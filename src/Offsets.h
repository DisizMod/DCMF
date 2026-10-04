#pragma once

#include <cstdint>

// The engine addresses the conditions call, taken from Open Animation
// Replacer's Offsets.h. Only what the conditions and Utils reach: the rest of
// that file is the replacement engine's, and needs Havok headers this copy
// does not carry.

inline uint32_t g_durationOfApplicationRunTimeMS = 0;

static int32_t* g_RelationshipRankTypeIdsByIndex = (int32_t*)REL::VariantID(502260, 369311, 0x1E911A0).address();                                          // 1DD3EF8, 1E67FE8, 1E911A0
static float& g_worldScale = *(float*)REL::VariantID(231896, 188105, 0x15B78F4).address();                                                                 // 154064C, 1637AA0, 15B78F4
static float& g_worldScaleInverse = *(float*)REL::VariantID(230692, 187407, 0x15ADFE8).address();                                                          // 1536BA0, 162DF48, 15ADFE8

using tActor_GetCombatState = RE::ACTOR_COMBAT_STATE (*)(RE::Actor* a_this);
static REL::Relocation<tActor_GetCombatState> Actor_GetCombatState{ REL::VariantID(37603, 38556, 0x62DD00) };  // 624E90, 64A520, 62DD00

using tActor_GetEquippedShout = RE::TESShout* (*)(RE::Actor* a_this);
static REL::Relocation<tActor_GetEquippedShout> Actor_GetEquippedShout{ REL::VariantID(37822, 38771, 0x63B4E0) };  // 632610, 6584A0, 63B4E0

using tActor_GetFactionRank = int32_t (*)(RE::Actor* a_this, const RE::TESFaction* a_faction, bool bIsPlayer);
static REL::Relocation<tActor_GetFactionRank> Actor_GetFactionRank{ REL::VariantID(36668, 37676, 0x600190) };  // 5F7A20, 61E7B0, 600190

using tActor_GetLightLevel = float (*)(RE::Actor* a_this);
static REL::Relocation<tActor_GetLightLevel> Actor_GetLightLevel{ REL::VariantID(36759, 37775, 0x606B80) };  // 5FE370, 6253D0, 606B80

using tActor_GetMovementDirectionRelativeToFacing = float (*)(RE::Actor* a_this);
static REL::Relocation<tActor_GetMovementDirectionRelativeToFacing> Actor_GetMovementDirectionRelativeToFacing{ REL::VariantID(36935, 37960, 0x611900) };  // 608D40, 630540, 611900

using tActor_GetTarget = RE::Actor* (*)(RE::Actor* a_this);
static REL::Relocation<tActor_GetTarget> Actor_GetTarget{ REL::VariantID(37655, 38608, 0x630CE0) };  // 627E10, 64D900, 630CE0

using tActor_IsInDialogue = bool (*)(RE::Actor* a_this);
static REL::Relocation<tActor_IsInDialogue> Actor_IsInDialogue{ REL::VariantID(36727, 37739, 0x604230) };  // 5FBA40, 6229C0, 604230

using tActor_IsMoving = bool (*)(RE::Actor* a_this);
static REL::Relocation<tActor_IsMoving> Actor_IsMoving{ REL::VariantID(36928, 37953, 0x6116C0) };  // 608B30, 630330, 6116C0

using tActor_IsTalking = bool (*)(RE::Actor* a_this);
static REL::Relocation<tActor_IsTalking> Actor_IsTalking{ REL::VariantID(36277, 37266, 0x5DA8F0) };  // 5D2330, 5F69A0, 5DA8F0

using tActor_HasMagicEffectWithKeyword = bool (*)(RE::Actor* a_this, RE::BGSKeyword* a_keyword);
static REL::Relocation<tActor_HasMagicEffectWithKeyword> Actor_HasMagicEffectWithKeyword{ REL::VariantID(19220, 19646, 0x29DB70) };  // 28C440, 29E660, 29DB70

using tMagicTarget_HasMagicEffectWithKeyword = bool (*)(RE::MagicTarget* a_this, RE::BGSKeyword* a_keyword, void* a3);
static REL::Relocation<tMagicTarget_HasMagicEffectWithKeyword> MagicTarget_HasMagicEffectWithKeyword{ REL::VariantID(33734, 34518, 0x557400) };  // 553160, 56E7A0, 557400

using tbhkCharacterController_CalcFallDistance = float (*)(RE::bhkCharacterController* a_this);
static REL::Relocation<tbhkCharacterController_CalcFallDistance> bhkCharacterController_CalcFallDistance{ REL::VariantID(76430, 78269, 0xE142F0) };  // DBF370, DFFB90, E142F0


using tGetCurrentGameTime = float (*)();
static REL::Relocation<tGetCurrentGameTime> GetCurrentGameTime{ REL::VariantID(56475, 56832, 0x9F3290) };  // 9B8740, 9DC890, 9F3290

using tInventoryChanges_WornHasKeyword = bool (*)(RE::InventoryChanges* a_this, RE::BGSKeyword* a_keyword);
static REL::Relocation<tInventoryChanges_WornHasKeyword> InventoryChanges_WornHasKeyword{ REL::VariantID(15808, 16046, 0x1E9D10) };  // 1D9170, 1E4C70, 1E9D10



using tTESNPC_GetRelationshipRankIndex = int32_t (*)(RE::TESNPC* a_npc1, RE::TESNPC* a_npc2);
static REL::Relocation<tTESNPC_GetRelationshipRankIndex> TESNPC_GetRelationshipRankIndex{ REL::VariantID(23624, 24076, 0x355790) };  // 345ED0, 35C270, 355790

using tTESObjectREFR_CalcFallDamage = float (*)(RE::TESObjectREFR* a_this, float a_fallDamage, float a_mult);
static REL::Relocation<tTESObjectREFR_CalcFallDamage> TESObjectREFR_CalcFallDamage{ REL::VariantID(37294, 38273, 0x6214D0) };  // 618920, 63E9B0, 6214D0

using tTESObjectREFR_GetInventoryChanges = RE::InventoryChanges* (*)(RE::TESObjectREFR* a_this);
static REL::Relocation<tTESObjectREFR_GetInventoryChanges> TESObjectREFR_GetInventoryChanges{ REL::VariantID(15801, 16039, 0x1E99C0) };  // 1D8E20, 1E4860, 1E99C0

using tTESObjectREFR_GetLocationRefType = RE::BGSLocationRefType* (*)(RE::TESObjectREFR* a_this);
static REL::Relocation<tTESObjectREFR_GetLocationRefType> TESObjectREFR_GetLocationRefType{ REL::VariantID(19843, 20248, 0x2B9790) };  // 2A8020, 2B9E50, 2B9790

using tTESObjectREFR_GetSubmergeLevel = float (*)(RE::TESObjectREFR* a_this, float a_zPos, RE::TESObjectCELL* a_parentCell);
static REL::Relocation<tTESObjectREFR_GetSubmergeLevel> TESObjectREFR_GetSubmergeLevel{ REL::VariantID(36452, 37448, 0x5E9B60) };  // 5E1510, 607080, 5E9B60

using tTESQuest_GetStageDone = bool (*)(RE::TESQuest* a_this, uint16_t a_stageIndex);
static REL::Relocation<tTESQuest_GetStageDone> TESQuest_GetStageDone{ REL::VariantID(24483, 25011, 0x3804C0) };  // 370B20, 3881F0, 3804C0

