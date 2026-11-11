// ================================================================================
// Weapon icon resolution for the ESP.
//
// Icon font: includes/fonts/esp/weapon.h ("furry_coder-Weapon-Font"), one
// glyph per weapon on the characters 0-9, A-O, a-z (legend at the bottom).
//
// Weapon source (established from bycmd.log, build 10885a5, 2026-11-11):
//   player (odq) -> wwv.bvvg (nzk) -> kevw (cew[]) -> component of class uyg
//   (WeaponPart, TypeInfo 0xCD34CE0). Raw dumps of five enemies:
//     +0x30: 0x18 0x19 0x16 0x10 0x0b   differs per player, mirrored at +0x36
//     +0x18: 1 while alive, 0 for the dead player whose +0x30 was also 0
//   so uyg+0x30 (byte) is the weapon id. No weapon *name* string exists near
//   the component (the previous string scan only found class/culture names),
//   so the id -> glyph table below has to be filled from observation:
//   the local player's id is logged on every change ("[WEAPON] local weapon
//   id=N") and shown in the menu; until an id has a glyph the ESP draws "#N".
//
// Cost: one 8-byte read per enemy per 250 ms plus one parts-array walk per
// 2 s. The earlier depth-3 string scan is gone (it stalled a frame for 7 s).
// ================================================================================

#pragma once

#include "game.hpp"
#include <unordered_map>

namespace weapon {
    // ---- uyg layout (observed, see header) ----
    inline constexpr uint64_t uyg_active = 0x18;      // 1 = weapon shown, 0 = none/dead (hypothesis, consistent with dumps)
    inline constexpr uint64_t uyg_weapon_id = 0x30;   // byte, observed
    inline constexpr uint64_t uyg_weapon_id_mirror = 0x36; // same byte again, used as a consistency check
    inline constexpr int k_max_id = 127;

    struct Info {
        int id{0};          // raw uyg+0x30
        char glyph{0};      // 0 = id known but no glyph assigned yet
        const char* name{nullptr};
        bool valid{false};  // id read and consistent
    };

    // ---- id -> glyph. FILL FROM THE LOG. ----
    // Procedure: run bycmd-debug.sh, buy / pick up a weapon, read
    // "[WEAPON] local weapon id=N" (or the "my weapon: #N" line in the menu),
    // add {N, '<glyph>', "<name>"} here, rebuild. Nothing below is verified yet.
    struct id_entry { int id; char glyph; const char* name; };
    inline constexpr id_entry k_id_table[] = {
        // {24, 's', "AKR"},     // example only, not verified
        {0, 0, nullptr},
    };

    inline const id_entry* lookup(int id) {
        for (const id_entry& e : k_id_table)
            if (e.id == id && e.glyph) return &e;
        return nullptr;
    }

    struct cache_entry {
        uint64_t part{0};
        long long part_ms{0};
        long long read_ms{0};
        long long dump_ms{0};
        Info info{};
    };
    inline std::unordered_map<uint64_t, cache_entry> g_cache;

    // WeaponPart (uyg) of a player: nzk parts array, exact class match.
    inline uint64_t find_part(uint64_t player) {
        const uint64_t part_class = typeinfo(v100::weapon_part_typeinfo);
        if (!part_class) return 0;
        const uint64_t container = rpm<uint64_t>(player + v100::player_parts);
        const uint64_t array = rpm<uint64_t>(container + v100::parts_array);
        const uint64_t length = rpm<uint64_t>(array + v100::array_length);
        if (!obj_ptr(container) || !obj_ptr(array) || length == 0 || length > 64) return 0;
        for (uint64_t i = 0; i < length; ++i) {
            const uint64_t part = rpm<uint64_t>(array + v100::array_data + i * 8);
            if (obj_ptr(part) && exact_class(part, part_class)) return part;
        }
        return 0;
    }

    inline Info resolve(const player_snapshot& p, bool is_local = false) {
        cache_entry& c = g_cache[p.object];
        const long long now = diag::now_ms();
        if (now - c.read_ms < 250) return c.info;
        c.read_ms = now;

        if (!c.part || now - c.part_ms > 2000 || !exact_class(c.part, typeinfo(v100::weapon_part_typeinfo))) {
            c.part = find_part(p.object);
            c.part_ms = now;
        }
        Info next{};
        if (c.part) {
            uint8_t block[8]{};
            if (mem_read(c.part + uyg_weapon_id, block, sizeof(block))) {
                const int id = block[0];
                const int mirror = block[uyg_weapon_id_mirror - uyg_weapon_id];
                if (id > 0 && id <= k_max_id && id == mirror) {
                    next.id = id;
                    next.valid = true;
                    if (const id_entry* e = lookup(id)) { next.glyph = e->glyph; next.name = e->name; }
                }
            }
            if (diag::every(c.dump_ms, 60000)) {
                uint64_t q[16]{};
                if (mem_read(c.part, q, sizeof(q))) {
                    char line[16 * 19 + 1];
                    int used = 0;
                    for (int i = 0; i < 16 && used < int(sizeof(line)) - 20; ++i)
                        used += std::snprintf(line + used, sizeof(line) - size_t(used), "%s%llx", i ? " " : "", (unsigned long long)q[i]);
                    diag::log("WEAPON", "uyg dump %s object=0x%llx part=0x%llx: %s", is_local ? "LOCAL" : "enemy",
                              (unsigned long long)p.object, (unsigned long long)c.part, line);
                }
            }
        }
        if (next.id != c.info.id || next.valid != c.info.valid) {
            if (is_local)
                diag::log("WEAPON", "local weapon id=%d%s (uyg+0x30, part=0x%llx)", next.id,
                          next.valid ? "" : " (none)", (unsigned long long)c.part);
            else
                diag::log("WEAPON", "object=0x%llx weapon id=%d%s glyph=%s", (unsigned long long)p.object, next.id,
                          next.valid ? "" : " (none)", next.glyph ? next.name : "unassigned");
        }
        c.info = next;
        return c.info;
    }

    // Last local-player id for the menu status line (0 = none).
    inline int g_local_id = 0;

    // ---- glyph legend (identified visually from the font; (?) = by shape only) ----
    // knives:  1 M9 Bayonet, 2 Butterfly, 3 Dual Daggers, 4 Flip(?), 5 Scorpion(?),
    //          6 Stiletto(?), 7 Tanto(?), 8 Karambit, 9 Jkommando(?), J Kunai,
    //          K Fang(?), L Standoff knife(?), M Stiletto-type(?), N Dagger(?), O Katana
    // utility: D C4, E HE grenade, F Flashbang, G Molotov, H Incendiary, I Smoke
    // pistols: a G22, b USP, c P350, d Dual Berettas, e Tec-9, f Five-SeveN, g Desert Eagle
    // SMG:     h MAC-10, i UMP45, j MP5, k MP7, l Dual UZI(?), m P90
    // rifles:  n FAL-type(?), o FAMAS, p AKR-12, q M4, r M16, s AKR, t SCAR-type(?),
    //          u scoped M4(?), v rifle(?), w LMG(?)
    // other:   x SPAS-12, A M40, B SR-25, C AWM, y DMR(?), z scoped rifle(?)
    // 0 = "no icon" placeholder glyph.
}
