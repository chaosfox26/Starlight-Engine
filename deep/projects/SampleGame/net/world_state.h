// world_state.h — C++ structs mirroring the Starlight slice-room wire protocol
// (JSON over WebSocket), plus parseWelcome / parseSnapshot and a thread-safe
// double-buffered latest-snapshot store the DX12 render loop can poll each frame.
//
// Wire schema source of truth: server/vps-slice/server.mjs (the canonical VPS slice server:
// `welcome` incl. bar + catalog, `state` snapshots, normalizeLook gear/tattoo), cross-checked
// against client/room/game.js (onWelcome / onState + the ?mock=1 replay loop).
//
// Standalone module — NEW files only under native-dx12/net/. Does not touch main.cpp.
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include "json.hpp"

namespace net {

// ---- welcome: static world actors ----------------------------------------
struct Npc {
    std::string id;
    std::string role;   // "bartender" | "friend" | "ally"
    float x = 0, z = 0, yaw = 0;
    std::string line;   // spoken line
};

// ---- snapshot entities ----------------------------------------------------
// players[].look.gear.<slot> — CONTRACT-3D §3 v1.2: each slot is null | {piece, color, show}.
// A slot is only listed here when it is non-null on the wire. `color` is a colorway name
// ("magenta", "blue", "white", ...) or "#rrggbb", or empty when the server sent null.
// RENDER RULE: draw a slot ONLY when show == true (hidden gear still counts for stats —
// that is the server's concern, not the client's).
struct GearSlot {
    std::string slot;       // "head" | "shoulders" | "chest" | "gloves" | "pants" | "hip" | "feet" | "weapon" | ...
    std::string piece;      // opaque piece id
    std::string color;      // colorway | "#rrggbb" | "" (null)
    bool show = false;
};

struct Tattoo {             // look.tattoo {on, design, color} — design 0 = heart; glowing (emissive)
    bool on = false;
    int  design = 0;
    std::string color = "magenta";
    bool present = false;
};

struct LookState {          // players[].look — appearance descriptor (server-normalized)
    std::string species;    // e.g. "fox"
    std::string sex;        // "f" | "m"
    std::vector<GearSlot> gear;   // non-null slots only, in wire order
    Tattoo tattoo;
    bool present = false;
};

struct Player {
    std::string id;
    std::string name;
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
    float hp = 0, stam = 0;
    int   state = 0;        // movement state enum (server-side)
    int   anim = 0;         // ANIM enum
    float charge = 0;       // charged-shot progress
    LookState look;
    uint32_t lastSeq = 0;   // last input seq the server has applied
};

struct Proj {
    std::string id;
    float x = 0, y = 0, z = 0;
    std::string kind;       // "bolt" | "charged" | ...
};

struct Dummy {
    std::string id;
    float x = 0, z = 0;
    float hp = 0, maxhp = 0;
    bool  dead = false;
};

struct ShadowEnemy {             // "shadowenemy" shades
    std::string id;
    float x = 0, z = 0, yaw = 0;
    float hp = 0, maxhp = 0;
    std::string kind;       // "shadowenemy"
    bool  dead = false;
};

struct Fire {
    std::string id;
    float x = 0, z = 0;
    float hp = 0;
    bool  out = false;      // extinguished
};

struct Boss {
    std::string id;
    float x = 0, z = 0, yaw = 0;
    float hp = 0, maxhp = 0;
    bool  active = false;
    bool  dead = false;
    bool  telegraph = false; // AoE wind-up active
    std::string kind;        // "warden"
    bool  present = false;   // boss:null on the wire when not in the boss phase
};

struct CrowdNpc {           // ambient club crowd (dancers + patrons)
    std::string id;
    float x = 0, z = 0, yaw = 0;
    std::string kind;       // "dancer" | "patron"
    int   anim = 0;         // 0 idle | 1 walk | 2 dance
};

// ---- encounter phase enum -------------------------------------------------
enum class Encounter { Intro, Attack, Combat, Boss, Cleared, Unknown };

inline Encounter parseEncounter(const std::string& s) {
    if (s == "intro")   return Encounter::Intro;
    if (s == "attack")  return Encounter::Attack;
    if (s == "combat")  return Encounter::Combat;
    if (s == "boss")    return Encounter::Boss;
    if (s == "cleared") return Encounter::Cleared;
    return Encounter::Unknown;
}
inline const char* encounterName(Encounter e) {
    switch (e) {
        case Encounter::Intro:   return "intro";
        case Encounter::Attack:  return "attack";
        case Encounter::Combat:  return "combat";
        case Encounter::Boss:    return "boss";
        case Encounter::Cleared: return "cleared";
        default:                 return "unknown";
    }
}

// ---- welcome payload ------------------------------------------------------
struct WorldConsts {        // welcome.consts (movement/combat tuning) — kept generic
    float WALK = 0, SPRINT = 0, JUMP_V = 0, DODGE_SPEED = 0;
    float STAM_MAX = 0, DODGE_COST = 0, CHARGE_FULL = 0;
};

struct TattooDesign {       // welcome.catalog.tattooDesigns[] — creator catalog (server = single source of truth)
    int id = 0;
    std::string name, label, defaultColor, lore;
};

struct Welcome {
    std::string myId;       // welcome.id (this client's player id)
    float worldHalf = 0;    // welcome.world.half
    int   tick = 0;         // authoritative tick hz
    WorldConsts consts;
    std::vector<Npc>      npcs;
    std::vector<CrowdNpc> crowd;   // welcome may seed the ambient crowd too
    float barX = 0, barZ = 0;      // welcome.bar {x,z} — the bar counter (first room)
    bool  barValid = false;
    std::vector<TattooDesign> tattooDesigns;   // welcome.catalog.tattooDesigns[]
    Encounter encounter = Encounter::Unknown;  // welcome also carries the current phase + darkness
    float darkness = 0.0f;
    bool  valid = false;
};

// ---- the per-frame render-facing snapshot --------------------------------
struct WorldState {
    std::vector<Player>   players;
    std::vector<Proj>     proj;
    std::vector<Dummy>    dummies;
    std::vector<ShadowEnemy>   shadowenemy;
    std::vector<Fire>     fires;
    Boss                  boss;      // .present == false when boss:null
    std::vector<CrowdNpc> crowd;
    Encounter             encounter = Encounter::Unknown;
    float                 darkness = 0.0f; // 0..1 room-darkening
    uint64_t              seq = 0;         // local monotonically-increasing apply counter
    bool                  valid = false;
};

// ---- json helpers (tolerant of missing / null fields) ---------------------
namespace detail {
    using json = nlohmann::json;
    inline float   f(const json& o, const char* k, float d = 0.f) {
        auto it = o.find(k); return (it != o.end() && it->is_number()) ? it->get<float>() : d; }
    inline int     i(const json& o, const char* k, int d = 0) {
        auto it = o.find(k); return (it != o.end() && it->is_number()) ? it->get<int>() : d; }
    inline uint32_t u(const json& o, const char* k, uint32_t d = 0) {
        auto it = o.find(k); return (it != o.end() && it->is_number()) ? it->get<uint32_t>() : d; }
    inline bool    b(const json& o, const char* k, bool d = false) {
        auto it = o.find(k);
        if (it == o.end() || it->is_null()) return d;
        if (it->is_boolean()) return it->get<bool>();
        if (it->is_number())  return it->get<double>() != 0.0;
        return d; }
    inline std::string s(const json& o, const char* k, const char* d = "") {
        auto it = o.find(k); return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string(d); }
}

// ---- parsers --------------------------------------------------------------
inline Welcome parseWelcome(const nlohmann::json& m) {
    using namespace detail;
    Welcome w;
    w.myId = s(m, "id");
    auto wit = m.find("world");
    if (wit != m.end() && wit->is_object()) w.worldHalf = f(*wit, "half");
    w.tick = i(m, "tick");
    auto cit = m.find("consts");
    if (cit != m.end() && cit->is_object()) {
        const auto& c = *cit;
        w.consts.WALK        = f(c, "WALK");
        w.consts.SPRINT      = f(c, "SPRINT");
        w.consts.JUMP_V      = f(c, "JUMP_V");
        w.consts.DODGE_SPEED = f(c, "DODGE_SPEED");
        w.consts.STAM_MAX    = f(c, "STAM_MAX");
        w.consts.DODGE_COST  = f(c, "DODGE_COST");
        w.consts.CHARGE_FULL = f(c, "CHARGE_FULL");
    }
    auto nit = m.find("npcs");
    if (nit != m.end() && nit->is_array()) {
        for (const auto& n : *nit) {
            Npc npc;
            npc.id = s(n, "id"); npc.role = s(n, "role");
            npc.x = f(n, "x"); npc.z = f(n, "z"); npc.yaw = f(n, "yaw");
            npc.line = s(n, "line");
            w.npcs.push_back(std::move(npc));
        }
    }
    auto crit = m.find("crowd");
    if (crit != m.end() && crit->is_array()) {
        for (const auto& c : *crit) {
            CrowdNpc cn;
            cn.id = s(c, "id"); cn.kind = s(c, "kind");
            cn.x = f(c, "x"); cn.z = f(c, "z"); cn.yaw = f(c, "yaw");
            cn.anim = i(c, "anim");
            w.crowd.push_back(std::move(cn));
        }
    }
    auto bit = m.find("bar");
    if (bit != m.end() && bit->is_object()) {
        w.barX = f(*bit, "x"); w.barZ = f(*bit, "z"); w.barValid = true;
    }
    auto cat = m.find("catalog");
    if (cat != m.end() && cat->is_object()) {
        auto tit = cat->find("tattooDesigns");
        if (tit != cat->end() && tit->is_array()) {
            for (const auto& t : *tit) {
                if (!t.is_object()) continue;
                TattooDesign d;
                d.id = i(t, "id"); d.name = s(t, "name"); d.label = s(t, "label");
                d.defaultColor = s(t, "defaultColor"); d.lore = s(t, "lore");
                w.tattooDesigns.push_back(std::move(d));
            }
        }
    }
    w.encounter = parseEncounter(s(m, "encounter"));
    w.darkness  = f(m, "darkness");
    w.valid = true;
    return w;
}

// look = server-normalized appearance. gear.<slot> is null | {piece,color,show}; null slots are
// dropped (nothing to render), strings (pre-v1.2 shape) are kept with show=false so the
// "render only when show === true" rule still holds. tattoo is {on,design,color}.
inline LookState parseLook(const nlohmann::json& l) {
    using namespace detail;
    LookState look;
    if (!l.is_object()) return look;
    look.present = true;
    look.species = s(l, "species");
    look.sex     = s(l, "sex");
    auto git = l.find("gear");
    if (git != l.end() && git->is_object()) {
        for (auto it = git->begin(); it != git->end(); ++it) {
            const auto& v = it.value();
            GearSlot gs; gs.slot = it.key();
            if (v.is_object()) {
                gs.piece = s(v, "piece");
                gs.color = s(v, "color");           // null -> ""
                gs.show  = v.contains("show") && v["show"].is_boolean() && v["show"].get<bool>();
            } else if (v.is_string()) {
                gs.piece = v.get<std::string>();    // legacy bare id: no show flag -> not drawn
            } else continue;                        // null / anything else = no piece
            if (gs.piece.empty()) continue;
            look.gear.push_back(std::move(gs));
        }
    }
    auto tit = l.find("tattoo");
    if (tit != l.end() && tit->is_object()) {
        look.tattoo.present = true;
        look.tattoo.on     = b(*tit, "on");
        look.tattoo.design = i(*tit, "design");
        look.tattoo.color  = s(*tit, "color", "magenta");
    }
    return look;
}

inline WorldState parseSnapshot(const nlohmann::json& m) {
    using namespace detail;
    WorldState st;

    auto pit = m.find("players");
    if (pit != m.end() && pit->is_array()) {
        for (const auto& p : *pit) {
            Player pl;
            pl.id = s(p, "id"); pl.name = s(p, "name");
            pl.x = f(p, "x"); pl.y = f(p, "y"); pl.z = f(p, "z");
            pl.yaw = f(p, "yaw"); pl.pitch = f(p, "pitch");
            pl.hp = f(p, "hp"); pl.stam = f(p, "stam");
            pl.state = i(p, "state"); pl.anim = i(p, "anim");
            pl.charge = f(p, "charge"); pl.lastSeq = u(p, "lastSeq");
            auto lit = p.find("look");
            if (lit != p.end() && lit->is_object()) pl.look = parseLook(*lit);
            st.players.push_back(std::move(pl));
        }
    }

    auto jit = m.find("proj");
    if (jit != m.end() && jit->is_array()) {
        for (const auto& b : *jit) {
            Proj pr;
            pr.id = s(b, "id");
            pr.x = f(b, "x"); pr.y = f(b, "y"); pr.z = f(b, "z");
            pr.kind = s(b, "kind");
            st.proj.push_back(std::move(pr));
        }
    }

    auto dit = m.find("dummies");
    if (dit != m.end() && dit->is_array()) {
        for (const auto& d : *dit) {
            Dummy dm;
            dm.id = s(d, "id");
            dm.x = f(d, "x"); dm.z = f(d, "z");
            dm.hp = f(d, "hp"); dm.maxhp = f(d, "maxhp");
            dm.dead = b(d, "dead");
            st.dummies.push_back(std::move(dm));
        }
    }

    auto xit = m.find("shadowenemy");
    if (xit != m.end() && xit->is_array()) {
        for (const auto& c : *xit) {
            ShadowEnemy cx;
            cx.id = s(c, "id");
            cx.x = f(c, "x"); cx.z = f(c, "z"); cx.yaw = f(c, "yaw");
            cx.hp = f(c, "hp"); cx.maxhp = f(c, "maxhp");
            cx.kind = s(c, "kind"); cx.dead = b(c, "dead");
            st.shadowenemy.push_back(std::move(cx));
        }
    }

    auto fit = m.find("fires");
    if (fit != m.end() && fit->is_array()) {
        for (const auto& fr : *fit) {
            Fire f2;
            f2.id = s(fr, "id");
            f2.x = f(fr, "x"); f2.z = f(fr, "z");
            f2.hp = f(fr, "hp"); f2.out = b(fr, "out");
            st.fires.push_back(std::move(f2));
        }
    }

    auto bit = m.find("boss");
    if (bit != m.end() && bit->is_object()) {
        const auto& bo = *bit;
        st.boss.present   = true;
        st.boss.id        = s(bo, "id");
        st.boss.x = f(bo, "x"); st.boss.z = f(bo, "z"); st.boss.yaw = f(bo, "yaw");
        st.boss.hp = f(bo, "hp"); st.boss.maxhp = f(bo, "maxhp");
        st.boss.active    = b(bo, "active");
        st.boss.dead      = b(bo, "dead");
        st.boss.telegraph = b(bo, "telegraph");
        st.boss.kind      = s(bo, "kind");
    }

    auto cit = m.find("crowd");
    if (cit != m.end() && cit->is_array()) {
        for (const auto& c : *cit) {
            CrowdNpc cn;
            cn.id = s(c, "id"); cn.kind = s(c, "kind");
            cn.x = f(c, "x"); cn.z = f(c, "z"); cn.yaw = f(c, "yaw");
            cn.anim = i(c, "anim");
            st.crowd.push_back(std::move(cn));
        }
    }

    st.encounter = parseEncounter(s(m, "encounter"));
    st.darkness  = f(m, "darkness");
    st.valid     = true;
    return st;
}

// ---- thread-safe latest-snapshot store -----------------------------------
// The network thread calls set(); the render thread calls getLatest() once per
// frame. Mutex-guarded copy: simple, correct, and the payload is small.
class LatestState {
public:
    void setWelcome(const Welcome& w) {
        std::lock_guard<std::mutex> lk(mu_);
        welcome_ = w;
    }
    Welcome getWelcome() const {
        std::lock_guard<std::mutex> lk(mu_);
        return welcome_;
    }
    void set(const WorldState& s) {
        std::lock_guard<std::mutex> lk(mu_);
        state_ = s;
        state_.seq = ++seq_;
    }
    WorldState getLatest() const {
        std::lock_guard<std::mutex> lk(mu_);
        return state_;
    }
    uint64_t seq() const {
        std::lock_guard<std::mutex> lk(mu_);
        return seq_;
    }
private:
    mutable std::mutex mu_;
    Welcome    welcome_;
    WorldState state_;
    uint64_t   seq_ = 0;
};

} // namespace net
