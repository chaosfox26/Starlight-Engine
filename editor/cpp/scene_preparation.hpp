#pragma once
// Shared scene preparation for every scene that starts (editor Play, player start and restart): CPU stages first, then the renderer
// stage that ends in a generation-tagged GPU fence. A report is Ready only when finish_scene_upload completed it.
#include "adventure.hpp"
#include "loading_screen.hpp"
#include "native_session.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>
namespace starlight::editor {
struct PreparationReport {
    double geometryMs = 0, collisionMs = 0, fractureMs = 0, uploadMs = 0, gpuFenceMs = 0, totalMs = 0;
    size_t breakablesPrepared = 0, breakablesQueued = 0, objectsWarmed = 0;
    uint64_t framesAtReady = 0;
    uint64_t generation = 0, sceneGeneration = 0, preloadGeneration = 0, gpuFence = 0;
    size_t tasksDone = 0, tasksTotal = 0;
    bool cancelled = false;
    enum class State { Preparing, Ready, Incomplete, Failed } state = State::Preparing;
    std::string reason;
    json counters;
    std::string summary;
    bool ready() const { return state == State::Ready; }
    bool stopped() const { return state == State::Incomplete || state == State::Failed; }
    void fail(State why, const std::string& text) { if (state == State::Preparing) { state = why; reason = text; } }
    void complete() { if (state == State::Preparing) state = State::Ready; }
};
inline json runtime_counters_json(AdventurePlayer& adventure, const RenderStatus& r) {
    json out;
    const auto g = adventure.geometry_report();
    out["geometry"] = {{"objects", g.objects}, {"distinctGeometries", g.distinctGeometries}, {"rayTrees", g.rayTrees}, {"rayTreeBytes", g.rayTreeBytes}, {"geometryBytes", g.geometryBytes},
                       {"pinnedGeometries", g.pinnedGeometries}, {"pinnedBytes", g.pinnedBytes}, {"treeBytesBuiltHere", g.treeBytesBuiltHere}, {"overBudget", g.overBudget}};
    const auto cs = ObjectCache::shared().stats();
    out["objectCache"] = {{"boundsHits", cs.hits}, {"boundsMisses", cs.misses}, {"meshHits", cs.meshHits}, {"meshMisses", cs.meshMisses}, {"evictedInstances", cs.evictedInstances}, {"evictedGeometries", cs.evictedGeometries},
                          {"entries", cs.entries}, {"geometries", cs.geometries}, {"geometryBytes", cs.geometryBytes}, {"rayTreeBuilds", cs.rayTreeBuilds}, {"lateGeometryBuilds", cs.lateGeometryBuilds},
                          {"lateRayTreeBuilds", cs.lateRayTreeBuilds}, {"rayTrees", cs.rayTrees}, {"rayTreeBytes", cs.rayTreeBytes}, {"pinnedGeometries", cs.pinnedGeometries}, {"pinnedBytes", cs.pinnedBytes}, {"overBudget", cs.overBudget}};
    auto& b = runtime_bypass();
    out["runtimeBypass"] = {{"fractureComputedAtBreak", b.fractureComputedAtBreak.load()}, {"fractureNoEntry", b.fractureNoEntry.load()}, {"fractureStaleSource", b.fractureStaleSource.load()}, {"fractureNotStarted", b.fractureNotStarted.load()},
                            {"fractureWaited", b.fractureWaited.load()}, {"fractureFailedEntry", b.fractureFailedEntry.load()}, {"hullBuiltAtBreak", b.hullBuiltAtBreak.load()}, {"spatialIndexBuiltLate", b.spatialIndexBuiltLate.load()}};
    out["bruteForceRayTests"] = brute_force_ray_tests().load();
    out["uncachedStatic"] = {{"bounds", bypass_counters().staticBounds.load()}, {"meshes", bypass_counters().staticMeshes.load()}};
#ifdef STARLIGHT_CHARACTER_CONTROLLER
    const auto h = hull_report();
    out["hulls"] = {{"requested", h.requested}, {"exact", h.exact}, {"exactAfterFinerRetry", h.exactAfterFinerRetry}, {"reducedToLimit", h.reducedToLimit}, {"reducedBeyondBound", h.reducedBeyondBound}, {"boxFallbacks", h.boxFallbacks},
                    {"rejected", h.rejected}, {"unbuildable", h.unbuildable}, {"rebuiltFromHullVertices", h.rebuiltFromHullVertices}, {"worstReducedRelativeDeviation", h.worstReducedRelativeDeviation},
                    {"lastBoxFallback", h.lastBoxFallback}, {"lastUnbuildable", h.lastUnbuildable}};
    const auto s = shape_cache_report();
    out["shapeCache"] = {{"shapes", s.shapes}, {"bytes", s.bytes}, {"hits", s.hits}, {"misses", s.misses}, {"trimmed", s.trimmed}};
#endif
    const auto f = adventure.fracture_progress();
    out["destructibles"] = {{"total", f.total}, {"ready", f.ready}, {"prepared", f.prepared}, {"failed", f.failed}, {"abandoned", f.abandoned}, {"failures", f.failures}};
    out["render"] = {{"effectDeltaFrames", r.effect_delta_frames}, {"effectDeltaActors", r.effect_delta_actors}, {"effectWholeRepublish", r.effect_whole_republish}, {"lightChannelFrames", r.light_channel_frames},
                     {"lightChannelChanges", r.light_channel_changes}, {"emitterChannelChanges", r.emitter_channel_changes},
                     {"bodyCache", {{"hits", r.body_cache_hits}, {"misses", r.body_cache_misses}, {"evicted", r.body_cache_evicted}, {"pinnedSkips", r.body_cache_pinned_skips}, {"entries", r.body_cache_entries}, {"bytes", r.body_cache_bytes}}},
                     {"partsCache", {{"hits", r.parts_cache_hits}, {"misses", r.parts_cache_misses}, {"evicted", r.parts_cache_evicted}, {"entries", r.parts_cache_entries}, {"bytes", r.parts_cache_bytes}}},
                     {"preloadPinnedParts", r.preload_pinned_parts}, {"preloadPinHolds", r.preload_pin_holds}, {"uploadedResidentMeshes", r.rubble_uploaded_meshes}, {"sceneDeltaUpdates", r.scene_delta_updates}, {"sceneFullUpdates", r.scene_full_updates}};
    return out;
}
inline std::string preparation_summary(const json& c) {
    auto megabytes = [](double bytes) { char text[32]; std::snprintf(text, sizeof(text), "%.1f", bytes / 1048576.0); return std::string(text); };
    auto number = [&](const char* group, const char* name) { return c.at(group).at(name).get<uint64_t>(); };
    std::string text;
    const auto& g = c.at("geometry");
    text += "Prepared " + std::to_string(number("geometry", "objects")) + " objects: " + std::to_string(number("geometry", "distinctGeometries")) + " distinct shapes, " + std::to_string(number("geometry", "rayTrees")) + " collision trees, " +
            megabytes(double(number("geometry", "geometryBytes"))) + " MB of shape data, " + std::to_string(number("geometry", "pinnedGeometries")) + " shapes held in memory for this scene" + (g.at("overBudget").get<bool>() ? " (over the memory budget)" : "") + ".";
    const auto& d = c.at("destructibles");
    text += " Breakable objects: " + std::to_string(number("destructibles", "prepared")) + " of " + std::to_string(number("destructibles", "total")) + " ready";
    if (number("destructibles", "abandoned")) text += ", " + std::to_string(number("destructibles", "abandoned")) + " taken for work at the break";
    if (number("destructibles", "failed")) {
        text += ", " + std::to_string(number("destructibles", "failed")) + " could not be prepared (";
        const auto& failures = d.at("failures");
        for (size_t i = 0; i < failures.size() && i < 5; ++i) text += (i ? "; " : "") + failures[i].get<std::string>();
        if (failures.size() > 5) text += "; and " + std::to_string(failures.size() - 5) + " more";
        text += ")";
    }
    text += ".";
    const uint64_t lateShapes = number("objectCache", "lateGeometryBuilds"), lateTrees = number("objectCache", "lateRayTreeBuilds");
    text += lateShapes || lateTrees ? " Built late, after loading ended: " + std::to_string(lateShapes) + " shapes and " + std::to_string(lateTrees) + " collision trees." : std::string(" Nothing was built late.");
    std::string late;
    for (const auto& item : c.at("runtimeBypass").items()) if (item.value().get<uint64_t>()) late += std::string(late.empty() ? "" : ", ") + item.key() + " " + std::to_string(item.value().get<uint64_t>());
    if (c.at("bruteForceRayTests").get<uint64_t>()) late += std::string(late.empty() ? "" : ", ") + "slow ray tests " + std::to_string(c.at("bruteForceRayTests").get<uint64_t>());
    if (!late.empty()) text += " Work done late during play: " + late + ".";
    const auto& r = c.at("render");
    if (r.at("effectWholeRepublish").get<uint64_t>()) text += " The graphics scene was republished whole " + std::to_string(r.at("effectWholeRepublish").get<uint64_t>()) + " times because of effect objects.";
    if (c.contains("hulls") && (c.at("hulls").at("boxFallbacks").get<uint64_t>() || c.at("hulls").at("unbuildable").get<uint64_t>()))
        text += " Pieces that fell back to a plain box: " + std::to_string(c.at("hulls").at("boxFallbacks").get<uint64_t>()) + ", unbuildable: " + std::to_string(c.at("hulls").at("unbuildable").get<uint64_t>()) + ".";
    return text;
}
inline void capture_preparation_counters(PreparationReport& report, AdventurePlayer& adventure, const RenderStatus& status) {
    report.counters = runtime_counters_json(adventure, status);
    report.summary = preparation_summary(report.counters);
    const auto number = [&](const char* group, const char* name) { return double(report.counters.at(group).at(name).get<uint64_t>()); };
    starlight_diag::evlog("prep.geometry", number("geometry", "objects"), number("geometry", "distinctGeometries"), number("geometry", "rayTrees"));
    starlight_diag::evlog("prep.geobytes", number("geometry", "geometryBytes"), number("geometry", "rayTreeBytes"), number("geometry", "pinnedGeometries"));
    starlight_diag::evlog("prep.late", number("objectCache", "lateGeometryBuilds"), number("objectCache", "lateRayTreeBuilds"), double(report.counters.at("bruteForceRayTests").get<uint64_t>()));
    starlight_diag::evlog("prep.bypass1", number("runtimeBypass", "fractureComputedAtBreak"), number("runtimeBypass", "fractureNoEntry"), number("runtimeBypass", "fractureStaleSource"));
    starlight_diag::evlog("prep.bypass2", number("runtimeBypass", "fractureNotStarted"), number("runtimeBypass", "fractureWaited"), number("runtimeBypass", "fractureFailedEntry"));
    starlight_diag::evlog("prep.bypass3", number("runtimeBypass", "hullBuiltAtBreak"), number("runtimeBypass", "spatialIndexBuiltLate"));
    starlight_diag::evlog("prep.fracture", number("destructibles", "total"), number("destructibles", "prepared"), number("destructibles", "failed"));
#ifdef STARLIGHT_CHARACTER_CONTROLLER
    starlight_diag::evlog("prep.hull", number("hulls", "requested"), number("hulls", "boxFallbacks"), number("hulls", "unbuildable"));
    starlight_diag::evlog("prep.shapes", number("shapeCache", "shapes"), number("shapeCache", "hits"), number("shapeCache", "misses"));
#endif
    starlight_diag::evlog("prep.effects", number("render", "effectWholeRepublish"), number("render", "effectDeltaFrames"), number("render", "effectDeltaActors"));
    starlight_diag::evlog("prep.channels", number("render", "lightChannelFrames"), number("render", "lightChannelChanges"), number("render", "emitterChannelChanges"));
    starlight_diag::evlog("prep.body", report.counters.at("render").at("bodyCache").at("hits").get<double>(), report.counters.at("render").at("bodyCache").at("misses").get<double>(), report.counters.at("render").at("bodyCache").at("evicted").get<double>());
    starlight_diag::evlog("prep.body2", report.counters.at("render").at("bodyCache").at("pinnedSkips").get<double>(), report.counters.at("render").at("bodyCache").at("entries").get<double>(), report.counters.at("render").at("bodyCache").at("bytes").get<double>());
    starlight_diag::evlog("prep.parts", report.counters.at("render").at("partsCache").at("hits").get<double>(), report.counters.at("render").at("partsCache").at("misses").get<double>(), report.counters.at("render").at("partsCache").at("evicted").get<double>());
    starlight_diag::evlog("prep.parts2", report.counters.at("render").at("partsCache").at("entries").get<double>(), report.counters.at("render").at("partsCache").at("bytes").get<double>(), report.counters.at("render").at("preloadPinnedParts").get<double>());
    std::fprintf(stderr, "prepare: %s\n", report.summary.c_str());
}
inline std::function<void(double)>& loading_observer() { static std::function<void(double)> observer; return observer; }
inline double prepare_now_ms() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::atomic<uint64_t>& preparation_counter() { static std::atomic<uint64_t> counter{0}; return counter; }
inline std::atomic<bool>& preparation_cancel() { static std::atomic<bool> flag{false}; return flag; }
inline std::vector<HWND>& preparation_deferred_close() { static std::vector<HWND> windows; return windows; }
inline void release_deferred_close() {
    auto windows = std::move(preparation_deferred_close());
    preparation_deferred_close().clear();
    preparation_cancel() = false;
    for (HWND window : windows) if (IsWindow(window)) PostMessageW(window, WM_CLOSE, 0, 0);
}
inline bool loading_discards_input(UINT message) {
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR || message == WM_DEADCHAR || message == WM_INPUT || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
}
inline void pump_loading_messages() {
    static constexpr UINT frame = NativeSession::frameUpdateMessage;
    static constexpr UINT ranges[3][2] = {{0, WM_TIMER - 1}, {WM_TIMER + 1, frame - 1}, {frame + 1, 0xFFFF}};
    MSG message{};
    const auto stop = GetTickCount64() + 12;
    for (;;) {
        bool any = false;
        for (const auto& range : ranges) {
            while (PeekMessageW(&message, nullptr, range[0], range[1], PM_REMOVE)) {
                any = true;
                if (message.message == WM_QUIT) { PostQuitMessage(int(message.wParam)); preparation_cancel() = true; return; }
                if (message.message == WM_CLOSE || (message.message == WM_SYSCOMMAND && (message.wParam & 0xFFF0) == SC_CLOSE)) {
                    preparation_cancel() = true;
                    auto& windows = preparation_deferred_close();
                    if (message.hwnd && std::find(windows.begin(), windows.end(), message.hwnd) == windows.end()) windows.push_back(message.hwnd);
                    continue;
                }
                if (loading_discards_input(message.message)) continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
                if (GetTickCount64() >= stop) return;
            }
        }
        if (!any || GetTickCount64() >= stop) return;
    }
}
// Runs `work` on a worker thread while `tick` (message pump, loading screen, observer) runs on the calling thread. Completion is a flag the worker sets after
// `work` returns, separate from the progress fraction, so work that reports 1.0 early keeps being pumped until it has really finished. The worker is joined on
// every exit path, including an exception thrown by `tick`; `failure` is only read after the join.
template <class Tick>
inline void run_pumped_stage(const std::function<void(std::atomic<double>&)>& work, Tick&& tick, std::string& failure) {
    std::atomic<double> fraction{0};
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        try { work(fraction); } catch (const std::exception& e) { failure = e.what(); } catch (...) { failure = "unknown error"; }
        finished = true;
    });
    struct Join { std::thread& thread; ~Join() { if (thread.joinable()) thread.join(); } } join{worker};
    for (;;) {
        tick(std::clamp(fraction.load(), 0.0, 1.0));
        if (finished.load()) break;
        MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    worker.join();
}
// Reads a STARLIGHT_PREPARE_LIMIT_MS value. Text that is not a finite number of at least 1 keeps the 60 second default instead of becoming a 1 ms limit that fails every scene with breakables.
inline double prepare_limit_ms(const char* text) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    while (end && (*end == ' ' || *end == '\t')) ++end;
    return end != text && *end == 0 && std::isfinite(value) && value >= 1 ? value : 60000.0;
}
struct PreparationStages {
    static constexpr double geometry = .15, collision = .22, fracture = .38, upload = .25;
};
inline PreparationReport prepare_scene(AdventurePlayer& adventure, LoadingScreen& screen) {
    PreparationReport report;
    report.generation = ++preparation_counter();
    preparation_cancel() = false;
    const double begin = prepare_now_ms();
    double base = 0;
    report.tasksTotal = 2;
    auto cancelled = [&] {
        if (!preparation_cancel().load()) return false;
        report.cancelled = true;
        report.fail(PreparationReport::State::Incomplete, "Loading was cancelled because the window was closed");
        return true;
    };
    auto stage = [&](const std::string& label, double weight, double& elapsed, const std::function<void(std::atomic<double>&)>& work) {
        if (report.stopped() || cancelled()) return;
        const double started = prepare_now_ms();
        std::string failure, interface_failure;
        try {
            screen.update(base, label);
            run_pumped_stage(work, [&](double fraction) {
                pump_loading_messages();
                screen.update(base + weight * fraction, label);
                if (loading_observer()) loading_observer()(base + weight * fraction);
            }, failure);
        } catch (const std::exception& e) { interface_failure = e.what(); } catch (...) { interface_failure = "unknown error"; }
        if (!interface_failure.empty()) report.fail(PreparationReport::State::Failed, label + " stopped because the loading display failed: " + interface_failure);
        else if (!failure.empty()) report.fail(PreparationReport::State::Failed, label + " failed: " + failure);
        else ++report.tasksDone;
        elapsed = prepare_now_ms() - started;
        starlight_diag::evlog("prepare", base, elapsed, weight);
        base += weight;
        screen.update(base, label);
    };
    stage("Preparing scenery shapes", PreparationStages::geometry, report.geometryMs, [&](std::atomic<double>& fraction) { adventure.prepare_geometry(fraction); });
    stage("Preparing collision", PreparationStages::collision, report.collisionMs, [&](std::atomic<double>& fraction) { adventure.prepare_collision(fraction); });
    if (!report.stopped() && !cancelled()) {
        const double started = prepare_now_ms();
        const auto initial = adventure.fracture_progress();
        report.breakablesQueued = initial.total;
        report.tasksTotal += initial.total;
        const double limitMs = [] { char v[64]{}; const unsigned n = GetEnvironmentVariableA("STARLIGHT_PREPARE_LIMIT_MS", v, 64); return n && n < 64 ? prepare_limit_ms(v) : 60000.0; }();
        for (;;) {
            const auto progress = adventure.fracture_progress();
            report.breakablesPrepared = progress.ready;
            report.tasksDone = std::min<size_t>(2, report.tasksDone) + progress.ready;
            const double fraction = progress.total ? double(progress.ready) / double(progress.total) : 1.0;
            const std::string label = progress.total ? "Preparing breakable rocks (" + std::to_string(progress.ready) + " of " + std::to_string(progress.total) + ")"
                                                      : std::string("Preparing breakable rocks");
            pump_loading_messages();
            screen.update(base + PreparationStages::fracture * fraction, label);
            if (loading_observer()) loading_observer()(base + PreparationStages::fracture * fraction);
            if (progress.settled()) {
                if (!progress.ok()) {
                    std::string detail;
                    for (size_t i = 0; i < progress.failures.size() && i < 5; ++i) detail += (i ? "; " : "") + progress.failures[i];
                    report.fail(PreparationReport::State::Failed, std::to_string(progress.total - progress.ready) + " of " + std::to_string(progress.total) + " breakable objects could not be prepared: " + detail);
                }
                break;
            }
            if (cancelled()) break;
            if (prepare_now_ms() - started > limitMs) {
                report.fail(PreparationReport::State::Incomplete, "Only " + std::to_string(progress.ready) + " of " + std::to_string(progress.total) + " breakable objects finished preparing within " + (limitMs < 1000 ? std::to_string(int(limitMs)) + " ms" : std::to_string(int(limitMs / 1000)) + " seconds"));
                break;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        report.fractureMs = prepare_now_ms() - started;
        starlight_diag::evlog("prepare", base, report.fractureMs, PreparationStages::fracture);
        base += PreparationStages::fracture;
        screen.update(base, "Preparing breakable rocks");
    }
    report.totalMs = prepare_now_ms() - begin;
    return report;
}
template <class Renderer, class Status>
inline void finish_scene_upload(Renderer& renderer, const Status& first, AdventurePlayer& adventure, LoadingScreen& screen, PreparationReport& report) {
    if (report.stopped()) { capture_preparation_counters(report, adventure, renderer.status()); return; }
    struct PreloadPin { Renderer& renderer; explicit PreloadPin(Renderer& r) : renderer(r) { renderer.pin_preload(true); } ~PreloadPin() { renderer.pin_preload(false); } } preloadPin(renderer);
    const double started = prepare_now_ms();
    const double base = 1.0 - PreparationStages::upload;
    constexpr double installLimitMs = 30000, fenceLimitMs = 15000;
    std::vector<std::shared_ptr<const MeshPart>> prepared;
    if (adventure.take_prefractured_parts(prepared)) report.preloadGeneration = renderer.rubble_preload(std::move(prepared), true);
    const size_t cpuDone = report.tasksDone;
    report.tasksTotal = cpuDone + 3;
    bool fenceRequested = false;
    double fenceStarted = 0;
    auto stopWith = [&](PreparationReport::State why, const std::string& text, uint64_t frames) {
        report.fail(why, text);
        report.framesAtReady = frames;
    };
    for (;;) {
        pump_loading_messages();
        const auto status = renderer.status();
        if (!status.error.empty()) { stopWith(PreparationReport::State::Failed, "The graphics renderer reported an error: " + status.error, status.frames); break; }
        if (preparation_cancel().load()) { report.cancelled = true; stopWith(PreparationReport::State::Incomplete, "Loading was cancelled because the window was closed", status.frames); break; }
        const bool installed = report.sceneGeneration ? status.scene_generation_installed >= report.sceneGeneration : status.scene_updates > first.scene_updates;
        const bool preloaded = report.preloadGeneration == 0 || status.preload_generation_submitted >= report.preloadGeneration;
        if (installed && preloaded && !fenceRequested) {
            report.gpuFence = renderer.request_gpu_fence();
            fenceRequested = true;
            fenceStarted = prepare_now_ms();
        }
        const bool fenceDone = fenceRequested && status.gpu_fence_done >= report.gpuFence;
        report.tasksDone = cpuDone + (installed ? 1 : 0) + (preloaded ? 1 : 0) + (fenceDone ? 1 : 0);
        const double fraction = double(report.tasksDone - cpuDone) / 3.0;
        const char* label = !installed ? "Uploading scene data" : !preloaded ? "Uploading prepared pieces" : "Finishing on the graphics card";
        screen.update(base + PreparationStages::upload * fraction, label);
        if (loading_observer()) loading_observer()(base + PreparationStages::upload * fraction);
        if (fenceDone) {
            const bool sceneCovered = report.sceneGeneration == 0 || status.gpu_fence_scene_generation >= report.sceneGeneration;
            if (status.gpu_fence_preload_generation < report.preloadGeneration || !sceneCovered)
                stopWith(PreparationReport::State::Failed, "The graphics card finished its work before this scene's resources were submitted", status.frames);
            else report.framesAtReady = status.frames;
            report.gpuFenceMs = prepare_now_ms() - fenceStarted;
            break;
        }
        const std::string paused = status.suspended ? " (the window is minimized, so the graphics renderer is paused)" : std::string();
        if (!fenceRequested && prepare_now_ms() - started > installLimitMs) {
            stopWith(PreparationReport::State::Incomplete, std::string(installed ? "The prepared pieces did not reach the graphics card within 30 seconds" : "The scene was not accepted by the graphics renderer within 30 seconds") + paused, status.frames);
            break;
        }
        if (fenceRequested && prepare_now_ms() - fenceStarted > fenceLimitMs) {
            stopWith(PreparationReport::State::Incomplete, "The graphics card did not finish the scene's work within 15 seconds" + paused, status.frames);
            break;
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    report.uploadMs = prepare_now_ms() - started;
    report.totalMs += report.uploadMs;
    starlight_diag::evlog("prepare", base, report.uploadMs, PreparationStages::upload);
    starlight_diag::evlog("prepare.total", report.totalMs, double(report.breakablesPrepared), double(report.breakablesQueued));
    capture_preparation_counters(report, adventure, renderer.status());
    report.complete();
    if (report.ready()) screen.update(1.0, "Ready");
    else screen.update(base + PreparationStages::upload * 0.5, "Loading did not finish: " + report.reason);
}
}
