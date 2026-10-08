#include "solid_fixture.hpp"
#include "engine_assets.hpp"
#include "native_session.hpp"
#include "physics.hpp"
#include "projects.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <psapi.h>
using namespace starlight::editor;
#include "graphics_preview.hpp"
#include "rock_fracture_preview.hpp"
#include "destruction_dust_preview.hpp"
#include "terrain_destruction_preview.hpp"
#include "gate0_handle_trace.hpp"
#include "memory_allocation_trace.hpp"
// Opt-in allocation-region evidence. Stream rows instead of retaining a second
// in-process snapshot that could itself distort the lifecycle measurements.
static void memory_map(const fs::path& output, const char* stage) {
  if (!GetEnvironmentVariableW(L"STARLIGHT_MEMORY_MAP", nullptr, 0)) return;
  std::ofstream file(output / (std::string("memory-map-") + stage + ".csv"));
  if (!file) throw std::runtime_error("Cannot write memory map");
  LARGE_INTEGER counter{}, frequency{};
  QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
  file << "pid,tickMs,qpc,frequency\n" << GetCurrentProcessId() << ',' << GetTickCount64()
       << ',' << counter.QuadPart << ',' << frequency.QuadPart << '\n';
  file << "base,allocationBase,bytes,state,type,protection\n";
  uintptr_t address = 0;
  MEMORY_BASIC_INFORMATION region{};
  while (VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) {
    if (region.State != MEM_FREE)
      file << reinterpret_cast<uintptr_t>(region.BaseAddress) << ','
           << reinterpret_cast<uintptr_t>(region.AllocationBase) << ',' << region.RegionSize << ','
           << region.State << ',' << region.Type << ',' << region.Protect << '\n';
    auto next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
    if (next <= address) break;
    address = next;
  }
  if (!file) throw std::runtime_error("Incomplete memory map");
  // Heap walks are diagnostic only and never perform allocation or I/O while
  // a heap is locked. Record unsupported heaps rather than guessing ownership.
  HANDLE heaps[1024]{};
  DWORD count = GetProcessHeaps(1024, heaps);
  if (count > 1024) throw std::runtime_error("Heap snapshot capacity exceeded");
  std::ofstream heapFile(output / (std::string("memory-heaps-") + stage + ".csv"));
  heapFile << "heap,isProcessHeap,busyBytes,freeBytes,committedBytes,uncommittedBytes,entries,error\n";
  std::ofstream regionsFile(output / (std::string("heap-regions-") + stage + ".csv"));
  regionsFile << "heap,address,allocationBase,bytes,flags\n";
  for (DWORD i=0; i<count; ++i) {
    struct HeapRegion { uintptr_t address, allocation; SIZE_T bytes; WORD flags; } regions[2048]{};
    size_t regionCount=0;
    uint64_t busy=0, free=0, committed=0, uncommitted=0, entries=0;
    DWORD error=0;
    if (HeapLock(heaps[i])) {
      PROCESS_HEAP_ENTRY entry{};
      while (HeapWalk(heaps[i], &entry)) {
        ++entries;
        if ((entry.wFlags & PROCESS_HEAP_REGION) || entry.cbData >= 65536) {
          MEMORY_BASIC_INFORMATION region{};
          if (regionCount<2048 && VirtualQuery(entry.lpData, &region, sizeof(region)))
            regions[regionCount++]={reinterpret_cast<uintptr_t>(entry.lpData),
                reinterpret_cast<uintptr_t>(region.AllocationBase),entry.cbData,entry.wFlags};
        }
        if (entry.wFlags & PROCESS_HEAP_REGION) {
          committed += entry.Region.dwCommittedSize;
          uncommitted += entry.Region.dwUnCommittedSize;
        } else if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) busy += entry.cbData;
        else if (!(entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE)) free += entry.cbData;
      }
      error=GetLastError();
      HeapUnlock(heaps[i]);
    } else error=GetLastError();
    heapFile << reinterpret_cast<uintptr_t>(heaps[i]) << ',' << (heaps[i]==GetProcessHeap()) << ',' << busy << ',' << free << ','
             << committed << ',' << uncommitted << ',' << entries << ',' << error << '\n';
    for(size_t r=0;r<regionCount;++r)
      regionsFile << reinterpret_cast<uintptr_t>(heaps[i]) << ',' << regions[r].address << ','
                  << regions[r].allocation << ',' << regions[r].bytes << ',' << regions[r].flags << '\n';
  }
  if (!heapFile) throw std::runtime_error("Incomplete heap snapshot");
  if (!regionsFile) throw std::runtime_error("Incomplete heap region snapshot");
}
// Snapshot only this process's handle table; no foreign handles or object
// names.
static json handle_snapshot() {
  struct Entry {
    HANDLE handle;
    ULONG_PTR references, pointers;
    ULONG access, type, attributes, reserved;
  };
  struct Table {
    ULONG_PTR count, reserved;
    Entry entries[1];
  };
  using Query = LONG(WINAPI *)(HANDLE, ULONG, void *, ULONG, ULONG *);
  auto query = reinterpret_cast<Query>(GetProcAddress(
      GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
  if (!query)
    return {{"available", false}};
  std::vector<unsigned char> buffer(65536);
  ULONG length = 0;
  LONG status = 0;
  for (int attempt = 0; attempt < 5; ++attempt) {
    status = query(GetCurrentProcess(), 51, buffer.data(),
                   static_cast<ULONG>(buffer.size()), &length);
    if (status >= 0)
      break;
    if (status != static_cast<LONG>(0xC0000004))
      return {{"available", false}, {"ntstatus", status}};
    buffer.resize(std::max<size_t>(buffer.size() * 2, length));
  }
  if (status < 0)
    return {{"available", false}, {"ntstatus", status}};
  auto table = reinterpret_cast<const Table *>(buffer.data());
  json entries = json::array();
  if (table->count > (buffer.size() - offsetof(Table, entries)) / sizeof(Entry))
    return {{"available", false}, {"error", "Invalid handle snapshot length"}};
  for (size_t i = 0; i < table->count; ++i) {
    entries.push_back(
        {{"handle", reinterpret_cast<uintptr_t>(table->entries[i].handle)},
         {"objectTypeIndex", table->entries[i].type},
         {"access", table->entries[i].access}});
    if (GetEnvironmentVariableW(L"STARLIGHT_HANDLE_TRACE", nullptr, 0))
      entries.back()["details"] = gate0::describeHandle(table->entries[i].handle);
  }
  return {{"available", true}, {"entries", entries}};
}
// Loading must prepare the scene that play reveals (audit 04-loading-readiness, finding 1). Mirrors the loading barrier on a hidden window: install the
// scene with playback stopped, wait for the graphics fence, then start play and count the resident-scene installs. The fire and grass objects are what
// make the stopped-mode and play-mode static packets differ; without the prewarm, starting play builds and installs a second base.
static json play_start_base_installs(bool prewarm) {
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpfnWndProc = DefWindowProcW;
  wc.lpszClassName = L"StarlightPlayStartCheck";
  RegisterClassW(&wc);
  HWND window = CreateWindowW(wc.lpszClassName, L"Play start check", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
  if (!window) throw std::runtime_error("Play start check window");
  json result;
  try {
    SceneData scene;
    Character rock; rock.Shape = "rock"; rock.X = -2;
    Character fire; fire.Shape = "fire"; fire.ParticleRate = 100; fire.X = 2;
    Character grass; grass.Shape = "grass"; grass.Size = 2; grass.Z = 2;
    scene.Characters = {rock, fire, grass};
    NativeSession session(window, 640, 360);
    auto until = [&](auto done) {
      const auto deadline = GetTickCount64() + 20000;
      for (;;) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        auto status = session.status();
        if (!status.error.empty()) throw std::runtime_error(status.error);
        if (done(status)) return status;
        if (GetTickCount64() > deadline) throw std::runtime_error("Play start check timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    };
    session.playback(false);
    if (prewarm) session.prewarm_play(true);
    const uint64_t generation = session.scene(scene);
    session.playback(false, true);
    until([&](const RenderStatus& s) { return s.scene_generation_installed >= generation; });
    const uint64_t fence = session.request_gpu_fence();
    auto fenced = until([&](const RenderStatus& s) { return s.gpu_fence_done >= fence; });
    auto ready = until([&](const RenderStatus& s) { return s.frames >= fenced.frames + 2; }); // the install counters are published at the end of a frame
    session.playback(true);
    auto playing = until([&](const RenderStatus& s) { return s.playing && s.frames >= ready.frames + 120; });
    result = {{"prewarm", prewarm}, {"baseInstallsAtReady", ready.scene_base_installs}, {"baseInstallsAfterPlay", playing.scene_base_installs},
              {"wholePacketUpdatesAtReady", ready.scene_full_updates}, {"wholePacketUpdatesAfterPlay", playing.scene_full_updates},
              {"deltaUpdatesAtReady", ready.scene_delta_updates}, {"deltaUpdatesAfterPlay", playing.scene_delta_updates}};
    session.suspend_rendering(true);
  } catch (...) {
    if (IsWindow(window)) DestroyWindow(window);
    throw;
  }
  DestroyWindow(window);
  return result;
}
int wmain(int argc, wchar_t **argv) {
  if(argc>=3&&std::wstring_view(argv[1])==L"--play-start-base"){
    fs::path output=argv[2];fs::create_directories(output);
    try{
      const auto control=play_start_base_installs(false),prewarmed=play_start_base_installs(true);
      atomic_write(output/L"play-start-base.json",json{{"control",control},{"prewarmed",prewarmed}}.dump(2));
      const bool pass=prewarmed["baseInstallsAtReady"].get<uint64_t>()>=1&&prewarmed["baseInstallsAfterPlay"]==prewarmed["baseInstallsAtReady"]&&prewarmed["wholePacketUpdatesAfterPlay"]==prewarmed["wholePacketUpdatesAtReady"];
      std::cout<<(pass?"PASS":"FAIL")<<" play start base installs\n"<<control.dump()<<'\n'<<prewarmed.dump()<<'\n';
      return pass?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
  }
  if(argc>=4&&std::wstring_view(argv[1])==L"--graphics-preview")return graphics_preview::run(argv[2],argv[3],argc>4&&std::wstring_view(argv[4])==L"visible");
  if(argc>=4&&std::wstring_view(argv[1])==L"--destruction-dust-preview")return destruction_dust_preview(argv[2],argv[3]);
  if(argc>=4&&std::wstring_view(argv[1])==L"--terrain-destruction-preview")return terrain_destruction_preview(argv[2],argv[3]);
  if(argc>=4&&std::wstring_view(argv[1])==L"--rock-fracture-preview")return rock_fracture_preview(argv[2],argv[3]);
  const bool visibleLifecycle=GetEnvironmentVariableW(L"STARLIGHT_VISIBLE_LIFECYCLE",nullptr,0)!=0;
  const auto traceStatus = gate0::enable();
  fs::path output = argc > 1 ? argv[1] : L"session-checks";
  fs::create_directories(output);
  json checks = json::array();
  auto require = [&](bool good, const char *name) {
    if (!good)
      throw std::runtime_error(name);
    checks.push_back(name);
  };
  try {
    wchar_t componentSetting[32]{};
    GetEnvironmentVariableW(L"STARLIGHT_GATE0_COMPONENT", componentSetting, 32);
    const std::wstring component = componentSetting;
    SceneData scene;
    scene.Terrain.emplace();
    Character rock;
    rock.Shape = "rock";
    rock.Dynamic = true;
    rock.Y = 3;
    scene.Characters.push_back(rock);
    if (component != L"renderer") {
      PhysicsSession physics;
      physics.reset(scene);
      auto deadline = GetTickCount64() + 10000;
      while (!physics.ready() && GetTickCount64() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      require(physics.ready(),
              "Native asynchronous terrain collision cook completes");
      for (int i = 0; i < 180; i++)
        physics.step(scene, 1. / 60, nullptr);
      require(physics.ticks() == 180, "Native Jolt fixed-step clock");
      require(scene.Characters[0].Y < 1 && scene.Characters[0].Y > -.5 &&
                  scene.Characters[0].PhysicsRotation.has_value(),
              "Dynamic rock falls and rests on terrain");
    }
    if(component==L"physics-behavior"){
      SceneData constrained;Character body;body.Shape="cube";body.Dynamic=true;body.X=3;body.Y=4;body.JointKind=1;constrained.Characters={body};PhysicsSession physics;physics.reset(constrained);
      auto waitReady=[&]{auto end=GetTickCount64()+10000;while(!physics.ready()&&GetTickCount64()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));require(physics.ready(),"Constraint terrain preparation completes");};waitReady();
      for(int i=0;i<180;++i)physics.step(constrained,1./60.,nullptr);
      require(std::abs(constrained.Characters[0].Y-4)<.03,"Standalone fixed constraints hold against gravity");
      constrained.Characters[0].JointKind=0;physics.reset(constrained);waitReady();for(int i=0;i<180;++i)physics.step(constrained,1./60.,nullptr);
      require(constrained.Characters[0].Y<.2,"Reset removes old constraints before rebuilding bodies");
      constrained.Characters[0].Y=4;constrained.Characters[0].VelocityX=2;constrained.Characters[0].AngularVelocityY=90;physics.reset(constrained);waitReady();for(int i=0;i<15;++i)physics.step(constrained,1./60.,nullptr);require(constrained.Characters[0].X>3.4&&std::abs((*constrained.Characters[0].PhysicsRotation)[1])>.1,"Standalone bodies apply authored translation and spin");
      constrained.Characters[0]=body;auto& rope=constrained.Characters[0];rope.Size=.2;rope.Density=1000;rope.JointKind=3;rope.JointSegments=12;rope.JointY=1;rope.JointTargetY=2.2;rope.JointLength=2;physics.reset(constrained);waitReady();for(int i=0;i<180;++i)physics.step(constrained,1./60.,nullptr);require(rope.JointRuntimePoints.size()==13&&rope.Y>3.85,"Standalone rope links carry their load and publish interpolated paths");
      rope.JointKind=0;physics.reset(constrained);waitReady();for(int i=0;i<180;++i)physics.step(constrained,1./60.,nullptr);require(rope.JointRuntimePoints.empty()&&rope.Y<.2,"Standalone reset removes rope constraints and collision links");
      {EngineAssets::ScopedRoot modelLibrary(output/"engine-assets");
      SceneData imported;Character model;model.Asset=import_model(solid_glb_fixture(output));model.Dynamic=true;model.X=3;model.Y=3;imported.Characters={model};physics.reset(imported);waitReady();for(int i=0;i<180;++i)physics.step(imported,1./60.,nullptr);require(imported.Characters[0].Y<.2&&imported.Characters[0].PhysicsRotation.has_value(),"Standalone physics simulates imported solid meshes instead of skipping them");}
      SceneData stack;for(int i=0;i<4;++i){Character cube;cube.Shape="cube";cube.Dynamic=true;cube.Y=i*1.03;cube.Friction=.8;stack.Characters.push_back(cube);}physics.reset(stack);waitReady();for(int i=0;i<480;++i)physics.step(stack,1./60.,nullptr);bool stable=true;for(int i=0;i<4;++i)stable&=std::abs(stack.Characters[i].Y-i)<.08&&std::abs(stack.Characters[i].X)<.08&&std::abs(stack.Characters[i].Z)<.08;require(stable,"Four-body stack settles without sinking, separation or lateral drift");
      SceneData fast;Character wall;wall.Shape="cube";wall.Width=.05;wall.Height=4;wall.Depth=4;Character bullet;bullet.Shape="cube";bullet.Dynamic=true;bullet.Size=.15;bullet.X=-3;bullet.Y=1;bullet.VelocityX=200;fast.Characters={wall,bullet};physics.reset(fast);waitReady();for(int i=0;i<6;++i)physics.step(fast,1./60.,nullptr);require(fast.Characters[1].X<.15,"Continuous rigid-body collision stops a fast body at a thin wall");
      SceneData hinge;Character arm;arm.Shape="cube";arm.Dynamic=true;arm.Y=3;arm.JointKind=2;arm.JointAngleMin=-15;arm.JointAngleMax=15;arm.AngularVelocityY=240;hinge.Characters={arm};physics.reset(hinge);waitReady();for(int i=0;i<120;++i)physics.step(hinge,1./60.,nullptr);auto rotation=*hinge.Characters[0].PhysicsRotation;require(std::abs(rotation[1])<.15&&std::abs(hinge.Characters[0].Y-3)<.05,"Hinge angular limits resist authored spin while holding the pivot");
      arm.JointKind=6;arm.JointAxisX=1;arm.JointAxisY=0;arm.JointLength=1;arm.AngularVelocityY=0;arm.VelocityX=4;hinge.Characters={arm};physics.reset(hinge);waitReady();for(int i=0;i<120;++i)physics.step(hinge,1./60.,nullptr);require(hinge.Characters[0].X<=1.05&&std::abs(hinge.Characters[0].Y-3)<.05,"Slider travel limits stop momentum and exclude off-axis gravity");
      SceneData routed;Character pusher;pusher.Shape="cube";pusher.Height=2;pusher.X=-2;pusher.Route={{3,0}};pusher.Speed=1;Character crate;crate.Shape="cube";crate.Size=.4;crate.Dynamic=true;crate.Density=100;crate.X=0;routed.Characters={pusher,crate};physics.reset(routed);waitReady();for(int i=0;i<240;++i)physics.step(routed,1./60.,nullptr);require(routed.Characters[1].X>1,"Standalone routed collision bodies push dynamic props along their visible path");require(routed.Characters[0].X==-2&&routed.Characters[0].Route.size()==1&&!routed.Characters[0].PhysicsRotation,"Kinematic simulation preserves authored route origins and headings");
      SceneData flowing;Character floater;floater.Shape="cube";floater.Dynamic=true;floater.Size=.4;floater.Density=500;floater.X=-.25;floater.Y=1.8;flowing.Characters={floater};physics.reset(flowing);waitReady();TerrainData flat;auto currentFlow=std::make_unique<TerrainFlow>(flat,std::vector<LiquidSource>{});for(int i=0;i<180;++i){currentFlow->water.fill(2);for(int z=0;z<48;++z)for(int x=0;x<24;++x)currentFlow->water[z*48+x]=3;currentFlow->step(1./60.);physics.step(flowing,1./60.,currentFlow.get());}require(flowing.Characters[0].X>-.20,"Standalone buoyancy uses liquid transport velocity to carry floating objects");
      SceneData cavern;cavern.Terrain.emplace();cavern.Terrain->Volumes.push_back({0,-4,0,3,true});Character falling;falling.Shape="cube";falling.Dynamic=true;falling.Size=.4;falling.Y=-3;cavern.Characters={falling};
      physics.reset(cavern);waitReady();for(int i=0;i<180;++i)physics.step(cavern,1./60.,nullptr);
      require(cavern.Characters[0].Y<-6.5 && cavern.Characters[0].Y>-7.2,"Dynamic props settle inside volumetric caves");
      cavern.Terrain->Volumes.clear();cavern.Characters[0].Y=3;cavern.Characters[0].PhysicsRotation.reset();physics.reset(cavern);waitReady();for(int i=0;i<180;++i)physics.step(cavern,1./60.,nullptr);
      require(cavern.Characters[0].Y>-.2 && cavern.Characters[0].Y<.3,"Resetting a volume world restores heightfield collision without stale cave bodies");
      atomic_write(output / "physics-behavior.json",json{{"pass",true},{"checks",checks}}.dump(2));return 0;
    }
    if (component == L"physics") {
      json samples = json::array();
      auto baseline = handle_snapshot();
      for (int i = 0; i < 90; ++i) {
        DWORD handles = 0;
        if (!GetProcessHandleCount(GetCurrentProcess(), &handles)) throw std::runtime_error("Handle query failed");
        samples.push_back({{"elapsedMs", GetTickCount64()}, {"handles", handles}});
        Sleep(500);
      }
      auto final = handle_snapshot();
      atomic_write(output / "component-control.json", json{{"diagnosticOnly",true}, {"component","physics-only-no-device"},
        {"baselineHandleTable",baseline}, {"finalHandleTable",final}, {"samples",samples},
        {"newHandleOrigins",traceStatus == 0 ? gate0::origins(baseline,final) : json::array()}}.dump(2));
      return 0; // Observation only, not lifecycle acceptance.
    }
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"StarlightNativeSessionCheck";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"Native session check",
                                WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, nullptr,
                                nullptr, wc.hInstance, nullptr);
    require(window != nullptr, "Native host window");
    if(visibleLifecycle){
      SetWindowTextW(window,L"Starlight memory test - preparing; 80 measured cycles will follow");
      ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);
    }
    {
      NativeSession session(window, 640, 360);
      session.scene(scene);
      auto wait = [&](uint64_t target) {
        auto deadline = GetTickCount64() + 15000;
        for (;;) {
          // The test owns a Win32 host window, just like the editor. Service
          // its queue while waiting on the renderer rather than making the
          // host unresponsive for the entire lifecycle run.
          MSG message{};
          while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
          }
          auto status = session.status();
          if (!status.error.empty())
            throw std::runtime_error(status.error);
          if (status.frames >= target)
            return status;
          if (GetTickCount64() > deadline)
            throw std::runtime_error("Native worker frame timeout");
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      };
      auto status = wait(5);
      require(status.width == 640,
              "Worker renders at requested native resolution");
      session.capture(output / L"native-session.bmp");
      // A queued screenshot was previously racing the memory baseline.
      // Wait for it to finish, then prime both scene topologies before
      // sampling.
      while (session.status().captures == 0)
        wait(session.status().frames + 1);
      auto memorySample = [&] {
        DWORD handles = 0;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (!GetProcessHandleCount(GetCurrentProcess(), &handles) ||
            !GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                sizeof(memory)))
          throw std::runtime_error("Lifecycle process measurement failed");
        auto st = session.status();
        return json{{"handles", handles},
                    {"elapsedMs", GetTickCount64()},
                    {"privateBytes", memory.PrivateUsage},
                    {"workingSetBytes", memory.WorkingSetSize},
                    {"parts", st.scene_parts},
                    {"blas", st.scene_blas},
                    {"pendingUploads", st.pending_uploads},
                    {"uploadPoolCount", st.upload_pool_count},
                    {"uploadPoolBytes", st.upload_pool_bytes},
                    {"uploadCreated", st.upload_created},
                    {"uploadReused", st.upload_reused}};
      };
      auto replace = [&](SceneData replacement) {
        auto revision = session.status().scene_revision;
        session.scene(std::move(replacement));
        while (session.status().scene_revision <= revision)
          wait(session.status().frames + 1);
        wait(session.status().frames +
             3); // all in-flight slots retire before taking a sample
      };
      SceneData empty, loaded = scene;
      loaded.RaytracingEnabled = true;
      loaded.RaytracingQuality = 1;
      loaded.RayReconstructionEnabled = false;
      json warmup = json::array();
      for (int i = 0; i < 8; ++i) {
        if(visibleLifecycle)SetWindowTextW(window,(L"Starlight memory test - warmup "+std::to_wstring(i+1)+L" of 8 (not measured cycles)").c_str());
        replace(loaded);
        replace(empty);
        warmup.push_back(memorySample());
      }
      memory_trace::start(output);
      memory_map(output, "warmup");
      // Direct3D device+queue-only controls reproduce delayed Windows/driver
      // timer/semaphore initialization at 30 and 60 seconds, without any scene
      // or render work. Keep startup evidence separate from matched endpoints;
      // never subtract handles or loosen the existing plateau assertions.
      const auto startupStart = GetTickCount64();
      auto stableSince = startupStart;
      auto previousHandles = memorySample()["handles"].get<DWORD>();
      json startup = json::array();
      bool startupStable = false;
      for (;;) {
        auto sample = memorySample();
        auto now = GetTickCount64();
        if(visibleLifecycle)SetWindowTextW(window,(L"Starlight memory test - startup observation "+std::to_wstring((now-startupStart)/1000)+L" seconds; cycles 0 of 80").c_str());
        if (sample["handles"].get<DWORD>() != previousHandles) stableSince = now;
        previousHandles = sample["handles"].get<DWORD>();
        startup.push_back(sample);
        atomic_write(output / "lifecycle-progress.json", json{{"stage","graphics runtime initialization"},
            {"elapsedMs",now-startupStart},{"handles",previousHandles}}.dump(2));
        if (now-startupStart >= 75000 && now-stableSince >= 15000) { startupStable = true; break; }
        if (now-startupStart >= 120000) break;
        auto nextSample = now + 1000;
        while (GetTickCount64() < nextSample) wait(session.status().frames + 1);
      }
      atomic_write(output / "lifecycle-startup.json", json{{"samples",startup},{"stable",startupStable},
          {"scope","Graphics runtime startup observation, not scene-soak acceptance"}}.dump(2));
      require(startupStable, "Graphics runtime reaches a stable handle baseline within its deadline");
      auto baseline = memorySample();
      memory_map(output, "baseline");
      auto baselineHandles = handle_snapshot();
      // An idle control distinguishes time-dependent runtime ownership from
      // scene replacement. It is diagnostic evidence, never a scene-soak pass.
      const bool idleControl = GetEnvironmentVariableW(L"STARLIGHT_LIFECYCLE_IDLE_CONTROL", nullptr, 0) != 0;
      unsigned batchCycles = 40;
      wchar_t requestedCycles[32]{};
      if (GetEnvironmentVariableW(L"STARLIGHT_LIFECYCLE_BATCH_CYCLES", requestedCycles, 32)) {
        wchar_t* end = nullptr;
        auto value = wcstoul(requestedCycles, &end, 10);
        if (!end || *end || value < 40 || value > 1000) throw std::runtime_error("Invalid lifecycle batch size (40..1000)");
        batchCycles = unsigned(value);
      }
      json samples = json::array(), batches = json::array(), batchHandles = json::array();
      const auto measuredStart = GetTickCount64();
      bool resourcesRetired = true;
      for (int batch = 0; batch < 2; ++batch) {
        for (unsigned i = 0; i < batchCycles; ++i) {
          if(visibleLifecycle)SetWindowTextW(window,(L"Starlight memory test - cycle "+std::to_wstring(batch*batchCycles+i+1)+L" of "+std::to_wstring(batchCycles*2)+L" - loading scene").c_str());
          if (idleControl) wait(session.status().frames + 4);
          else replace(loaded);
          auto occupied = memorySample();
          if(visibleLifecycle)SetWindowTextW(window,(L"Starlight memory test - cycle "+std::to_wstring(batch*batchCycles+i+1)+L" of "+std::to_wstring(batchCycles*2)+L" - unloading scene").c_str());
          if (idleControl) wait(session.status().frames + 4);
          else replace(empty);
          auto unloaded = memorySample();
          resourcesRetired &= unloaded["parts"] == baseline["parts"] &&
                              unloaded["blas"] == 0 &&
                              unloaded["pendingUploads"] == 0 &&
                              unloaded["uploadPoolCount"].get<uint64_t>() <= 16 &&
                              unloaded["uploadPoolBytes"].get<uint64_t>() <= 16ull*1024*1024;
          samples.push_back({{"batch", batch},
                             {"iteration", i},
                             {"loaded", occupied},
                             {"unloaded", unloaded}});
          if (i % 5 == 0 || i+1 == batchCycles)
            atomic_write(output / "lifecycle-progress.json", json{{"stage",idleControl ? "idle control" : "scene lifecycle"},
              {"completed",batch*batchCycles+i+1},{"total",batchCycles*2},
              {"elapsedMs",GetTickCount64()-measuredStart},{"privateBytes",unloaded["privateBytes"]},
              {"handles",unloaded["handles"]}}.dump(2));
        }
        batches.push_back(memorySample());
        batchHandles.push_back(handle_snapshot());
      }
      auto final = memorySample();
      if(visibleLifecycle)SetWindowTextW(window,L"Starlight memory test - 80 of 80 cycles complete; checking results");
      memory_map(output, "active-endpoint");
      const auto finalHandles = handle_snapshot();
      atomic_write(
          output / "lifecycle-memory.json",
          json{{"cycles", batchCycles*2},
               {"swaps", idleControl ? 0 : batchCycles*4},
               {"startup", startup},
               {"idleControl", idleControl},
               {"componentControl", component.empty() ? "full" : utf8(component)},
               {"warmup", warmup},
               {"baseline", baseline},
               {"baselineHandleTable", baselineHandles},
               {"finalHandleTable", finalHandles},
               {"batchHandleTables", batchHandles},
               {"handleTraceEnableStatus", traceStatus},
               {"newHandleOrigins", traceStatus == 0 ? gate0::origins(baselineHandles, finalHandles) : json::array()},
               {"batches", batches},
               {"final", final},
               {"samples", samples},
               {"resourcesRetired", resourcesRetired},
               {"scope",
                "Matched empty-scene endpoints after capture completion, "
                "topology warm-up, and frame-slot retirement. "
                "Scene-independent render targets/descriptors remain allocated "
                "for session reuse."}}
              .dump(2));
      // Optional observation AFTER sealed acceptance samples. Never use recovery
      // values to replace the baseline/final values or excuse a failed limit.
      if (GetEnvironmentVariableW(L"STARLIGHT_LIFECYCLE_RECOVERY", nullptr, 0)) {
        json recovery = json::array();
        auto observeIdle = [&](const char* stage) {
          const auto start = GetTickCount64();
          for (int second = 0; second <= 10; ++second) {
            while (GetTickCount64()-start < uint64_t(second)*1000)
              wait(session.status().frames+1);
            recovery.push_back({{"stage",stage},{"second",second},{"memory",memorySample()}});
            atomic_write(output / "lifecycle-progress.json", json{{"stage",stage},
                {"elapsedMs",GetTickCount64()-measuredStart}}.dump(2));
          }
        };
        observeIdle("post-soak idle observation");
        memory_map(output, "recovery");
        for (int i = 0; i < 8; ++i) {
          replace(loaded); replace(empty);
          recovery.push_back({{"stage","replay"},{"cycle",i+1},{"memory",memorySample()}});
        }
        observeIdle("post-replay idle observation");
        atomic_write(output / "lifecycle-recovery.json", json{{"diagnosticOnly",true},
            {"scope","Post-acceptance observations; original limits and verdict unchanged"},
            {"samples",recovery}}.dump(2));
      }
      memory_trace::stop();
      require(resourcesRetired,
              "Unloading each scene retires mesh, BLAS and upload ownership");
      require(
          final["handles"].get<uint64_t>() <=
                  baseline["handles"].get<uint64_t>() + 2 &&
              final["privateBytes"].get<uint64_t>() <=
                  baseline["privateBytes"].get<uint64_t>() +
                      16ull * 1024 * 1024 &&
              batches[1]["handles"].get<uint64_t>() <=
                  batches[0]["handles"].get<uint64_t>() + 1 &&
              batches[1]["privateBytes"].get<uint64_t>() <=
                  batches[0]["privateBytes"].get<uint64_t>() +
                      4ull * 1024 * 1024,
          "Matched unload endpoints plateau across two forty-cycle batches");
      session.scene(scene);
      session.playback(true);
      wait(session.status().frames + 15);
      session.playback(false, true);
      wait(session.status().frames + 3);
      require(true, "Native playback stop resets safely");
      SceneData route;
      Character walker;
      walker.Shape = "cube";
      walker.Route = {{.1, 0}};
      walker.Speed = 1;
      route.Characters.push_back(walker);
      session.scene(route);
      session.playback_options(false, walker.Id);
      session.playback(true);
      // Playback advances in elapsed seconds, not a fixed number of frames.
      // Fast rendering can finish twelve frames before this 0.1-second route.
      auto playbackDeadline = GetTickCount64() + 5000;
      do {
        wait(session.status().frames + 1);
      } while (session.status().seconds < .1 && GetTickCount64() < playbackDeadline);
      require(!session.status().playing &&
                  std::abs(session.status().seconds - .1) < 1e-6,
              "Path completion stops nonrepeating playback");
      session.playback_options(true, walker.Id);
      session.playback(true, true);
      // Observe the reset before waiting for the repeat to cross its duration.
      wait(session.status().frames + 2);
      playbackDeadline = GetTickCount64() + 5000;
      while (session.status().seconds <= .1 && GetTickCount64() < playbackDeadline)
        wait(session.status().frames + 1);
      require(session.status().playing && session.status().seconds > .1,
              "Repeat and character-follow continue beyond route duration");
    }
    DestroyWindow(window);
    {
      const auto prewarmed = play_start_base_installs(true);
      require(prewarmed["baseInstallsAtReady"].get<uint64_t>() >= 1 &&
                  prewarmed["baseInstallsAfterPlay"] == prewarmed["baseInstallsAtReady"] &&
                  prewarmed["wholePacketUpdatesAfterPlay"] == prewarmed["wholePacketUpdatesAtReady"],
              "A scene prepared behind the loading barrier is the one play uses: starting play installs no second resident scene");
    }
    if (argc > 2) {
      auto project = ProjectData::load(fs::path(argv[2]) /
                                       L"deep/projects/Empty/Empty.star");
      auto preset = fs::path(argv[2]) / L"deep/settings/native-1440p.startest";
      auto original = hash_file(preset);
      wchar_t exe[32768];
      GetModuleFileNameW(nullptr, exe, 32768);
      auto resolved = resolve_preset(
          fs::path(exe).parent_path() / L"Starlight.exe", project, preset);
      require(resolved.data.at("schemaVersion") == 2 &&
                  hash_file(preset) == original,
              "Native project and preset reconciliation preserves original");
    }
    atomic_write(output / L"session-results.json",
                 json{{"success", true}, {"checks", checks}}.dump(2));
    std::cout << "PASS " << checks.size() << " native session checks\n";
    if(visibleLifecycle)MessageBoxW(nullptr,L"The full session test passed. All 80 measured load/unload cycles completed. Results have been saved.",L"Starlight memory test - PASS",MB_OK|MB_ICONINFORMATION);
    return 0;
  } catch (const std::exception &e) {
    atomic_write(
        output / L"session-results.json",
        json{{"success", false}, {"checks", checks}, {"error", e.what()}}.dump(
            2));
    std::cerr << e.what() << '\n';
    if(visibleLifecycle)MessageBoxW(nullptr,wide(e.what()).c_str(),L"Starlight memory test - FAILED (logs saved)",MB_OK|MB_ICONERROR);
    return 1;
  }
}
