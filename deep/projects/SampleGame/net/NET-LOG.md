# net — standalone WebSocket network-client module (DX12 client)

A reusable, self-contained C++ module that connects to the Starlight slice-room
server over **ws:// WebSocket**, receives its JSON messages, and parses them into
plain C++ structs the DX12 render loop can poll each frame. Built and verified
independently of `main.cpp` / `build.bat` (NEW files only, no collision).

## Files
| File | Purpose |
|------|---------|
| `json.hpp` | Vendored **nlohmann/json v3.11.3** single-header parser (MIT). |
| `world_state.h` | Protocol structs (`Player, Proj, Dummy, ShadowEnemy, Fire, Boss, CrowdNpc, Npc, Welcome, WorldState`) + `Encounter` enum + `parseWelcome()` / `parseSnapshot()` + thread-safe `LatestState` (mutex double-copy). |
| `ws_client.h` / `ws_client.cpp` | Minimal RFC6455 WebSocket client over Winsock2: HTTP Upgrade handshake with `Sec-WebSocket-Key` (self-contained SHA-1 + base64, verifies `Sec-WebSocket-Accept`), background read thread, unmasks/reassembles server TEXT frames, answers PING→PONG and CLOSE, can send client-masked TEXT/CLOSE. |
| `test_net.cpp` | Console test: parses embedded MOCK welcome + one snapshot per encounter phase, OR connects to a live server (`test_net.exe <host> <port> [path]`). |
| `build-net.bat` | VS 2026 build (vswhere → vcvars64, same pattern as `build.bat`), links `ws2_32.lib`. |

## Protocol mirrored
Source of truth: `client/room/game.js` (`onWelcome` / `onState` + the `?mock=1`
replay loop) and `client/shared/protocol.js`.

- **WELCOME** `{t:"welcome", id, world:{half}, tick, consts:{WALK,SPRINT,JUMP_V,DODGE_SPEED,STAM_MAX,DODGE_COST,CHARGE_FULL}, npcs:[{id,role,x,z,yaw,line}], crowd:[...]}` — role = `bartender|friend|ally`.
- **STATE** `{t:"state", players:[{id,name,x,y,z,yaw,pitch,hp,stam,state,anim,charge,look:{species},lastSeq}], proj:[{id,x,y,z,kind}], dummies:[{id,x,z,hp,maxhp,dead}], shadowenemy:[{id,x,z,yaw,hp,maxhp,kind,dead}], fires:[{id,x,z,hp,out}], boss:{id,x,z,yaw,hp,maxhp,active,dead,telegraph,kind}|null, crowd:[{id,x,z,yaw,kind,anim}], encounter:"intro|attack|combat|boss|cleared", darkness:0..1}`.

`boss` is `null` outside the boss phase → represented as `Boss{present=false}`.
Parsers tolerate missing/null fields.

## Build & run
```bat
cd native-dx12\net
build-net.bat
bin\test_net.exe                       REM mock parse test (default)
bin\test_net.exe 127.0.0.1 8090 /      REM live: connect to a running ws:// server
```

## Verification (done)
- **Compiles with Visual Studio 2026** (v18 Community, `cl /std:c++17 /W3 /O2`), links `ws2_32.lib`. Clean build, no warnings surfaced at /W3.
- **Test result: ALL CHECKS PASSED (0 failures), exit code 0.** 40 assertions.
- Coverage: welcome (3 npcs bartender/friend/ally, consts, crowd) + every encounter
  phase (intro/attack/combat/boss/cleared); every entity type (players, proj incl.
  `kind=charged`, dummies, shadowenemy, fires incl. `out=true`, boss, crowd dancers +
  patrons); `boss.telegraph=true` and `boss:null` both exercised;
  `LatestState.getLatest()` returns the newest snapshot with a monotonic `seq`.

Parsed counts from the mock run (per state message): players=1, dummies=2,
crowd=4; combatish phases add proj=2, shadowenemy=2, fires=3; boss phase adds boss.

## Integration sketch (for later)
```cpp
net::LatestState g_state;
net::WsClient    ws;
ws.onMessage([&](const std::string& t){
    auto m = nlohmann::json::parse(t, nullptr, false);
    if (m.is_discarded()) return;
    auto ty = m.value("t","");
    if (ty=="welcome") g_state.setWelcome(net::parseWelcome(m));
    else if (ty=="state") g_state.set(net::parseSnapshot(m));
});
ws.connect("127.0.0.1", 8090, "/");
// render thread, once per frame:
net::WorldState snap = g_state.getLatest();   // thread-safe copy
```

## Honest gaps / scope notes
- **Receive-focused.** It fully receives/parses server frames and can send masked
  TEXT (e.g. a `join`) + PONG/CLOSE, but there's no reconnect/backoff, ping timer,
  or send queue depth control — add those before shipping.
- **No TLS (wss://).** Intentional: the slice server is local `ws://`. A `wss://`
  cutover to the VPS would need a TLS layer (schannel/OpenSSL) under the framing.
- **Thread-safety = mutex copy**, not a lock-free double-buffer. Payload is small
  (tens of entities) so the per-frame copy is cheap; revisit only if profiling flags it.
- **UTF-8 validation** on inbound text is not enforced; the JSON parser rejects
  malformed input but the frame layer trusts the server is well-behaved.
- The live-mode `join` payload is a best-effort guess; the exact handshake a given
  slice server expects (`slice-server.mjs` vs. the room server) may differ — mock
  path is the authoritative proof here.
- SHA-1/base64 are minimal in-file implementations (handshake only), not
  general-purpose crypto.
