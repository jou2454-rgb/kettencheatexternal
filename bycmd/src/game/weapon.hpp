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
//   Second log (build c10d758): the values run 4,5,7,9,11,13,15,17,18 over
//   the match and one enemy went 4 -> 13, so +0x30 is the weapon INSTANCE
//   number (grows on every switch / pickup), not the weapon type. It is used
//   here only as a change detector.
//   The type is resolved by name: the objects referenced from uyg (depth 2)
//   are treated as UnityEngine.Object wrappers, their native object is
//   scanned for ASCII names (inline and pointed-to), and the first name that
//   matches k_name_table gives the glyph. The native layout is not pinned,
//   hence the scan; it runs only when the instance number changes (and every
//   3 s while unresolved) with a hard budget of 500 reads.
// ================================================================================

#pragma once

#include "game.hpp"
#include <unordered_map>
#include <cctype>
#include <cstring>
#include <cstdio>

namespace weapon {
    // ---- uyg layout (observed, see header) ----
    inline constexpr uint64_t uyg_active = 0x18;      // 1 = weapon shown, 0 = none/dead (hypothesis, consistent with dumps)
    inline constexpr uint64_t uyg_weapon_id = 0x30;   // byte, observed
    inline constexpr uint64_t uyg_weapon_id_mirror = 0x36; // same byte again, used as a consistency check
    inline constexpr int k_max_id = 127;

    struct Info {
        int id{0};          // weapon instance number (uyg+0x30), changes on switch
        char glyph{0};      // 0 = weapon type not resolved
        const char* name{nullptr};
        bool valid{false};  // instance number read and consistent
    };

    // ---- name -> glyph (asset / prefab names, token match) ----
    struct name_entry { const char* key; char glyph; const char* name; };
    inline constexpr name_entry k_name_table[] = {
        {"m9bayonet",'1',"M9 Bayonet"}, {"bayonet",'1',"M9 Bayonet"}, {"butterfly",'2',"Butterfly"},
        {"dualdaggers",'3',"Dual Daggers"}, {"daggers",'3',"Dual Daggers"}, {"shadowdaggers",'3',"Dual Daggers"},
        {"flip",'4',"Flip Knife"}, {"flipknife",'4',"Flip Knife"}, {"scorpion",'5',"Scorpion"},
        {"stiletto",'6',"Stiletto"}, {"tanto",'7',"Tanto"}, {"karambit",'8',"Karambit"},
        {"jkommando",'9',"Jkommando"}, {"kommando",'9',"Jkommando"}, {"kunai",'J',"Kunai"},
        {"fang",'K',"Fang"}, {"dagger",'N',"Dagger"}, {"katana",'O',"Katana"}, {"knife",'L',"Knife"},
        {"c4",'D',"C4"}, {"bomb",'D',"C4"}, {"hegrenade",'E',"HE"}, {"grenade",'E',"HE"}, {"he",'E',"HE"},
        {"flashbang",'F',"Flash"}, {"flash",'F',"Flash"}, {"molotov",'G',"Molotov"},
        {"incendiary",'H',"Incendiary"}, {"smoke",'I',"Smoke"},
        {"g22",'a',"G22"}, {"glock",'a',"G22"}, {"usp",'b',"USP"}, {"usps",'b',"USP"},
        {"p350",'c',"P350"}, {"p250",'c',"P350"}, {"dualberettas",'d',"Dual Berettas"}, {"berettas",'d',"Dual Berettas"},
        {"beretta",'d',"Dual Berettas"}, {"tec9",'e',"Tec-9"}, {"fiveseven",'f',"Five-SeveN"}, {"57",'f',"Five-SeveN"},
        {"deagle",'g',"Desert Eagle"}, {"deserteagle",'g',"Desert Eagle"},
        {"mac10",'h',"MAC-10"}, {"ump45",'i',"UMP45"}, {"ump",'i',"UMP45"}, {"mp5",'j',"MP5"}, {"mp7",'k',"MP7"},
        {"dualuzi",'l',"Dual UZI"}, {"uzi",'l',"Dual UZI"}, {"p90",'m',"P90"},
        {"fal",'n',"FAL"}, {"g3",'n',"G3"}, {"famas",'o',"FAMAS"}, {"akr12",'p',"AKR-12"}, {"ak12",'p',"AKR-12"},
        {"m4",'q',"M4"}, {"m4a4",'q',"M4"}, {"m4a1",'q',"M4"}, {"m16",'r',"M16"}, {"akr",'s',"AKR"}, {"ak47",'s',"AKR"},
        {"ak",'s',"AKR"}, {"scar",'t',"SCAR"}, {"aug",'v',"AUG"}, {"m249",'w',"M249"},
        {"spas",'x',"SPAS-12"}, {"spas12",'x',"SPAS-12"}, {"m40",'A',"M40"}, {"sr25",'B',"SR-25"},
        {"awm",'C',"AWM"}, {"awp",'C',"AWM"}, {"m110",'y',"M110"}, {"svd",'z',"SVD"},
    };

    inline const name_entry* lookup_key(const char* key, size_t len) {
        if (len < 2 || len > 16) return nullptr;
        for (const name_entry& e : k_name_table) {
            size_t i = 0;
            while (i < len && e.key[i] && e.key[i] == key[i]) ++i;
            if (i == len && e.key[i] == 0) return &e;
        }
        return nullptr;
    }

    // Whole lowercase string without separators first ("AKR-12" -> akr12),
    // then alphanumeric tokens ("wpn_akr12_skin03(Clone)" -> wpn akr12 skin03 clone).
    inline const name_entry* match_text(const char* text, size_t n) {
        char whole[48]; size_t wl = 0;
        for (size_t i = 0; i < n && wl < sizeof(whole) - 1; ++i) {
            const unsigned char ch = (unsigned char)text[i];
            if (std::isalnum(ch)) whole[wl++] = char(std::tolower(ch));
        }
        if (const name_entry* e = lookup_key(whole, wl)) return e;
        char tok[48]; size_t tl = 0;
        for (size_t i = 0; i <= n; ++i) {
            const unsigned char ch = i < n ? (unsigned char)text[i] : 0;
            if (ch && std::isalnum(ch)) { if (tl < sizeof(tok) - 1) tok[tl++] = char(std::tolower(ch)); }
            else { if (const name_entry* e = lookup_key(tok, tl)) return e; tl = 0; }
        }
        return nullptr;
    }

    // Printable ASCII with at least two letters, 3..40 chars, NUL-terminated inside buf.
    inline int ascii_name(const char* buf, size_t cap) {
        int letters = 0; size_t i = 0;
        for (; i < cap && buf[i]; ++i) {
            const unsigned char ch = (unsigned char)buf[i];
            if (ch < 0x20 || ch > 0x7E) return 0;
            if (std::isalpha(ch)) ++letters;
        }
        if (i >= cap || i < 3 || letters < 2) return 0;
        return int(i);
    }

    struct probe_ctx {
        int budget{0};
        char seen[8][56];
        int seen_count{0};
        bool want_seen{false};
    };

    inline void note(probe_ctx& ctx, const char* where, const char* text) {
        if (!ctx.want_seen || ctx.seen_count >= 8) return;
        std::snprintf(ctx.seen[ctx.seen_count++], sizeof(ctx.seen[0]), "%s'%s'", where, text);
    }

    // Native Unity object behind a managed wrapper: scan its first 0x100
    // bytes for inline ASCII runs and for pointers to ASCII strings; the
    // first one that names a weapon wins. Unverified layout => no fixed offset.
    inline const name_entry* scan_native(uint64_t native, const char* where, probe_ctx& ctx) {
        if (!plausible(native) || native < 0x10000 || ctx.budget <= 0) return nullptr;
        unsigned char block[0x100]{};
        --ctx.budget;
        if (!mem_read(native, block, sizeof(block))) return nullptr;
        // inline runs
        for (size_t i = 0; i + 3 < sizeof(block); ) {
            const int n = ascii_name(reinterpret_cast<const char*>(block + i), sizeof(block) - i);
            if (n >= 3) {
                char tag[24]; std::snprintf(tag, sizeof(tag), "%s+0x%zx:", where, i);
                if (const name_entry* e = match_text(reinterpret_cast<const char*>(block + i), size_t(n))) return e;
                note(ctx, tag, reinterpret_cast<const char*>(block + i));
                i += size_t(n);
            } else ++i;
        }
        // pointed strings
        for (size_t i = 0x10; i + 8 <= sizeof(block); i += 8) {
            uint64_t ptr = 0; std::memcpy(&ptr, block + i, 8);
            if (!plausible(ptr) || ptr < 0x10000 || ctx.budget <= 0) continue;
            char str[44]{};
            --ctx.budget;
            if (!mem_read(ptr, str, sizeof(str) - 1)) continue;
            const int n = ascii_name(str, sizeof(str));
            if (n < 3) continue;
            char tag[24]; std::snprintf(tag, sizeof(tag), "%s+0x%zx->", where, i);
            if (const name_entry* e = match_text(str, size_t(n))) return e;
            note(ctx, tag, str);
        }
        return nullptr;
    }

    // Managed object: every reference field is treated as a possible
    // UnityEngine.Object wrapper (+0x10 m_CachedPtr -> native) and, one level
    // down, as a plain object whose fields are wrappers.
    inline const name_entry* scan_managed(uint64_t object, int depth, const char* where, probe_ctx& ctx) {
        if (!obj_ptr(object) || ctx.budget <= 0) return nullptr;
        uint64_t fields[24]{};
        --ctx.budget;
        if (!mem_read(object + 0x10, fields, sizeof(fields))) return nullptr;
        for (int i = 0; i < 24 && ctx.budget > 0; ++i) {
            const uint64_t value = fields[i];
            if (!obj_ptr(value)) continue;
            char tag[20]; std::snprintf(tag, sizeof(tag), "%s+%x", where, 0x10 + i * 8);
            const uint64_t cached = rpm<uint64_t>(value + v100::unity_cached_ptr);
            --ctx.budget;
            if (const name_entry* e = scan_native(cached, tag, ctx)) return e;
            if (depth > 1)
                if (const name_entry* e = scan_managed(value, depth - 1, tag, ctx)) return e;
        }
        return nullptr;
    }

    struct cache_entry {
        uint64_t part{0};
        long long part_ms{0};
        long long read_ms{0};
        long long probe_ms{0};
        long long dump_ms{0};
        int resolved_for_id{-1};   // instance id the glyph was resolved for
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
        next.glyph = c.info.glyph;
        next.name = c.info.name;
        if (c.part) {
            uint8_t block[8]{};
            if (mem_read(c.part + uyg_weapon_id, block, sizeof(block))) {
                const int id = block[0];
                const int mirror = block[uyg_weapon_id_mirror - uyg_weapon_id];
                if (id > 0 && id <= k_max_id && id == mirror) { next.id = id; next.valid = true; }
            }
            // The instance id changes on every weapon switch / pickup: resolve
            // the type again then, and retry every 3 s while still unknown.
            const bool stale = next.valid && (next.id != c.resolved_for_id ||
                               (!next.glyph && now - c.probe_ms > 3000));
            if (stale) {
                c.probe_ms = now;
                probe_ctx ctx;
                ctx.budget = 500;
                ctx.want_seen = is_local;
                const name_entry* e = scan_managed(c.part, 2, "uyg", ctx);
                next.glyph = e ? e->glyph : 0;
                next.name = e ? e->name : nullptr;
                c.resolved_for_id = next.id;
                if (e) diag::log("WEAPON", "%s object=0x%llx inst=%d weapon=%s glyph='%c' (reads left %d)",
                                 is_local ? "LOCAL" : "enemy", (unsigned long long)p.object, next.id, e->name, e->glyph, ctx.budget);
                else if (is_local) {
                    char joined[8 * 58 + 1]; int used = 0;
                    for (int i = 0; i < ctx.seen_count; ++i)
                        used += std::snprintf(joined + used, sizeof(joined) - size_t(used), "%s%s", i ? " | " : "", ctx.seen[i]);
                    diag::log("WEAPON", "LOCAL inst=%d no weapon name found (reads left %d); seen: %s",
                              next.id, ctx.budget, ctx.seen_count ? joined : "(none)");
                }
            }
            if (!next.valid) { next.glyph = 0; next.name = nullptr; c.resolved_for_id = -1; }
            if (is_local && diag::every(c.dump_ms, 60000)) {
                uint64_t q[16]{};
                if (mem_read(c.part, q, sizeof(q))) {
                    char line[16 * 19 + 1]; int used = 0;
                    for (int i = 0; i < 16 && used < int(sizeof(line)) - 20; ++i)
                        used += std::snprintf(line + used, sizeof(line) - size_t(used), "%s%llx", i ? " " : "", (unsigned long long)q[i]);
                    diag::log("WEAPON", "uyg dump LOCAL object=0x%llx part=0x%llx: %s", (unsigned long long)p.object, (unsigned long long)c.part, line);
                }
            }
        } else {
            next = {};
            c.resolved_for_id = -1;
        }
        if (is_local && next.id != c.info.id)
            diag::log("WEAPON", "local weapon inst=%d%s", next.id, next.valid ? "" : " (none)");
        c.info = next;
        return c.info;
    }

    inline const char* g_local_name = nullptr;   // menu status line

    inline int g_local_id = 0;   // menu status line

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
