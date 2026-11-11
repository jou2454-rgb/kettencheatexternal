// ================================================================================
// Weapon icon resolution for the ESP.
//
// Icon font: includes/fonts/esp/weapon.h ("furry_coder-Weapon-Font"), one
// glyph per weapon on the characters 0-9, A-O, a-z. The glyph assignment in
// the table below was identified VISUALLY from the rendered font (knife and
// rifle silhouettes); entries marked (?) are a best guess by shape.
//
// Weapon source: dump.cs 1.1.0 exposes the player's WeaponPart component
// (uyg, TypeInfo 0xCD34CE0) inside wwv.bvvg -> nzk.kevw (cew[]), the same
// array transform_candidates() walks for mtc/bis. The *field layout* of uyg
// is not established, so resolve() does not read a fixed offset: it scans the
// component's object fields (depth 2) for a managed string whose lowercase
// text matches a key in the table, exactly like player_name() finds the
// nickname. This is best-effort and self-diagnosing: when nothing matches,
// the strings that were found are logged ("[WEAPON] probe ...") so the real
// field can be identified from bycmd.log and pinned here as v100 offsets.
// ================================================================================

#pragma once

#include "game.hpp"
#include <string>
#include <unordered_map>
#include <cctype>

namespace weapon {
    struct Info {
        char glyph{0};
        const char* name{nullptr};
        bool valid{false};
    };

    struct entry { const char* key; char glyph; const char* name; };

    // key = lowercase, no spaces/dashes/underscores (see normalize()).
    inline const entry k_table[] = {
        // ---- knives (shapes, several are (?)) ----
        {"m9bayonet",   '1', "M9 Bayonet"},
        {"m9",          '1', "M9 Bayonet"},
        {"bayonet",     '1', "M9 Bayonet"},
        {"butterfly",   '2', "Butterfly"},
        {"dualdaggers", '3', "Dual Daggers"},
        {"daggers",     '3', "Dual Daggers"},
        {"flip",        '4', "Flip Knife (?)"},
        {"flipknife",   '4', "Flip Knife (?)"},
        {"scorpion",    '5', "Scorpion (?)"},
        {"stiletto",    '6', "Stiletto (?)"},
        {"tanto",       '7', "Tanto (?)"},
        {"karambit",    '8', "Karambit"},
        {"jkommando",   '9', "Jkommando (?)"},
        {"kunai",       'J', "Kunai"},
        {"fang",        'K', "Fang (?)"},
        {"standoff",    'L', "Standoff knife (?)"},
        {"dagger",      'N', "Dagger (?)"},
        {"katana",      'O', "Katana"},
        // ---- utility ----
        {"c4",          'D', "C4"},
        {"bomb",        'D', "C4"},
        {"he",          'E', "HE grenade"},
        {"grenade",     'E', "HE grenade"},
        {"hegrenade",   'E', "HE grenade"},
        {"flash",       'F', "Flashbang"},
        {"flashbang",   'F', "Flashbang"},
        {"molotov",     'G', "Molotov"},
        {"incendiary",  'H', "Incendiary"},
        {"smoke",       'I', "Smoke"},
        // ---- pistols ----
        {"g22",         'a', "G22"},
        {"glock",       'a', "G22"},
        {"usp",         'b', "USP"},
        {"usps",        'b', "USP"},
        {"p350",        'c', "P350"},
        {"p250",        'c', "P350"},
        {"dualberettas",'d', "Dual Berettas"},
        {"berettas",    'd', "Dual Berettas"},
        {"tec9",        'e', "Tec-9"},
        {"fiveseven",   'f', "Five-SeveN"},
        {"57",          'f', "Five-SeveN"},
        {"deagle",      'g', "Desert Eagle"},
        {"deserteagle", 'g', "Desert Eagle"},
        // ---- SMG ----
        {"mac10",       'h', "MAC-10"},
        {"ump45",       'i', "UMP45"},
        {"ump",         'i', "UMP45"},
        {"mp5",         'j', "MP5"},
        {"mp7",         'k', "MP7"},
        {"dualuzi",     'l', "Dual UZI (?)"},
        {"uzi",         'l', "Dual UZI (?)"},
        {"p90",         'm', "P90"},
        // ---- rifles ----
        {"fal",         'n', "FAL-type rifle (?)"},
        {"g3",          'n', "FAL-type rifle (?)"},
        {"famas",       'o', "FAMAS"},
        {"akr12",       'p', "AKR-12"},
        {"ak12",        'p', "AKR-12"},
        {"m4",          'q', "M4"},
        {"m4a4",        'q', "M4"},
        {"m16",         'r', "M16"},
        {"akr",         's', "AKR"},
        {"ak47",        's', "AKR"},
        {"scar",        't', "SCAR-type rifle (?)"},
        {"m4a1",        'u', "M4A1 scoped (?)"},
        {"aug",         'v', "Rifle (?)"},
        {"m249",        'w', "LMG (?)"},
        // ---- shotguns / snipers ----
        {"spas",        'x', "SPAS-12"},
        {"spas12",      'x', "SPAS-12"},
        {"m40",         'A', "M40"},
        {"sr25",        'B', "SR-25"},
        {"awm",         'C', "AWM"},
        {"awp",         'C', "AWM"},
        {"m110",        'y', "DMR (?)"},
        {"svd",         'z', "Scoped rifle (?)"},
    };

    inline std::string normalize(const std::string& s) {
        std::string out;
        for (unsigned char ch : s) {
            if (ch == ' ' || ch == '-' || ch == '_' || ch == '.') continue;
            out.push_back(char(std::tolower(ch)));
        }
        return out;
    }

    // Exact key match only: a nickname such as "m4killer" must not become an M4.
    inline const entry* lookup(const std::string& raw) {
        const std::string key = normalize(raw);
        if (key.empty() || key.size() > 16) return nullptr;
        for (const entry& e : k_table)
            if (key == e.key) return &e;
        return nullptr;
    }

    struct cache_entry {
        Info info{};
        uint64_t part{0};
        long long checked_ms{0};
        long long probe_ms{0};
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

    // Scan an object's first 24 reference fields (depth 2) for a weapon name.
    inline const entry* scan_object(uint64_t object, uint64_t string_class, int depth,
                                    std::string* seen, int& seen_count) {
        if (!obj_ptr(object)) return nullptr;
        uint64_t fields[24]{};
        if (!mem_read(object + 0x10, fields, sizeof(fields))) return nullptr;
        for (uint64_t value : fields) {
            if (!obj_ptr(value)) continue;
            const std::string text = read_managed_string(value, string_class);
            if (!text.empty()) {
                if (const entry* e = lookup(text)) return e;
                if (seen && seen_count < 8 && text.size() <= 24) seen[seen_count++] = text;
                continue;
            }
            if (depth > 1) {
                if (const entry* e = scan_object(value, string_class, depth - 1, seen, seen_count)) return e;
            }
        }
        return nullptr;
    }

    inline Info resolve(const player_snapshot& p) {
        cache_entry& c = g_cache[p.object];
        const long long now = diag::now_ms();
        if (now - c.checked_ms < 500) return c.info;
        c.checked_ms = now;

        const uint64_t known_string = rpm<uint64_t>(proc::lib + v100::player_string_literal);
        const uint64_t string_class = plausible(known_string) ? rpm<uint64_t>(known_string) : 0;
        const uint64_t part = find_part(p.object);
        if (!string_class || !part) {
            if (c.info.valid) diag::log("WEAPON", "object=0x%llx weapon part lost", (unsigned long long)p.object);
            c.info = {};
            c.part = part;
            if (diag::every(c.probe_ms, 10000))
                diag::log("WEAPON", "object=0x%llx no uyg part (string class=0x%llx)",
                          (unsigned long long)p.object, (unsigned long long)string_class);
            return c.info;
        }

        std::string seen[8];
        int seen_count = 0;
        const entry* e = scan_object(part, string_class, 2, seen, seen_count);
        Info next{};
        if (e) { next.glyph = e->glyph; next.name = e->name; next.valid = true; }

        if (next.valid != c.info.valid || next.glyph != c.info.glyph || part != c.part) {
            if (next.valid)
                diag::log("WEAPON", "object=0x%llx part=0x%llx weapon=%s glyph='%c'",
                          (unsigned long long)p.object, (unsigned long long)part, next.name, next.glyph);
        }
        if (!next.valid && diag::every(c.probe_ms, 5000)) {
            std::string joined;
            for (int i = 0; i < seen_count; ++i) { if (i) joined += " | "; joined += seen[i]; }
            diag::log("WEAPON", "probe object=0x%llx part=0x%llx no match; strings: %s",
                      (unsigned long long)p.object, (unsigned long long)part,
                      joined.empty() ? "(none)" : joined.c_str());
        }
        c.info = next;
        c.part = part;
        return c.info;
    }
}
