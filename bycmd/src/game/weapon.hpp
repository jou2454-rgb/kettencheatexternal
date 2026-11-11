// ================================================================================
// Weapon icon resolution for the ESP (dump x64 1.1.0 verified).
//
// Chain (dump.cs):
//   odq -> wwv.bvvg (nzk) -> kevw (cew[]) -> uyg (WeaponPart, TypeInfo 0xCD34CE0)
//   uyg+0x30  WeaponContext kbdx (inline struct, 0xA8 bytes)
//     +0x00 NetObjectId Owner   (this is the byte the earlier builds misread as an id)
//     +0x10 byte Team
//     +0x50 jrx ActiveWeapon    => uyg+0x80
//   jrx+0x10  WeaponId <oqrg>   (struct { byte Raw })
//   jrx+0x18  WeaponMetaData    (skin / charm / stickers only, no name)
//
// Raw -> weapon: the named constants live in static class WeaponIds
// (Axlebolt.Standoff.Gameplay.Features.Weaponry.Data, TypeDefIndex 20273) and
// are assigned in its .cctor (RVA 0x9CADAE0). Their values are not in dump.cs,
// so they are read from the running game once: the .cctor prologue loads the
// class pointer through an ADRP+LDR pair, the pointed Il2CppClass is checked
// by name ("WeaponIds", Il2CppClass+0x10) and its static_fields block
// (Il2CppClass+0xB8) holds one byte per constant in declaration order:
//   0x00..0x11 event weapons, 0x12 None, 0x13 G22 ... 0x3B VestAndHelmet.
// Until that table is read and validated no icon is drawn.
//
// Icon font: includes/fonts/esp/weapon.h, one glyph per weapon (legend below).
// ================================================================================

#pragma once

#include "game.hpp"
#include <unordered_map>
#include <cstring>
#include <cstdio>

namespace weapon {
    // ---- offsets (dump.cs 1.1.0) ----
    inline constexpr uint64_t uyg_context = 0x30;
    inline constexpr uint64_t ctx_owner = 0x00;
    inline constexpr uint64_t ctx_team = 0x10;
    inline constexpr uint64_t ctx_active_weapon = 0x50;
    inline constexpr uint64_t uyg_active_weapon = uyg_context + ctx_active_weapon; // 0x80
    inline constexpr uint64_t jrx_weapon_id = 0x10;
    inline constexpr uint64_t weapon_ids_cctor_rva = 0x9CADAE0;
    inline constexpr uint64_t il2cpp_class_name = 0x10;
    inline constexpr uint64_t il2cpp_class_static_fields = 0xB8;

    // Declaration order of WeaponIds static fields (offset == index).
    struct named { const char* name; char glyph; };
    inline constexpr named k_weapon_ids[] = {
        {"ExplosiveMine", 0}, {"PlayerTurret", 0}, {"Explosion", 0}, {"StunMine", 0},
        {"ReturnMark", 0}, {"Electricity", 0}, {"Fire", 0}, {"TeleportGrenade", 'E'},
        {"ClownGrenade", 'E'}, {"BuffGrenade", 'E'}, {"StunMineGrenade", 'E'}, {"Cleaver", '7'},
        {"DeagleClown", 'g'}, {"EventSPAS", 'x'}, {"GhostHands", 0}, {"PudgeExplosion", 0},
        {"GhostCloneExplosion", 0}, {"Chandelier", 0},
        {"None", 0},            // 0x12
        {"G22", 'a'}, {"USP", 'b'}, {"P350", 'c'}, {"Deagle", 'g'}, {"Tec9", 'e'},
        {"FiveSeven", 'f'}, {"Berettas", 'd'}, {"UMP45", 'i'}, {"AkimboUzi", 'l'},
        {"MP7", 'k'}, {"P90", 'm'}, {"MP5", 'j'}, {"MAC10", 'h'}, {"VAL", 'v'},
        {"M4A1", 'u'}, {"AKR", 's'}, {"AKR12", 'p'}, {"M4", 'q'}, {"M16", 'r'},
        {"FAMAS", 'o'}, {"FnFal", 'n'}, {"AWM", 'C'}, {"M40", 'A'}, {"M110", 'B'},
        {"Mallard", 't'}, {"SM1014", 'w'}, {"FabM", 'z'}, {"M60", 'y'}, {"SPAS", 'x'},
        {"Knife", 'L'}, {"Hands", 0}, {"GrenadeHE", 'E'}, {"GrenadeSmoke", 'I'},
        {"GrenadeFlash", 'F'}, {"GrenadeMolotov", 'G'}, {"GrenadeIncendiary", 'H'},
        {"Bomb", 'D'}, {"Defuser", 0}, {"DefuseKit", 0}, {"Vest", 0}, {"VestAndHelmet", 0},
    };
    inline constexpr int k_weapon_ids_count = int(sizeof(k_weapon_ids) / sizeof(k_weapon_ids[0])); // 0x3C
    static_assert(k_weapon_ids_count == 0x3C, "WeaponIds declaration list must match dump.cs");
    inline constexpr int k_first_builtin = 0x13; // G22

    struct Info {
        int raw{-1};          // WeaponId.Raw, -1 = no active weapon
        char glyph{0};        // 0 = no icon for this weapon / table not ready
        const char* name{nullptr};
        bool valid{false};    // active weapon object read
    };

    // ---- raw -> name/glyph table, read from the game ----
    struct table_state {
        bool ready{false};
        int pid{-1};
        uint64_t klass{0};
        uint8_t raw[0x3C]{};
        const named* by_raw[256]{};
        long long last_try_ms{0};
        int tries{0};
    };
    inline table_state g_table;

    inline bool read_cstring(uint64_t address, char* out, size_t cap) {
        if (!plausible(address) || address < 0x10000) return false;
        if (!mem_read(address, out, cap - 1)) return false;
        out[cap - 1] = 0;
        for (size_t i = 0; i < cap - 1 && out[i]; ++i)
            if ((unsigned char)out[i] < 0x20 || (unsigned char)out[i] > 0x7E) return false;
        return out[0] != 0;
    }

    inline bool class_named(uint64_t klass, const char* expected) {
        if (!plausible(klass) || klass < 0x10000) return false;
        char name[32]{};
        return read_cstring(rpm<uint64_t>(klass + il2cpp_class_name), name, sizeof(name)) &&
               std::strcmp(name, expected) == 0;
    }

    inline bool load_table() {
        if (g_table.ready && g_table.pid == proc::pid) return true;
        const long long now = diag::now_ms();
        if (now - g_table.last_try_ms < 5000) return false;
        g_table.last_try_ms = now;
        ++g_table.tries;
        g_table.ready = false;
        g_table.pid = proc::pid;
        if (!proc::lib) return false;

        // 1. ADRP/ADD/LDR walk over the .cctor prologue: every loaded 64-bit
        //    slot is a candidate TypeInfo pointer; the Il2CppClass name decides.
        uint32_t code[96]{};
        const uint64_t cctor = proc::lib + weapon_ids_cctor_rva;
        if (!mem_read(cctor, code, sizeof(code))) {
            diag::log("WEAPON", "WeaponIds .cctor unreadable at 0x%llx", (unsigned long long)cctor);
            return false;
        }
        uint64_t regs[32]{};
        bool known[32]{};
        uint64_t klass = 0;
        for (int i = 0; i < 96 && !klass; ++i) {
            const uint32_t insn = code[i];
            const uint64_t pc = cctor + uint64_t(i) * 4;
            if ((insn & 0x9F000000u) == 0x90000000u) {                   // ADRP Xd, page
                const uint64_t immlo = (insn >> 29) & 3, immhi = (insn >> 5) & 0x7FFFF;
                int64_t imm = int64_t((immhi << 2) | immlo);
                if (imm & (1 << 20)) imm -= (1 << 21);
                regs[insn & 31] = (pc & ~0xFFFULL) + uint64_t(imm << 12);
                known[insn & 31] = true;
            } else if ((insn & 0xFF800000u) == 0x91000000u) {            // ADD Xd, Xn, #imm12
                const int rn = (insn >> 5) & 31, rd = insn & 31;
                if (known[rn]) { regs[rd] = regs[rn] + ((insn >> 10) & 0xFFF); known[rd] = true; }
                else known[rd] = false;
            } else if ((insn & 0xFFC00000u) == 0xF9400000u) {            // LDR Xt, [Xn, #imm12*8]
                const int rn = (insn >> 5) & 31, rt = insn & 31;
                if (known[rn]) {
                    const uint64_t slot = regs[rn] + ((insn >> 10) & 0xFFF) * 8;
                    const uint64_t value = rpm<uint64_t>(slot);
                    if (class_named(value, "WeaponIds")) { klass = value; break; }
                    regs[rt] = value; known[rt] = plausible(value);
                } else known[rt] = false;
            } else if ((insn & 0xFC000000u) == 0x94000000u) {            // BL: stop at the first call after a miss
                continue;
            } else if (insn == 0xD65F03C0u) {                            // RET
                break;
            }
        }
        if (!klass) {
            diag::log("WEAPON", "WeaponIds class not found from .cctor (try %d); first words %08x %08x %08x %08x",
                      g_table.tries, code[0], code[1], code[2], code[3]);
            return false;
        }
        // 2. static_fields block: one byte per constant.
        const uint64_t statics = rpm<uint64_t>(klass + il2cpp_class_static_fields);
        uint8_t raw[0x3C]{};
        if (!plausible(statics) || !mem_read(statics, raw, sizeof(raw))) {
            diag::log("WEAPON", "WeaponIds statics unreadable klass=0x%llx statics=0x%llx",
                      (unsigned long long)klass, (unsigned long long)statics);
            return false;
        }
        // 3. validation: the built-in constants must be pairwise distinct and
        //    not all zero (an un-run .cctor leaves zeros).
        bool seen[256]{};
        int nonzero = 0;
        for (int i = k_first_builtin; i < k_weapon_ids_count; ++i) {
            if (seen[raw[i]]) {
                diag::log("WEAPON", "WeaponIds statics rejected: duplicate raw %d (%s)", raw[i], k_weapon_ids[i].name);
                return false;
            }
            seen[raw[i]] = true;
            if (raw[i]) ++nonzero;
        }
        if (nonzero < 20) {
            diag::log("WEAPON", "WeaponIds statics rejected: %d non-zero values (class not initialised yet?)", nonzero);
            return false;
        }
        std::memset(g_table.by_raw, 0, sizeof(g_table.by_raw));
        std::memcpy(g_table.raw, raw, sizeof(raw));
        for (int i = k_weapon_ids_count - 1; i >= 0; --i)      // built-ins override event ids on collision
            if (i >= k_first_builtin || !g_table.by_raw[raw[i]]) g_table.by_raw[raw[i]] = &k_weapon_ids[i];
        g_table.by_raw[raw[0x12]] = &k_weapon_ids[0x12];      // None
        g_table.klass = klass;
        g_table.ready = true;

        char line[1024]; int used = 0;
        for (int i = k_first_builtin; i < k_weapon_ids_count && used < int(sizeof(line)) - 32; ++i)
            used += std::snprintf(line + used, sizeof(line) - size_t(used), "%s%s=%d", i > k_first_builtin ? " " : "", k_weapon_ids[i].name, raw[i]);
        diag::log("WEAPON", "WeaponIds table ready klass=0x%llx statics=0x%llx None=%d: %s",
                  (unsigned long long)klass, (unsigned long long)statics, raw[0x12], line);
        return true;
    }

    // ---- per-player resolution ----
    struct cache_entry {
        uint64_t part{0};
        long long part_ms{0};
        long long read_ms{0};
        Info info{};
    };
    inline std::unordered_map<uint64_t, cache_entry> g_cache;

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

        const bool table = load_table();
        if (!c.part || now - c.part_ms > 2000 || !exact_class(c.part, typeinfo(v100::weapon_part_typeinfo))) {
            c.part = find_part(p.object);
            c.part_ms = now;
        }
        Info next{};
        if (c.part) {
            const uint64_t active = rpm<uint64_t>(c.part + uyg_active_weapon);
            if (obj_ptr(active)) {
                const int raw = rpm<uint8_t>(active + jrx_weapon_id);
                next.raw = raw;
                next.valid = true;
                if (table && g_table.by_raw[raw]) {
                    next.name = g_table.by_raw[raw]->name;
                    next.glyph = g_table.by_raw[raw]->glyph;
                }
            }
        }
        if (next.raw != c.info.raw || next.glyph != c.info.glyph) {
            diag::log("WEAPON", "%s object=0x%llx raw=%d weapon=%s glyph=%c",
                      is_local ? "LOCAL" : "enemy", (unsigned long long)p.object, next.raw,
                      next.name ? next.name : (next.valid ? (table ? "unknown-raw" : "table-not-ready") : "none"),
                      next.glyph ? next.glyph : '-');
        }
        c.info = next;
        return c.info;
    }

    inline int g_local_id = 0;                 // menu status line (raw)
    inline const char* g_local_name = nullptr; // menu status line

    // ---- glyph legend (font identified visually; (?) = by shape only) ----
    // knives:  1 M9 Bayonet, 2 Butterfly, 3 Dual Daggers, 4 Flip(?), 5 Scorpion(?),
    //          6 Stiletto(?), 7 Tanto/Cleaver(?), 8 Karambit, 9 Jkommando(?), J Kunai,
    //          K Fang(?), L generic knife, N Dagger(?), O Katana, 0 "no icon" text
    // utility: D C4, E HE grenade, F Flashbang, G Molotov, H Incendiary, I Smoke
    // pistols: a G22, b USP, c P350, d Berettas, e Tec-9, f Five-SeveN, g Deagle
    // SMG:     h MAC-10, i UMP45, j MP5, k MP7, l Akimbo UZI, m P90
    // rifles:  n FN FAL, o FAMAS, p AKR-12, q M4, r M16, s AKR, t Mallard(?),
    //          u M4A1(?), v VAL(?), w SM1014(?), y M60(?), z FabM(?)
    // other:   x SPAS, A M40, B M110, C AWM
}
