// Layout of "rise 1.0.0F5" (Standoff-family Unity/il2cpp build).
//
// Every value here is either taken from the dump (source noted) or marked FILL.
// FILL = 0 means "not known yet": readFrame() refuses to run and lists the
// missing items. How to get each one is in README.md.
#pragma once
#include <cstdint>

namespace rise {

// ---- il2cpp object / array basics (standard for every il2cpp build) ----
inline constexpr uint64_t kUnityCachedPtr = 0x10;   // managed Unity object -> native object (m_CachedPtr)
inline constexpr uint64_t kArrayMaxLength = 0x18;   // Il2CppArray.max_length
inline constexpr uint64_t kArrayVector = 0x20;      // first element of an Il2CppArray

// ---- script.json: TypeInfo RVA (relative to the il2cpp module base) ----
inline constexpr uint64_t kTypeInfoPlayerManager = 0x424aef0;   // Axlebolt.Standoff.Player.PlayerManager_TypeInfo
inline constexpr uint64_t kTypeInfoPlayerController = 0x424aec8;  // ...PlayerController_TypeInfo

// ---- dump.cs: PlayerManager (fields with offsets) ----
inline constexpr uint64_t kPM_Players = 0x20;       // Dictionary<int, PlayerController>
inline constexpr uint64_t kPM_LocalPlayer = 0x68;   // PlayerController (name obfuscated, type matches; verify)

// ---- il2cpp.h: Dictionary<int, PlayerController>_Fields ----
inline constexpr uint64_t kDict_Entries = 0x18;     // _entries (Entry[])
inline constexpr uint64_t kDict_Count = 0x20;       // _count (int32, high-water mark)

// ---- mscorlib Dictionary<TKey,TValue>.Entry (hashCode, next, key, value) ----
inline constexpr uint64_t kEntry_Next = 0x4;
inline constexpr uint64_t kEntry_Value = 0x10;      // int key at 0x8, value pointer at 0x10
inline constexpr uint64_t kEntry_Size = 0x18;

// ---- dump.cs: PlayerController ----
inline constexpr uint64_t kPC_Team = 0x59;          // Team enum, one byte
inline constexpr uint64_t kPC_MainCamera = 0xC0;    // PlayerMainCamera
inline constexpr uint64_t kPC_Transform = 0xD8;     // UnityEngine.Transform (managed)

// ---- dump.cs + libil2cpp.so (build 02b5a931...): player position, managed chain ----
// MovementController.HAEEFHHBFEEGFHH calls Transform.set_position with a Vector3 read from
// translationData (+0x44), which is the character's root position.
inline constexpr uint64_t kPC_Movement = 0x78;       // MovementController
inline constexpr uint64_t kMC_TranslationData = 0xA8; // DFHEFCGADAGGAEH
inline constexpr uint64_t kTD_Position = 0x44;       // Vector3, root position (world space)

// ---- dump.cs: PlayerMainCamera ----
inline constexpr uint64_t kPMC_Camera = 0x18;       // UnityEngine.Camera (managed)

// ---- FILL: needs the binary (libil2cpp.so of this build) or a runtime calibration ----
// Health: candidate float at +0x5C of PlayerController (unverified, calibrate by taking damage).
inline constexpr uint64_t kPC_HealthCandidate = 0x5C;
inline constexpr uint64_t kPcMaxHealthDefault = 100;  // max health: default, statics MaxCtHealth/MaxTrHealth not read yet

}  // namespace rise
