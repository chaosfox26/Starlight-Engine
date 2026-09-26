// test_net.cpp — console proof the net module parses the slice-room protocol.
//
// Default: parse embedded MOCK welcome + one snapshot per encounter phase (intro,
// attack, combat, boss, cleared) covering every entity type, boss.telegraph=true,
// a fire with out=true, and crowd dancers + patrons. Prints parsed counts + fields.
//
// Live mode:  test_net.exe <host> <port> [path]
//   connects to a running ws:// slice server, prints parsed counts from the first
//   welcome + a few snapshots, then exits. Falls back to nothing if unreachable.
#include <cstdio>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include "json.hpp"
#include "world_state.h"
#include "ws_client.h"

using nlohmann::json;

static int g_fail = 0;
static void check(bool cond, const char* what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++g_fail;
}

// ---- one mock snapshot per phase (mirrors client/room/game.js startMock) ----
static std::string mockWelcome() {
    return R"JSON({
      "t":"welcome","id":"me","world":{"half":98},"tick":30,
      "consts":{"WALK":5.5,"SPRINT":9.5,"JUMP_V":7.5,"DODGE_SPEED":16,"STAM_MAX":100,"DODGE_COST":33,"CHARGE_FULL":1.15},
      "npcs":[
        {"id":"n1","role":"bartender","x":-24,"z":0,"yaw":1.6,"line":"What will it be, stranger?"},
        {"id":"n2","role":"friend","x":-3,"z":4,"yaw":0,"line":"Grab a seat, show is about to start."},
        {"id":"n3","role":"ally","x":3,"z":-3,"yaw":3.1,"line":"Stick close if things go bad."}
      ],
      "crowd":[
        {"id":"cd0","x":3,"z":0,"yaw":0.2,"kind":"dancer","anim":2},
        {"id":"cd1","x":4.5,"z":1,"yaw":0.4,"kind":"dancer","anim":1},
        {"id":"cp0","x":-22.5,"z":-10.5,"yaw":0,"kind":"patron","anim":0}
      ]
    })JSON";
}

// Build a state message for a given phase. combatish phases carry shadowenemy/proj/fires/boss.
static std::string mockSnapshot(const std::string& phase, double darkness) {
    bool combatish = (phase == "attack" || phase == "combat" || phase == "boss");
    std::string boss = (phase == "boss")
        ? R"("boss":{"id":"boss","x":0,"z":-12,"yaw":0.4,"hp":620,"maxhp":800,"active":true,"dead":false,"telegraph":true,"kind":"warden"},)"
        : R"("boss":null,)";
    std::string shadowenemy = combatish
        ? R"("shadowenemy":[{"id":"c1","x":10,"z":-6,"yaw":-1.2,"hp":40,"maxhp":60,"kind":"shadowenemy","dead":false},{"id":"c2","x":11,"z":8,"yaw":0.8,"hp":55,"maxhp":60,"kind":"shadowenemy","dead":false}],)"
        : R"("shadowenemy":[],)";
    std::string proj = combatish
        ? R"("proj":[{"id":"b1","x":6,"y":1.2,"z":-3,"kind":"bolt"},{"id":"b2","x":-5,"y":1.4,"z":4,"kind":"charged"}],)"
        : R"("proj":[],)";
    // fires: f2 is out=true; f3 becomes out only in cleared.
    std::string fires = combatish
        ? std::string(R"("fires":[{"id":"f1","x":-6,"z":-8,"hp":30,"out":false},{"id":"f2","x":10,"z":-2,"hp":0,"out":true},{"id":"f3","x":2,"z":12,"hp":20,"out":)")
            + (phase == "cleared" ? "true" : "false") + "}],"
        : R"("fires":[],)";

    return std::string("{\"t\":\"state\",")
        + R"("players":[{"id":"me","name":"Chaos","x":1.2,"y":0,"z":2.4,"yaw":0.5,"pitch":0,"hp":100,"stam":90,"state":0,"anim":1,"charge":0,"look":{"species":"fox"},"lastSeq":7}],)"
        + proj
        + R"("dummies":[{"id":"d0","x":-14,"z":14,"hp":60,"maxhp":60,"dead":false},{"id":"d1","x":14,"z":14,"hp":30,"maxhp":60,"dead":false}],)"
        + shadowenemy
        + fires
        + R"("crowd":[{"id":"cd0","x":3.1,"z":0.2,"yaw":0.9,"kind":"dancer","anim":2},{"id":"cd1","x":4.6,"z":1.1,"yaw":0.5,"kind":"dancer","anim":1},{"id":"cp0","x":-22.5,"z":-10.5,"yaw":0.1,"kind":"patron","anim":0},{"id":"cl0","x":16,"z":0,"yaw":0.3,"kind":"patron","anim":1}],)"
        + boss
        + "\"encounter\":\"" + phase + "\",\"darkness\":" + std::to_string(darkness) + "}";
}

static int runMock() {
    std::printf("=== net module MOCK parse test ===\n\n");
    net::LatestState store;

    // ---- welcome ----
    {
        json m = json::parse(mockWelcome());
        net::Welcome w = net::parseWelcome(m);
        store.setWelcome(w);
        std::printf("WELCOME  id=%s  world.half=%.1f  tick=%d\n", w.myId.c_str(), w.worldHalf, w.tick);
        std::printf("  consts: WALK=%.1f SPRINT=%.1f JUMP_V=%.1f DODGE_SPEED=%.1f STAM_MAX=%.1f CHARGE_FULL=%.2f\n",
            w.consts.WALK, w.consts.SPRINT, w.consts.JUMP_V, w.consts.DODGE_SPEED, w.consts.STAM_MAX, w.consts.CHARGE_FULL);
        std::printf("  npcs=%zu  crowd=%zu\n", w.npcs.size(), w.crowd.size());
        for (const auto& n : w.npcs)
            std::printf("    npc %s role=%s line=\"%s\"\n", n.id.c_str(), n.role.c_str(), n.line.c_str());
        check(w.valid, "welcome valid");
        check(w.myId == "me", "welcome id == me");
        check(w.worldHalf == 98.0f, "welcome world.half == 98");
        check(w.npcs.size() == 3, "welcome 3 npcs");
        check(w.npcs.size() == 3 && w.npcs[0].role == "bartender", "npc[0] role == bartender");
        check(w.consts.CHARGE_FULL == 1.15f, "consts.CHARGE_FULL == 1.15");
        std::printf("\n");
    }

    // ---- one snapshot per encounter phase ----
    struct PH { const char* name; double dark; };
    PH phases[] = { {"intro",0.10}, {"attack",0.45}, {"combat",0.70}, {"boss",1.00}, {"cleared",0.15} };
    bool sawTelegraph = false, sawFireOut = false, sawDancer = false, sawPatron = false, sawCharged = false;

    for (const auto& ph : phases) {
        json m = json::parse(mockSnapshot(ph.name, ph.dark));
        net::WorldState st = net::parseSnapshot(m);
        store.set(st);

        std::printf("STATE phase=%-8s darkness=%.2f | players=%zu proj=%zu dummies=%zu shadowenemy=%zu fires=%zu crowd=%zu boss=%s\n",
            net::encounterName(st.encounter), st.darkness,
            st.players.size(), st.proj.size(), st.dummies.size(),
            st.shadowenemy.size(), st.fires.size(), st.crowd.size(),
            st.boss.present ? "present" : "null");

        check(st.encounter == net::parseEncounter(ph.name), (std::string("encounter parsed == ") + ph.name).c_str());
        check(st.valid, "snapshot valid");
        check(!st.players.empty(), "has >=1 player");

        for (const auto& f : st.fires) if (f.out) sawFireOut = true;
        for (const auto& c : st.crowd) { if (c.kind == "dancer") sawDancer = true; if (c.kind == "patron") sawPatron = true; }
        for (const auto& p : st.proj)  if (p.kind == "charged") sawCharged = true;

        if (std::string(ph.name) == "boss") {
            check(st.boss.present, "boss present in boss phase");
            check(st.boss.telegraph, "boss.telegraph == true");
            check(st.boss.kind == "warden", "boss.kind == warden");
            check(st.boss.hp == 620.0f && st.boss.maxhp == 800.0f, "boss hp 620/800");
            if (st.boss.telegraph) sawTelegraph = true;
            std::printf("    BOSS %s hp=%.0f/%.0f active=%d dead=%d telegraph=%d\n",
                st.boss.id.c_str(), st.boss.hp, st.boss.maxhp, st.boss.active, st.boss.dead, st.boss.telegraph);
        } else {
            check(!st.boss.present, "boss null outside boss phase");
        }
    }
    std::printf("\n");

    // ---- verify the render-facing getters + cross-cutting coverage ----
    net::WorldState latest = store.getLatest();
    std::printf("getLatest(): encounter=%s seq=%llu players=%zu (thread-safe copy)\n",
        net::encounterName(latest.encounter), (unsigned long long)latest.seq, latest.players.size());
    check(latest.encounter == net::Encounter::Cleared, "latest snapshot is 'cleared' phase");
    check(latest.seq == 5, "seq counted 5 snapshots");

    check(sawTelegraph, "covered boss.telegraph=true");
    check(sawFireOut,   "covered a fire with out=true");
    check(sawDancer,    "covered crowd kind=dancer");
    check(sawPatron,    "covered crowd kind=patron");
    check(sawCharged,   "covered proj kind=charged");

    std::printf("\n=== %s (%d failures) ===\n", g_fail == 0 ? "ALL CHECKS PASSED" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}

static int runLive(const std::string& host, uint16_t port, const std::string& path) {
    std::printf("=== net module LIVE test -> ws://%s:%u%s ===\n", host.c_str(), (unsigned)port, path.c_str());
    net::LatestState store;
    net::WsClient ws;
    std::atomic<int> welcomes{0}, snaps{0};

    ws.onLog([](const std::string& s){ std::printf("[ws] %s\n", s.c_str()); });
    ws.onMessage([&](const std::string& text){
        json m = json::parse(text, nullptr, false);
        if (m.is_discarded()) { std::printf("[ws] non-JSON frame (%zu bytes)\n", text.size()); return; }
        std::string t = m.value("t", "");
        if (t == "welcome") {
            net::Welcome w = net::parseWelcome(m);
            store.setWelcome(w);
            std::printf("WELCOME id=%s half=%.1f npcs=%zu crowd=%zu\n", w.myId.c_str(), w.worldHalf, w.npcs.size(), w.crowd.size());
            ++welcomes;
        } else if (t == "state") {
            net::WorldState st = net::parseSnapshot(m);
            store.set(st);
            if (++snaps <= 3)
                std::printf("STATE #%d phase=%s players=%zu proj=%zu shadowenemy=%zu fires=%zu crowd=%zu boss=%s\n",
                    snaps.load(), net::encounterName(st.encounter),
                    st.players.size(), st.proj.size(), st.shadowenemy.size(),
                    st.fires.size(), st.crowd.size(), st.boss.present ? "present" : "null");
        }
    });

    if (!ws.connect(host, port, path)) { std::printf("connect failed\n"); return 2; }
    // this slice server may require a JOIN before it streams; send a harmless one.
    ws.sendText(R"({"t":"join","name":"NetTest"})");

    for (int i = 0; i < 50 && snaps.load() < 5; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ws.close();
    std::printf("live: welcomes=%d snapshots=%d\n", welcomes.load(), snaps.load());
    return (welcomes.load() > 0 || snaps.load() > 0) ? 0 : 3;
}

int main(int argc, char** argv) {
    if (argc >= 3) {
        std::string host = argv[1];
        uint16_t port = (uint16_t)std::atoi(argv[2]);
        std::string path = (argc >= 4) ? argv[3] : "/";
        return runLive(host, port, path);
    }
    return runMock();
}
