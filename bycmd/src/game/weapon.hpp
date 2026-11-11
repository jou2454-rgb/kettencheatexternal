// ================================================================================
// Active weapon of a player, read-only.
//
// Chain (dump.cs 1.1.0, see v100 in game.hpp):
//   odq (wwv) +0x28 nzk -> +0x20 cew[] -> element of class uyg (WeaponPart)
//   uyg +0x30 WeaponContext { +0x0 Owner NetObjectId, +0x10 Team, +0x50 ActiveWeapon : jrx }
//   jrx +0x10 WeaponId { byte Raw }
//
// TypeInfo slots on this il2cpp can stay lazy (encoded index instead of a
// klass pointer). When the uyg / jrx slot is not initialised, the class is
// learned once from a part that passes the structural check: owner id equals
// the player's NetObjectId, team equals the player's team, and the weapon id
// is one of the known WeaponIds.
// ================================================================================

#pragma once

#include "game.hpp"
#include <cstdint>
#include <unordered_map>

namespace weapon {
    struct id_name { uint8_t id; const char* name; };

    // WeaponIds static readonly values, measured from WeaponIds..cctor
    // (RVA 0x9CADAE0) in libunity build-id 69815f2befe135c2: each field is
    // WeaponId(byte) built from an immediate. Gameplay-only ids (mines,
    // turrets, event weapons) are kept so they are not shown as unknown.
    inline constexpr id_name k_ids[] = {
        {0x0B, "G22"},        {0x0C, "USP"},        {0x0D, "P350"},       {0x0F, "Deagle"},
        {0x10, "Tec-9"},      {0x11, "Five-seveN"}, {0x12, "Berettas"},   {0x20, "UMP45"},
        {0x21, "Akimbo Uzi"}, {0x22, "MP7"},        {0x23, "P90"},        {0x24, "MP5"},
        {0x25, "MAC10"},      {0x2A, "VAL"},        {0x2B, "M4A1"},       {0x2C, "AKR"},
        {0x2D, "AKR12"},      {0x2E, "M4"},         {0x2F, "M16"},        {0x30, "FAMAS"},
        {0x31, "FN FAL"},     {0x33, "AWM"},        {0x34, "M40"},        {0x35, "M110"},
        {0x36, "Mallard"},    {0x3E, "SM1014"},     {0x3F, "FabM"},       {0x40, "M60"},
        {0x41, "SPAS"},       {0x46, "Knife"},      {0x47, "Hands"},      {0x5B, "HE"},
        {0x5C, "Smoke"},      {0x5D, "Flash"},      {0x5E, "Molotov"},    {0x5F, "Incendiary"},
        {0x64, "Bomb"},       {0x65, "Defuser"},    {0x66, "Defuse kit"}, {0x6E, "Vest"},
        {0x6F, "Vest+Helmet"},{0x70, "Mine"},       {0x71, "Turret"},     {0x72, "Explosion"},
        {0x73, "Stun mine"},  {0x74, "Return mark"},{0x75, "Electricity"},{0x76, "Fire"},
        {0x77, "Teleport"},   {0x78, "Clown"},      {0x79, "Buff"},       {0x7A, "Stun grenade"},
        {0x7B, "Cleaver"},    {0x7C, "Deagle Clown"},{0x7D, "Event SPAS"},{0x7E, "Ghost hands"},
        {0x7F, "Pudge"},      {0x80, "Ghost clone"},{0x81, "Chandelier"},
    };

    inline const char* name(int id) {
        for (const id_name& e : k_ids)
            if (e.id == id) return e.name;
        return nullptr;
    }

    struct player_weapon {
        uint64_t part{0};        // uyg
        int id{-1};              // WeaponId.Raw, -1 = unknown
        long long part_checked_ms{0};
        long long id_read_ms{0};
    };
    inline std::unordered_map<uint64_t, player_weapon> g_players; // key: odq object
    inline uint64_t g_part_class = 0;    // uyg klass (slot or learned)
    inline uint64_t g_weapon_class = 0;  // jrx klass (slot or learned)
    inline int g_context_pid = -1;
    inline uint64_t g_context_lib = 0;

    inline void reset_if_context_changed() {
        if (g_context_pid == proc::pid && g_context_lib == proc::lib) return;
        g_players.clear();
        g_part_class = g_weapon_class = 0;
        g_context_pid = proc::pid;
        g_context_lib = proc::lib;
    }

    // jrx -> WeaponId. Returns -1 when the object is not a known weapon.
    inline int read_weapon_id(uint64_t weapon_object) {
        if (!obj_ptr(weapon_object)) return -1;
        uint8_t block[0x18]{};
        if (!mem_read(weapon_object, block, sizeof(block))) return -1;
        uint64_t klass = 0;
        std::memcpy(&klass, block, sizeof(klass));
        const int id = block[v100::weapon_id];
        if (!name(id)) return -1;
        if (g_weapon_class) return klass == g_weapon_class ? id : -1;
        if (!obj_ptr(klass)) return -1;
        g_weapon_class = klass;
        diag::log("WEAPON", "jrx klass learned 0x%llx from id=0x%02x (%s)",
                  (unsigned long long)klass, id, name(id));
        return id;
    }

    // Structural identity of a WeaponPart belonging to this player.
    inline bool part_matches_owner(uint64_t part, const player_snapshot& p) {
        uint8_t context[0x14]{};
        if (!mem_read(part + v100::weapon_part_owner, context, sizeof(context))) return false;
        uint32_t owner = 0;
        std::memcpy(&owner, context, sizeof(owner));
        const uint8_t team = context[v100::weapon_part_team - v100::weapon_part_owner];
        return owner == p.id && team == p.team;
    }

    inline uint64_t find_part(const player_snapshot& p) {
        const uint64_t container = rpm<uint64_t>(p.object + v100::player_parts);
        if (!obj_ptr(container)) return 0;
        const uint64_t array = rpm<uint64_t>(container + v100::parts_array);
        if (!obj_ptr(array)) return 0;
        const uint64_t length = rpm<uint64_t>(array + v100::array_length);
        if (length == 0 || length > 64) return 0;
        uint64_t parts[64]{};
        if (!mem_read(array + v100::array_data, parts, size_t(length) * 8)) return 0;
        for (uint64_t i = 0; i < length; ++i) {
            const uint64_t part = parts[i];
            if (!obj_ptr(part)) continue;
            const uint64_t klass = rpm<uint64_t>(part);
            if (g_part_class) {
                if (klass == g_part_class) return part;
                continue;
            }
            // Class not known yet: accept only a part that is unambiguously
            // this player's weapon component, then remember its class.
            if (!obj_ptr(klass) || !part_matches_owner(part, p)) continue;
            if (read_weapon_id(rpm<uint64_t>(part + v100::weapon_part_active)) < 0) continue;
            g_part_class = klass;
            diag::log("WEAPON", "uyg klass learned 0x%llx (slot lazy) from player 0x%llx id=%u team=%u",
                      (unsigned long long)klass, (unsigned long long)p.object, p.id, unsigned(p.team));
            return part;
        }
        return 0;
    }

    // Active weapon id of a player, -1 when unknown. The part pointer is
    // cached per player and revalidated every 500 ms; the id itself is re-read
    // every 100 ms (two remote reads).
    inline int active_id(const player_snapshot& p) {
        reset_if_context_changed();
        if (!g_part_class) g_part_class = typeinfo(v100::weapon_part_typeinfo);
        if (!g_weapon_class) g_weapon_class = typeinfo(v100::weapon_typeinfo);

        player_weapon& w = g_players[p.object];
        const long long now = diag::now_ms();
        if (!w.part || now - w.part_checked_ms > 500) {
            w.part_checked_ms = now;
            if (!w.part || !g_part_class || rpm<uint64_t>(w.part) != g_part_class) {
                w.part = find_part(p);
                w.id = -1;
                w.id_read_ms = 0;
            }
        }
        if (!w.part) return -1;
        if (now - w.id_read_ms >= 100) {
            w.id_read_ms = now;
            w.id = read_weapon_id(rpm<uint64_t>(w.part + v100::weapon_part_active));
        }
        return w.id;
    }
}
