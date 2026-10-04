#include "Conditions.h"
#include "BuiltinResources.h"

#include <bit>
#include "Resources.h"
#include "Program.h"
#include "ActorState.h"
#include "AnimationGraph.h"
#include "BodyTypes.h"
#include "DetectedProblems.h"
#include "Offsets.h"
#include "ModRegistry.h"
#include "Utils.h"
#include "apply/Entries.h"
#include "scan/Scan.h"

#include <imgui_stdlib.h>
#include <limits>
#include <ranges>

namespace Conditions
{
	namespace
	{
		// A form row's form as a type, or its id; none when a feed holds nothing for the actor
		template <class T>
		T* FormAs(RE::TESForm* a_form)
		{
			return a_form ? a_form->As<T>() : nullptr;
		}

		RE::FormID FormIdOf(RE::TESForm* a_form)
		{
			return a_form ? a_form->GetFormID() : 0;
		}

		// What the actor has is the form wanted; nothing wanted, as an empty feed, matches nothing, an empty slot included
		bool Matches(const RE::TESForm* a_have, const RE::TESForm* a_wanted)
		{
			return a_wanted && a_have == a_wanted;
		}
	}

	constexpr char CharToLower(const char a_char)
	{
		return (a_char >= 'A' && a_char <= 'Z') ? a_char + ('a' - 'A') : a_char;
	}

	constexpr uint32_t Hash(const char* a_data, const size_t a_size) noexcept
	{
		uint32_t hash = 5381;

		for (const char* c = a_data; c < a_data + a_size; ++c)
			hash = ((hash << 5) + hash) + CharToLower(*c);

		return hash;
	}

	constexpr uint32_t operator"" _h(const char* a_str, size_t a_size) noexcept
	{
		return Hash(a_str, a_size);
	}

	std::string_view CorrectLegacyConditionName(std::string_view a_conditionName)
	{
		if (a_conditionName == "IsEquippedShout"sv) {
			return "IsEquippedPower"sv;
		}

		return a_conditionName;
	}

	std::string_view CorrectConditionName(std::string_view a_conditionName)
	{
		if (a_conditionName == "CurrentPackageProcedureType"sv) {
			return "CurrentPackageType"sv;
		}

		return a_conditionName;
	}

	std::unique_ptr<ICondition> CreateConditionFromString(std::string_view a_line)
	{
		if (a_line.starts_with(";"sv)) {
			return nullptr;
		}

		const bool bNOT = a_line.starts_with("NOT"sv);

		if (bNOT) {
			a_line = a_line.substr(3);
			a_line = Utils::TrimWhitespace(a_line);
		}
		const size_t functionEndPos = a_line.find_first_of(" ("sv);
		const size_t argumentStartPos = a_line.find_first_not_of(" ("sv, functionEndPos);
		const size_t argumentEndPos = a_line.find(")"sv);

		std::string conditionName;
		std::string argument;
		if (functionEndPos == std::string_view::npos || argumentStartPos == std::string_view::npos) {
			conditionName = a_line;
			argument = ""sv;
		} else {
			conditionName = a_line.substr(0, functionEndPos);
			argument = a_line.substr(argumentStartPos, argumentEndPos - argumentStartPos);
		}

		if (auto condition = ModRegistry::GetSingleton().CreateCondition(CorrectLegacyConditionName(conditionName))) {
			condition->PreInitialize();
			condition->InitializeLegacy(argument.data());
			condition->SetNegated(bNOT);
			condition->PostInitialize();
			return std::move(condition);
		}

		auto errorStr = std::format("Invalid line: \"{}\"", a_line);
		return std::make_unique<InvalidCondition>(errorStr);
	}

	std::unique_ptr<ICondition> CreateConditionFromJson(rapidjson::Value& a_value, ConditionSet* a_parentConditionSet /* = nullptr*/)
	{
		if (!a_value.IsObject()) {
			logger::error("Missing condition value!");
			return std::make_unique<InvalidCondition>("Missing condition value");
		}

		const auto object = a_value.GetObj();

		if (const auto conditionNameIt = object.FindMember("condition"); conditionNameIt != object.MemberEnd() && conditionNameIt->value.IsString()) {
			bool bHasRequiredPlugin = false;
			std::string requiredPluginName;
			if (const auto requiredPluginIt = object.FindMember("requiredPlugin"); requiredPluginIt != object.MemberEnd() && requiredPluginIt->value.IsString()) {
				bHasRequiredPlugin = true;
				requiredPluginName = requiredPluginIt->value.GetString();
			}

			REL::Version requiredVersion;
			if (const auto requiredVersionIt = object.FindMember("requiredVersion"); requiredVersionIt != object.MemberEnd() && requiredVersionIt->value.IsString()) {
				requiredVersion = REL::Version(requiredVersionIt->value.GetString());
			}

			EssentialState essentialState = EssentialState::kEssential;
			if (const auto essentialIt = object.FindMember("essential"); essentialIt != object.MemberEnd() && essentialIt->value.IsInt()) {
				essentialState = static_cast<EssentialState>(essentialIt->value.GetInt());
			}
			bool bEssential = essentialState == EssentialState::kEssential;

			std::string conditionName = conditionNameIt->value.GetString();

			if (bHasRequiredPlugin && !requiredPluginName.empty()) {
				// check if required plugin is loaded and is the required version or higher
				if (!ModRegistry::GetSingleton().IsPluginLoaded(requiredPluginName, requiredVersion)) {
					auto errorStr = std::format("Missing required plugin {} version {}!", requiredPluginName, requiredVersion.string("."));
					if (bEssential) {
						DetectedProblems::GetSingleton().AddMissingPluginName(requiredPluginName, requiredVersion);
						logger::error("{}", errorStr);
						return std::make_unique<InvalidCondition>(errorStr);
					} else {
						auto condition = std::make_unique<InvalidNonEssentialCondition>(errorStr, conditionName, a_value);
						condition->Initialize(&a_value);
						return std::move(condition);
					}
				}
			} else {
				// no required plugin: the required version against the version of the conditions this build carries
				constexpr REL::Version kConditionsVersion{ 3, 2, 1 };
				if (requiredVersion > kConditionsVersion) {
					auto errorStr = std::format("Condition {} requires a newer version of DCMF! ({})", conditionName, requiredVersion.string("."));
					if (bEssential) {
						DetectedProblems::GetSingleton().MarkOutdatedVersion();
						logger::error("{}", errorStr);
						return std::make_unique<InvalidCondition>(errorStr);
					} else {
						auto condition = std::make_unique<InvalidNonEssentialCondition>(errorStr, conditionName, a_value);
						condition->Initialize(&a_value);
						return std::move(condition);
					}
				}
			}

			if (auto condition = ModRegistry::GetSingleton().CreateCondition(CorrectConditionName(conditionName))) {
				if (condition->IsDeprecated()) {
					return ConvertDeprecatedCondition(condition, conditionName, a_value);
				}
				condition->PreInitialize();
				if (a_parentConditionSet) {  // set the parent condition set early if possible so that PRESET conditions contained inside a multicondition can find the parent mod on their init
					condition->SetParentConditionSet(a_parentConditionSet);
				}
				condition->Initialize(&a_value);
				condition->PostInitialize();
				return std::move(condition);
			}

			// at this point we failed to create a new condition even though the plugin is present and not outdated. This means that the plugin failed to initialize factories for whatever reason.
			if (bHasRequiredPlugin) {
				auto errorStr = std::format("Condition {} not found in plugin {}!", conditionName, requiredPluginName);
				if (bEssential) {
					DetectedProblems::GetSingleton().AddInvalidPluginName(requiredPluginName, requiredVersion);
					logger::error("{}", errorStr);
					return std::make_unique<InvalidCondition>(errorStr);
				} else {
					auto condition = std::make_unique<InvalidNonEssentialCondition>(errorStr, conditionName, a_value);
					condition->Initialize(&a_value);
					return std::move(condition);
				}
			}

			auto errorStr = std::format("Condition {} not found!", conditionName);
			if (bEssential) {
				logger::error("{}", errorStr);
				return std::make_unique<InvalidCondition>(errorStr);
			} else {
				auto condition = std::make_unique<InvalidNonEssentialCondition>(errorStr, conditionName, a_value);
				condition->Initialize(&a_value);
				return std::move(condition);
			}
		}

		auto errorStr = "Condition name not found!";
		logger::error("{}", errorStr);

		return std::make_unique<InvalidCondition>(errorStr);
	}


	void SurfaceMaterialCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		numericComponent->value.getEnumMap = &SurfaceMaterialCondition::GetEnumMap;
	}

	RE::BSString SurfaceMaterialCondition::GetArgument() const
	{
		return RE::MaterialIDToString(GetRequiredMaterialID());
	}



	RE::MATERIAL_ID SurfaceMaterialCondition::GetRequiredMaterialID() const
	{
		const auto& materialIDs = GetMaterialIDs();
		int32_t currentValue = static_cast<int32_t>(numericComponent->GetNumericValue(nullptr));
		if (currentValue >= materialIDs.size()) {
			return RE::MATERIAL_ID::kNone;
		}

		return materialIDs[currentValue];
	}

	const std::vector<RE::MATERIAL_ID>& SurfaceMaterialCondition::GetMaterialIDs()
	{
		static const std::vector<RE::MATERIAL_ID> materialIDs = [] {
			std::vector<RE::MATERIAL_ID> ids{
				RE::MATERIAL_ID::kNone,
				RE::MATERIAL_ID::kStoneBroken,
				RE::MATERIAL_ID::kBlockBlade1Hand,
				RE::MATERIAL_ID::kMeat,
				RE::MATERIAL_ID::kCarriageWheel,
				RE::MATERIAL_ID::kMetalLight,
				RE::MATERIAL_ID::kWoodLight,
				RE::MATERIAL_ID::kSnow,
				RE::MATERIAL_ID::kGravel,
				RE::MATERIAL_ID::kChainMetal,
				RE::MATERIAL_ID::kBottle,
				RE::MATERIAL_ID::kWood,
				RE::MATERIAL_ID::kAsh,
				RE::MATERIAL_ID::kSkin,
				RE::MATERIAL_ID::kBlockBlunt,
				RE::MATERIAL_ID::kDLC1DeerSkin,
				RE::MATERIAL_ID::kInsect,
				RE::MATERIAL_ID::kBarrel,
				RE::MATERIAL_ID::kCeramicMedium,
				RE::MATERIAL_ID::kBasket,
				RE::MATERIAL_ID::kIce,
				RE::MATERIAL_ID::kGlassStairs,
				RE::MATERIAL_ID::kStoneStairs,
				RE::MATERIAL_ID::kWater,
				RE::MATERIAL_ID::kDraugrSkeleton,
				RE::MATERIAL_ID::kBlade1Hand,
				RE::MATERIAL_ID::kBook,
				RE::MATERIAL_ID::kCarpet,
				RE::MATERIAL_ID::kMetalSolid,
				RE::MATERIAL_ID::kAxe1Hand,
				RE::MATERIAL_ID::kBlockBlade2Hand,
				RE::MATERIAL_ID::kOrganicLarge,
				RE::MATERIAL_ID::kAmulet,
				RE::MATERIAL_ID::kWoodStairs,
				RE::MATERIAL_ID::kMud,
				RE::MATERIAL_ID::kBoulderSmall,
				RE::MATERIAL_ID::kSnowStairs,
				RE::MATERIAL_ID::kStoneHeavy,
				RE::MATERIAL_ID::kCharacterBumper,
				RE::MATERIAL_ID::kTrap,
				RE::MATERIAL_ID::kBowsStaves,
				RE::MATERIAL_ID::kAlduin,
				RE::MATERIAL_ID::kBlockBowsStaves,
				RE::MATERIAL_ID::kWoodAsStairs,
				RE::MATERIAL_ID::kSteelGreatSword,
				RE::MATERIAL_ID::kGrass,
				RE::MATERIAL_ID::kBoulderLarge,
				RE::MATERIAL_ID::kStoneAsStairs,
				RE::MATERIAL_ID::kBlade2Hand,
				RE::MATERIAL_ID::kBottleSmall,
				RE::MATERIAL_ID::kBoneActor,
				RE::MATERIAL_ID::kSand,
				RE::MATERIAL_ID::kMetalHeavy,
				RE::MATERIAL_ID::kDLC1SabreCatPelt,
				RE::MATERIAL_ID::kIceForm,
				RE::MATERIAL_ID::kDragon,
				RE::MATERIAL_ID::kBlade1HandSmall,
				RE::MATERIAL_ID::kSkinSmall,
				RE::MATERIAL_ID::kPotsPans,
				RE::MATERIAL_ID::kSkinSkeleton,
				RE::MATERIAL_ID::kBlunt1Hand,
				RE::MATERIAL_ID::kStoneStairsBroken,
				RE::MATERIAL_ID::kSkinLarge,
				RE::MATERIAL_ID::kOrganic,
				RE::MATERIAL_ID::kBone,
				RE::MATERIAL_ID::kWoodHeavy,
				RE::MATERIAL_ID::kChain,
				RE::MATERIAL_ID::kDirt,
				RE::MATERIAL_ID::kGhost,
				RE::MATERIAL_ID::kSkinMetalLarge,
				RE::MATERIAL_ID::kBlockAxe,
				RE::MATERIAL_ID::kArmorLight,
				RE::MATERIAL_ID::kShieldLight,
				RE::MATERIAL_ID::kCoin,
				RE::MATERIAL_ID::kBlockBlunt2Hand,
				RE::MATERIAL_ID::kShieldHeavy,
				RE::MATERIAL_ID::kArmorHeavy,
				RE::MATERIAL_ID::kArrow,
				RE::MATERIAL_ID::kGlass,
				RE::MATERIAL_ID::kStone,
				RE::MATERIAL_ID::kWaterPuddle,
				RE::MATERIAL_ID::kCloth,
				RE::MATERIAL_ID::kSkinMetalSmall,
				RE::MATERIAL_ID::kWard,
				RE::MATERIAL_ID::kWeb,
				RE::MATERIAL_ID::kTrailerSteelSword,
				RE::MATERIAL_ID::kBlunt2Hand,
				RE::MATERIAL_ID::kDLC1SwingingBridge,
				RE::MATERIAL_ID::kBoulderMedium
			};

			std::ranges::sort(ids, [](RE::MATERIAL_ID a_lhs, RE::MATERIAL_ID a_rhs) {
				return RE::MaterialIDToString(a_lhs) < RE::MaterialIDToString(a_rhs);
			});

			return ids;
		}();

		return materialIDs;
	}

	bool SurfaceMaterialCondition::GetSurfaceMaterialID(RE::TESObjectREFR* a_refr, RE::MATERIAL_ID& a_outMaterialID) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto charController = actor->GetCharController()) {
					a_outMaterialID = *SKSE::stl::adjust_pointer<RE::MATERIAL_ID>(charController, 0x304);
					return true;
				}
			}
		}

		return false;
	}

	const std::map<int32_t, std::string_view>& SurfaceMaterialCondition::GetEnumMap()
	{
		static bool bInitialized = false;
		static std::map<int32_t, std::string_view> enumMap;

		if (!bInitialized) {
			const std::vector<RE::MATERIAL_ID>& materialIDs = GetMaterialIDs();
			for (int32_t i = 0; i < materialIDs.size(); ++i) {
				enumMap[i] = RE::MaterialIDToString(materialIDs[i]);
			}
			bInitialized = true;
		}
		return enumMap;
	}

	bool MovementSurfaceAngleCondition::MovementSurfaceAngleConditionStateData::Update(float a_deltaTime)
	{
		if (const auto ref = _refHandle.get()) {
			RE::hkVector4 newSurfaceNormal;
			if (Utils::GetSurfaceNormal(ref.get(), newSurfaceNormal, _bUseNavmesh)) {
				if (_bHasValue) {
					const float effectiveFactor = 1.0f - std::exp(-_smoothingFactor * a_deltaTime);
					_smoothedNormal = Utils::Mix(_smoothedNormal, newSurfaceNormal, effectiveFactor);
				} else {
					_smoothedNormal = newSurfaceNormal;
					_bHasValue = true;
				}
			}
		}

		return false;
	}

	bool MovementSurfaceAngleCondition::MovementSurfaceAngleConditionStateData::GetSmoothedSurfaceNormal(RE::hkVector4& a_outVector) const
	{
		a_outVector = _smoothedNormal;
		return _bHasValue;
	}

	RE::BSString MovementSurfaceAngleCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		const auto stateArgument = stateComponent->GetArgument();

		return std::format("Angle {} {} | {}", separator, numericComponent->value.GetArgument(), stateArgument.data()).data();
	}



	void RegisterConditions()
	{
		auto& registry = ModRegistry::GetSingleton();

		registry.AddConditionFactory("IsForm"sv, []() -> ICondition* { return new IsFormCondition(); });
		registry.AddConditionFactory("OR"sv, []() -> ICondition* { return new ORCondition(); });
		registry.AddConditionFactory("AND"sv, []() -> ICondition* { return new ANDCondition(); });
		registry.AddConditionFactory("IsEquipped"sv, []() -> ICondition* { return new IsEquippedCondition(); });
		registry.AddConditionFactory("IsEquippedType"sv, []() -> ICondition* { return new IsEquippedTypeCondition(); });
		registry.AddConditionFactory("IsEquippedHasKeyword"sv, []() -> ICondition* { return new IsEquippedHasKeywordCondition(); });
		registry.AddConditionFactory("IsEquippedPower"sv, []() -> ICondition* { return new IsEquippedPowerCondition(); });
		registry.AddConditionFactory("IsWorn"sv, []() -> ICondition* { return new IsWornCondition(); });
		registry.AddConditionFactory("IsWornHasKeyword"sv, []() -> ICondition* { return new IsWornHasKeywordCondition(); });
		registry.AddConditionFactory("IsWornNamed"sv, []() -> ICondition* { return new IsWornNamedCondition(); });
		registry.AddConditionFactory("IsWornFromPlugin"sv, []() -> ICondition* { return new IsWornFromPluginCondition(); });
		registry.AddConditionFactory("IsFemale"sv, []() -> ICondition* { return new IsFemaleCondition(); });
		registry.AddConditionFactory("IsDead"sv, []() -> ICondition* { return new IsDeadCondition(); });
		registry.AddConditionFactory("IsChild"sv, []() -> ICondition* { return new IsChildCondition(); });
		registry.AddConditionFactory("IsPlayerTeammate"sv, []() -> ICondition* { return new IsPlayerTeammateCondition(); });
		registry.AddConditionFactory("IsInInterior"sv, []() -> ICondition* { return new IsInInteriorCondition(); });
		registry.AddConditionFactory("IsInFaction"sv, []() -> ICondition* { return new IsInFactionCondition(); });
		registry.AddConditionFactory("HasKeyword"sv, []() -> ICondition* { return new HasKeywordCondition(); });
		registry.AddConditionFactory("HasMagicEffect"sv, []() -> ICondition* { return new HasMagicEffectCondition(); });
		registry.AddConditionFactory("HasMagicEffectWithKeyword"sv, []() -> ICondition* { return new HasMagicEffectWithKeywordCondition(); });
		registry.AddConditionFactory("HasPerk"sv, []() -> ICondition* { return new HasPerkCondition(); });
		registry.AddConditionFactory("HasSpell"sv, []() -> ICondition* { return new HasSpellCondition(); });
		registry.AddConditionFactory("CompareValues"sv, []() -> ICondition* { return new CompareValues(); });
		registry.AddConditionFactory("Level"sv, []() -> ICondition* { return new LevelCondition(); });
		registry.AddConditionFactory("IsActorBase"sv, []() -> ICondition* { return new IsActorBaseCondition(); });
		registry.AddConditionFactory("IsRace"sv, []() -> ICondition* { return new IsRaceCondition(); });
		registry.AddConditionFactory("CurrentWeather"sv, []() -> ICondition* { return new CurrentWeatherCondition(); });
		registry.AddConditionFactory("CurrentGameTime"sv, []() -> ICondition* { return new CurrentGameTimeCondition(); });
		registry.AddConditionFactory("Random"sv, []() -> ICondition* { return new RandomCondition(); });
		registry.AddConditionFactory("IsUnique"sv, []() -> ICondition* { return new IsUniqueCondition(); });
		registry.AddConditionFactory("IsClass"sv, []() -> ICondition* { return new IsClassCondition(); });
		registry.AddConditionFactory("IsCombatStyle"sv, []() -> ICondition* { return new IsCombatStyleCondition(); });
		registry.AddConditionFactory("IsVoiceType"sv, []() -> ICondition* { return new IsVoiceTypeCondition(); });
		registry.AddConditionFactory("IsAttacking"sv, []() -> ICondition* { return new IsAttackingCondition(); });
		registry.AddConditionFactory("IsRunning"sv, []() -> ICondition* { return new IsRunningCondition(); });
		registry.AddConditionFactory("IsSneaking"sv, []() -> ICondition* { return new IsSneakingCondition(); });
		registry.AddConditionFactory("IsSprinting"sv, []() -> ICondition* { return new IsSprintingCondition(); });
		registry.AddConditionFactory("IsInAir"sv, []() -> ICondition* { return new IsInAirCondition(); });
		registry.AddConditionFactory("IsInCombat"sv, []() -> ICondition* { return new IsInCombatCondition(); });
		registry.AddConditionFactory("IsWeaponDrawn"sv, []() -> ICondition* { return new IsWeaponDrawnCondition(); });
		registry.AddConditionFactory("IsInLocation"sv, []() -> ICondition* { return new IsInLocationCondition(); });
		registry.AddConditionFactory("HasRefType"sv, []() -> ICondition* { return new HasRefTypeCondition(); });
		registry.AddConditionFactory("IsParentCell"sv, []() -> ICondition* { return new IsParentCellCondition(); });
		registry.AddConditionFactory("IsWorldSpace"sv, []() -> ICondition* { return new IsWorldSpaceCondition(); });
		registry.AddConditionFactory("FactionRank"sv, []() -> ICondition* { return new FactionRankCondition(); });
		registry.AddConditionFactory("IsMovementDirection"sv, []() -> ICondition* { return new IsMovementDirectionCondition(); });
		registry.AddConditionFactory("IsEquippedShout"sv, []() -> ICondition* { return new IsEquippedShoutCondition(); });
		registry.AddConditionFactory("SubmergeLevel"sv, []() -> ICondition* { return new SubmergeLevelCondition(); });
		registry.AddConditionFactory("IsCurrentPackage"sv, []() -> ICondition* { return new IsCurrentPackageCondition(); });
		registry.AddConditionFactory("IsWornInSlotHasKeyword"sv, []() -> ICondition* { return new IsWornInSlotHasKeywordCondition(); });
		registry.AddConditionFactory("Scale"sv, []() -> ICondition* { return new ScaleCondition(); });
		registry.AddConditionFactory("Height"sv, []() -> ICondition* { return new HeightCondition(); });
		registry.AddConditionFactory("Weight"sv, []() -> ICondition* { return new WeightCondition(); });
		registry.AddConditionFactory("MovementSpeed"sv, []() -> ICondition* { return new MovementSpeedCondition(); });
		registry.AddConditionFactory("CurrentMovementSpeed"sv, []() -> ICondition* { return new CurrentMovementSpeedCondition(); });
		registry.AddConditionFactory("WindSpeed"sv, []() -> ICondition* { return new WindSpeedCondition(); });
		registry.AddConditionFactory("WindAngleDifference"sv, []() -> ICondition* { return new WindAngleDifferenceCondition(); });
		registry.AddConditionFactory("CrimeGold"sv, []() -> ICondition* { return new CrimeGoldCondition(); });
		registry.AddConditionFactory("IsBlocking"sv, []() -> ICondition* { return new IsBlockingCondition(); });
		registry.AddConditionFactory("IsCombatState"sv, []() -> ICondition* { return new IsCombatStateCondition(); });
		registry.AddConditionFactory("InventoryCount"sv, []() -> ICondition* { return new InventoryCountCondition(); });
		registry.AddConditionFactory("FallDistance"sv, []() -> ICondition* { return new FallDistanceCondition(); });
		registry.AddConditionFactory("FallDamage"sv, []() -> ICondition* { return new FallDamageCondition(); });
		registry.AddConditionFactory("CurrentPackageType"sv, []() -> ICondition* { return new CurrentPackageTypeCondition(); });
		registry.AddConditionFactory("IsOnMount"sv, []() -> ICondition* { return new IsOnMountCondition(); });
		registry.AddConditionFactory("IsRiding"sv, []() -> ICondition* { return new IsRidingCondition(); });
		registry.AddConditionFactory("IsRidingHasKeyword"sv, []() -> ICondition* { return new IsRidingHasKeywordCondition(); });
		registry.AddConditionFactory("CurrentFurniture"sv, []() -> ICondition* { return new CurrentFurnitureCondition(); });
		registry.AddConditionFactory("CurrentFurnitureHasKeyword"sv, []() -> ICondition* { return new CurrentFurnitureHasKeywordCondition(); });
		registry.AddConditionFactory("HasTarget"sv, []() -> ICondition* { return new HasTargetCondition(); });
		registry.AddConditionFactory("CurrentTargetDistance"sv, []() -> ICondition* { return new CurrentTargetDistanceCondition(); });
		registry.AddConditionFactory("CurrentTargetRelationship"sv, []() -> ICondition* { return new CurrentTargetRelationshipCondition(); });
		registry.AddConditionFactory("EquippedObjectWeight"sv, []() -> ICondition* { return new EquippedObjectWeightCondition(); });
		registry.AddConditionFactory("CurrentCastingType"sv, []() -> ICondition* { return new CurrentCastingTypeCondition(); });
		registry.AddConditionFactory("CurrentDeliveryType"sv, []() -> ICondition* { return new CurrentDeliveryTypeCondition(); });
		registry.AddConditionFactory("IsQuestStageDone"sv, []() -> ICondition* { return new IsQuestStageDoneCondition(); });
		registry.AddConditionFactory("CurrentWeatherHasFlag"sv, []() -> ICondition* { return new CurrentWeatherHasFlagCondition(); });
		registry.AddConditionFactory("InventoryCountHasKeyword"sv, []() -> ICondition* { return new InventoryCountHasKeywordCondition(); });
		registry.AddConditionFactory("CurrentTargetRelativeAngle"sv, []() -> ICondition* { return new CurrentTargetRelativeAngleCondition(); });
		registry.AddConditionFactory("CurrentTargetLineOfSight"sv, []() -> ICondition* { return new CurrentTargetLineOfSightCondition(); });
		registry.AddConditionFactory("CurrentRotationSpeed"sv, []() -> ICondition* { return new CurrentRotationSpeedCondition(); });
		registry.AddConditionFactory("IsTalking"sv, []() -> ICondition* { return new IsTalkingCondition(); });
		registry.AddConditionFactory("IsGreetingPlayer"sv, []() -> ICondition* { return new IsGreetingPlayerCondition(); });
		registry.AddConditionFactory("IsInScene"sv, []() -> ICondition* { return new IsInSceneCondition(); });
		registry.AddConditionFactory("IsInSpecifiedScene"sv, []() -> ICondition* { return new IsInSpecifiedSceneCondition(); });
		registry.AddConditionFactory("IsScenePlaying"sv, []() -> ICondition* { return new IsScenePlayingCondition(); });
		registry.AddConditionFactory("IsDoingFavor"sv, []() -> ICondition* { return new IsDoingFavorCondition(); });
		registry.AddConditionFactory("AttackState"sv, []() -> ICondition* { return new AttackStateCondition(); });
		registry.AddConditionFactory("IsMenuOpen"sv, []() -> ICondition* { return new IsMenuOpenCondition(); });
		registry.AddConditionFactory("HasMorph"sv, []() -> ICondition* { return new HasMorphCondition(); });
		registry.AddConditionFactory("IsAnimationPlaying"sv, []() -> ICondition* { return new IsAnimationPlayingCondition(); });
		registry.AddConditionFactory("IsBodyType"sv, []() -> ICondition* { return new SkinTypeCondition(Scan::SkinPart::kBody); });
		registry.AddConditionFactory("IsHeadType"sv, []() -> ICondition* { return new SkinTypeCondition(Scan::SkinPart::kHead); });
		registry.AddConditionFactory("IsBodyUVLayoutType"sv, []() -> ICondition* { return new UVLayoutCondition(Scan::SkinPart::kBody); });
		registry.AddConditionFactory("IsHeadUVLayoutType"sv, []() -> ICondition* { return new UVLayoutCondition(Scan::SkinPart::kHead); });
		registry.AddConditionFactory("TARGET"sv, []() -> ICondition* { return new TARGETCondition(); });
		registry.AddConditionFactory("PLAYER"sv, []() -> ICondition* { return new PLAYERCondition(); });
		registry.AddConditionFactory("LightLevel"sv, []() -> ICondition* { return new LightLevelCondition(); });
		registry.AddConditionFactory("LocationHasKeyword"sv, []() -> ICondition* { return new LocationHasKeywordCondition(); });
		registry.AddConditionFactory("LifeState"sv, []() -> ICondition* { return new LifeStateCondition(); });
		registry.AddConditionFactory("SitSleepState"sv, []() -> ICondition* { return new SitSleepStateCondition(); });
		registry.AddConditionFactory("XOR"sv, []() -> ICondition* { return new XORCondition(); });
		registry.AddConditionFactory("PRESET"sv, []() -> ICondition* { return new PRESETCondition(); });
		registry.AddConditionFactory("IsAttackTypeKeyword"sv, []() -> ICondition* { return new IsAttackTypeKeywordCondition(); });
		registry.AddConditionFactory("IsAttackTypeFlag"sv, []() -> ICondition* { return new IsAttackTypeFlagCondition(); });
		registry.AddConditionFactory("LocationCleared"sv, []() -> ICondition* { return new LocationClearedCondition(); });
		registry.AddConditionFactory("IsSummoned"sv, []() -> ICondition* { return new IsSummonedCondition(); });
		registry.AddConditionFactory("IsEquippedHasEnchantment"sv, []() -> ICondition* { return new IsEquippedHasEnchantmentCondition(); });
		registry.AddConditionFactory("IsEquippedHasEnchantmentWithKeyword"sv, []() -> ICondition* { return new IsEquippedHasEnchantmentWithKeywordCondition(); });
		registry.AddConditionFactory("IsOverEncumbered"sv, []() -> ICondition* { return new IsOverEncumberedCondition(); });
		registry.AddConditionFactory("IsTrespassing"sv, []() -> ICondition* { return new IsTrespassingCondition(); });
		registry.AddConditionFactory("IsGuard"sv, []() -> ICondition* { return new IsGuardCondition(); });
		registry.AddConditionFactory("IsCrimeSearching"sv, []() -> ICondition* { return new IsCrimeSearchingCondition(); });
		registry.AddConditionFactory("IsCombatSearching"sv, []() -> ICondition* { return new IsCombatSearchingCondition(); });
		registry.AddConditionFactory("IdleTime"sv, []() -> ICondition* { return new IdleTimeCondition(); });
		registry.AddConditionFactory("IsAboveWater"sv, []() -> ICondition* { return new IsAboveWaterCondition(); });
		registry.AddConditionFactory("MagicEffectElapsedTime"sv, []() -> ICondition* { return new MagicEffectElapsedTimeCondition(); });
		registry.AddConditionFactory("IsWornInSlot"sv, []() -> ICondition* { return new IsWornInSlotCondition(); });
		registry.AddConditionFactory("InventoryWeight"sv, []() -> ICondition* { return new InventoryWeightCondition(); });
		registry.AddConditionFactory("IsGhost"sv, []() -> ICondition* { return new IsGhostCondition(); });
		registry.AddConditionFactory("IsSwimming"sv, []() -> ICondition* { return new IsSwimmingCondition(); });
		registry.AddConditionFactory("IsStaggered"sv, []() -> ICondition* { return new IsStaggeredCondition(); });
		registry.AddConditionFactory("CastingSpell"sv, []() -> ICondition* { return new CastingSpellCondition(); });
		registry.AddConditionFactory("HasBoundWeaponEquipped"sv, []() -> ICondition* { return new HasBoundWeaponEquippedCondition(); });
		registry.AddConditionFactory("IsOnStairs"sv, []() -> ICondition* { return new IsOnStairsCondition(); });
		registry.AddConditionFactory("SurfaceMaterial"sv, []() -> ICondition* { return new SurfaceMaterialCondition(); });
		registry.AddConditionFactory("MovementSurfaceAngle"sv, []() -> ICondition* { return new MovementSurfaceAngleCondition(); });
	}

	namespace
	{
		constexpr std::array kCategories{ "Structure"sv, "Identity"sv, "State"sv, "Magic"sv, "Equipment and inventory"sv, "World"sv, "Target and mount"sv, "Values and packages"sv, "Other"sv };

		// A plugin's conditions, listed under the plugin's name: by condition name, never removed
		std::mutex g_pluginCategoryLock;
		std::unordered_map<std::string, std::string> g_pluginCategories;

		const std::unordered_map<std::string_view, std::string_view>& CategoryTable()
		{
			static const std::unordered_map<std::string_view, std::string_view> table = [] {
				std::unordered_map<std::string_view, std::string_view> out;
				const auto put = [&](std::string_view a_category, std::initializer_list<std::string_view> a_names) {
					for (const auto name : a_names) {
						out.emplace(name, a_category);
					}
				};
				put(kCategories[0], { "AND", "OR", "XOR", "PRESET", "TARGET", "PLAYER" });
				put(kCategories[1], { "IsForm", "IsActorBase", "IsRace", "IsClass", "IsCombatStyle", "IsVoiceType", "IsUnique", "IsFemale", "IsChild", "IsGuard", "IsSummoned", "IsGhost", "HasKeyword",
										"IsInFaction", "FactionRank", "Level", "HasPerk", "HasSpell", "Scale", "Height", "Weight", "HasMorph", "IsBodyType", "IsHeadType", "IsBodyUVLayoutType", "IsHeadUVLayoutType" });
				put(kCategories[2], { "IsDead", "IsAttacking", "AttackState", "IsBlocking", "IsStaggered", "IsRunning", "IsSneaking", "IsSprinting", "IsSwimming", "IsInAir", "IsInCombat", "IsCombatState",
										"IsWeaponDrawn", "IsTalking", "IsGreetingPlayer", "IsDoingFavor", "IsInScene", "IsInSpecifiedScene", "IsScenePlaying", "LifeState", "SitSleepState", "IdleTime",
										"IsMovementDirection", "MovementSpeed", "CurrentMovementSpeed", "CurrentRotationSpeed", "IsPlayerTeammate", "IsTrespassing", "IsCrimeSearching", "IsCombatSearching",
										"CrimeGold", "IsOverEncumbered", "IsAnimationPlaying" });
				put(kCategories[3], { "HasMagicEffect", "HasMagicEffectWithKeyword", "MagicEffectElapsedTime", "CastingSpell", "CurrentCastingType", "CurrentDeliveryType" });
				put(kCategories[4], { "IsEquipped", "IsEquippedType", "IsEquippedHasKeyword", "IsEquippedPower", "IsEquippedShout", "IsEquippedHasEnchantment", "IsEquippedHasEnchantmentWithKeyword",
										"HasBoundWeaponEquipped", "IsWorn", "IsWornHasKeyword", "IsWornNamed", "IsWornFromPlugin", "IsWornInSlot", "IsWornInSlotHasKeyword", "EquippedObjectWeight", "InventoryCount", "InventoryCountHasKeyword",
										"InventoryWeight", "IsAttackTypeKeyword", "IsAttackTypeFlag" });
				put(kCategories[5], { "IsInInterior", "IsInLocation", "LocationHasKeyword", "LocationCleared", "IsParentCell", "IsWorldSpace", "HasRefType", "CurrentWeather", "CurrentWeatherHasFlag",
										"WindSpeed", "WindAngleDifference", "CurrentGameTime", "LightLevel", "IsAboveWater", "SubmergeLevel", "SurfaceMaterial", "IsOnStairs", "MovementSurfaceAngle",
										"FallDistance", "FallDamage", "IsMenuOpen" });
				put(kCategories[6], { "HasTarget", "CurrentTargetDistance", "CurrentTargetRelationship", "CurrentTargetRelativeAngle", "CurrentTargetLineOfSight", "IsOnMount", "IsRiding",
										"IsRidingHasKeyword", "CurrentFurniture", "CurrentFurnitureHasKeyword" });
				put(kCategories[7], { "CompareValues", "Random", "IsQuestStageDone", "IsCurrentPackage", "CurrentPackageType" });
				return out;
			}();
			return table;
		}
	}

	std::string_view CategoryOf(std::string_view a_conditionName)
	{
		const auto& table = CategoryTable();
		if (const auto it = table.find(a_conditionName); it != table.end()) {
			return it->second;
		}
		std::scoped_lock lock(g_pluginCategoryLock);
		if (const auto it = g_pluginCategories.find(std::string(a_conditionName)); it != g_pluginCategories.end()) {
			return it->second;
		}
		return kCategories.back();
	}

	int CategoryRank(std::string_view a_category)
	{
		// The built-in categories in their order, then each plugin's, then Other
		if (a_category == kCategories.back()) {
			return static_cast<int>(kCategories.size());
		}
		const auto it = std::ranges::find(kCategories, a_category);
		return static_cast<int>(it - kCategories.begin()) - (it == kCategories.end() ? 1 : 0);
	}

	std::unique_ptr<ICondition> CreateCondition(std::string_view a_conditionName)
	{
		if (auto condition = ModRegistry::GetSingleton().CreateCondition(a_conditionName)) {
			condition->PreInitialize();
			condition->PostInitialize();
			return std::move(condition);
		}

		return std::make_unique<InvalidCondition>(a_conditionName);
	}

	std::unique_ptr<ICondition> DuplicateCondition(const std::unique_ptr<ICondition>& a_conditionToDuplicate)
	{
		// serialize condition to json
		rapidjson::Document doc(rapidjson::kObjectType);

		rapidjson::Value serializedCondition(rapidjson::kObjectType);
		a_conditionToDuplicate->Serialize(&serializedCondition, &doc.GetAllocator());

		// create new condition from the serialized json
		return CreateConditionFromJson(serializedCondition);
	}

	std::unique_ptr<ConditionSet> DuplicateConditionSet(ConditionSet* a_conditionSetToDuplicate)
	{
		// serialize the condition set to json
		rapidjson::Document doc(rapidjson::kObjectType);

		rapidjson::Value serializedConditionSet = a_conditionSetToDuplicate->Serialize(doc.GetAllocator());

		// create new conditions from the serialized json
		auto newConditionSet = std::make_unique<ConditionSet>();

		for (auto& conditionValue : serializedConditionSet.GetArray()) {
			if (auto condition = CreateConditionFromJson(conditionValue)) {
				newConditionSet->Add(condition);
			}
		}

		return newConditionSet;
	}

	std::unique_ptr<ICondition> ConvertDeprecatedCondition(std::unique_ptr<ICondition>& a_deprecatedCondition, std::string_view a_conditionName, rapidjson::Value& a_value)
	{
		rapidjson::Document doc(rapidjson::kObjectType);
		const auto object = a_value.GetObj();

		if (a_conditionName == "CurrentTarget"sv || a_conditionName == "CurrentTargetFactionRank"sv || a_conditionName == "CurrentTargetHasKeyword"sv) {
			if (auto newCondition = ModRegistry::GetSingleton().CreateCondition("TARGET"sv)) {
				newCondition->PreInitialize();

				const auto targetCondition = static_cast<TARGETCondition*>(newCondition.get());

				// disabled
				bool bDisabled = false;
				if (const auto disabledIt = object.FindMember("disabled"); disabledIt != object.MemberEnd() && disabledIt->value.IsBool()) {
					bDisabled = disabledIt->value.GetBool();
				}

				// negated
				bool bNegated = false;
				if (const auto negatedIt = object.FindMember("negated"); negatedIt != object.MemberEnd() && negatedIt->value.IsBool()) {
					bNegated = negatedIt->value.GetBool();
				}

				rapidjson::Value targetConditionValues(rapidjson::kObjectType);

				// Target condition values
				if (const auto targetTypeIt = object.FindMember("Target type"); targetTypeIt != object.MemberEnd()) {
					targetConditionValues.AddMember("Target type", targetTypeIt->value, doc.GetAllocator());
				}
				if (bDisabled) {
					targetConditionValues.AddMember("disabled", bDisabled, doc.GetAllocator());
				}

				targetCondition->Initialize(&targetConditionValues);

				rapidjson::Value serializedCondition(rapidjson::kObjectType);
				if (bNegated) {
					serializedCondition.AddMember("negated", bNegated, doc.GetAllocator());
				}

				if (a_conditionName == "CurrentTarget"sv) {
					// Base
					bool bBase = false;
					if (const auto baseIt = object.FindMember("Base"); baseIt != object.MemberEnd() && baseIt->value.IsBool()) {
						bBase = baseIt->value.GetBool();
					}

					if (bBase) {
						auto actorBaseCondition = ModRegistry::GetSingleton().CreateCondition("IsActorBase"sv);

						if (const auto formIt = object.FindMember("Form"); formIt != object.MemberEnd()) {
							serializedCondition.AddMember("Actor base", formIt->value, doc.GetAllocator());
						}

						actorBaseCondition->PreInitialize();
						actorBaseCondition->Initialize(&serializedCondition);
						actorBaseCondition->PostInitialize();

						targetCondition->conditionsComponent->conditionSet->Add(actorBaseCondition);
					} else {
						auto isFormCondition = ModRegistry::GetSingleton().CreateCondition("IsForm"sv);

						if (const auto formIt = object.FindMember("Form"); formIt != object.MemberEnd()) {
							serializedCondition.AddMember("Form", formIt->value, doc.GetAllocator());
						}

						isFormCondition->PreInitialize();
						isFormCondition->Initialize(&serializedCondition);
						isFormCondition->PostInitialize();

						targetCondition->conditionsComponent->conditionSet->Add(isFormCondition);
					}
				} else if (a_conditionName == "CurrentTargetFactionRank"sv) {
					auto factionRankCondition = ModRegistry::GetSingleton().CreateCondition("FactionRank"sv);

					if (const auto factionIt = object.FindMember("Faction"); factionIt != object.MemberEnd()) {
						serializedCondition.AddMember("Faction", factionIt->value, doc.GetAllocator());
					}

					if (const auto comparisonIt = object.FindMember("Comparison"); comparisonIt != object.MemberEnd()) {
						serializedCondition.AddMember("Comparison", comparisonIt->value, doc.GetAllocator());
					}

					if (const auto numericValueIt = object.FindMember("Numeric value"); numericValueIt != object.MemberEnd()) {
						serializedCondition.AddMember("Numeric value", numericValueIt->value, doc.GetAllocator());
					}

					factionRankCondition->PreInitialize();
					factionRankCondition->Initialize(&serializedCondition);
					factionRankCondition->PostInitialize();

					targetCondition->conditionsComponent->conditionSet->Add(factionRankCondition);
				} else if (a_conditionName == "CurrentTargetHasKeyword"sv) {
					auto hasKeywordCondition = ModRegistry::GetSingleton().CreateCondition("HasKeyword"sv);

					if (const auto keywordIt = object.FindMember("Keyword"); keywordIt != object.MemberEnd()) {
						serializedCondition.AddMember("Keyword", keywordIt->value, doc.GetAllocator());
					}

					hasKeywordCondition->PreInitialize();
					hasKeywordCondition->Initialize(&serializedCondition);
					hasKeywordCondition->PostInitialize();

					targetCondition->conditionsComponent->conditionSet->Add(hasKeywordCondition);
				}

				newCondition->PostInitialize();

				return newCondition;
			}
		}

		return std::move(a_deprecatedCondition);
	}

	void InvalidNonEssentialCondition::Serialize(void* a_value, void* a_allocator)
	{
		auto& value = *static_cast<rapidjson::Value*>(a_value);
		auto& allocator = *static_cast<rapidjson::Document::AllocatorType*>(a_allocator);

		// just serialize back the json that was read
		value.CopyFrom(_json, allocator);
	}

	RE::BSString InvalidNonEssentialCondition::GetName() const
	{
		std::string ret = "[Missing] " + _name;
		return ret.data();
	}

	RE::BSString InvalidNonEssentialCondition::GetDescription() const
	{
		switch (GetEssential()) {
		case EssentialState::kNonEssential_True:
			return "The condition was not found, however it was marked as non-essential by the replacer mod author. It will be treated as if it evaluated to true."sv.data();
		case EssentialState::kNonEssential_False:
			return "The condition was not found, however it was marked as non-essential by the replacer mod author. It will be treated as if it evaluated to false."sv.data();
		}
		return "The condition was not found!"sv.data();
	}





	void IsEquippedCondition::InitializeLegacy(const char* a_argument)
	{
		formComponent->form.ParseLegacy(a_argument);
	}

	RE::BSString IsEquippedCondition::GetArgument() const
	{
		const auto formArgument = formComponent->GetArgument();
		return std::format("{} in {} hand", formArgument.data(), boolComponent->GetBoolValue() ? "left"sv : "right"sv).data();
	}



	RE::TESForm* IsEquippedCondition::GetEquippedForm(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->GetEquippedObject(boolComponent->GetBoolValue());
			}
		}

		return nullptr;
	}

	void IsEquippedTypeCondition::InitializeLegacy(const char* a_argument)
	{
		numericComponent->value.ParseLegacy(a_argument);
	}

	void IsEquippedTypeCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		numericComponent->value.getEnumMap = &IsEquippedTypeCondition::GetEnumMap;
	}

	RE::BSString IsEquippedTypeCondition::GetArgument() const
	{
		const auto numericArgument = numericComponent->GetArgument();
		return std::format("{} in {} hand", numericArgument.data(), boolComponent->GetBoolValue() ? "left"sv : "right"sv).data();
	}



	RE::TESForm* IsEquippedTypeCondition::GetEquippedForm(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->GetEquippedObject(boolComponent->GetBoolValue());
			}
		}

		return nullptr;
	}

	int8_t IsEquippedTypeCondition::GetEquippedType(RE::TESObjectREFR* a_refr) const
	{
		int8_t currentType = -1;

		if (const auto equippedForm = GetEquippedForm(a_refr)) {
			switch (*equippedForm->formType) {
			case RE::FormType::Weapon:
				if (const auto equippedWeapon = equippedForm->As<RE::TESObjectWEAP>()) {
					switch (equippedWeapon->GetWeaponType()) {
					case RE::WEAPON_TYPE::kHandToHandMelee:
						currentType = 0;
						break;
					case RE::WEAPON_TYPE::kOneHandSword:
						currentType = 1;
						break;
					case RE::WEAPON_TYPE::kOneHandDagger:
						currentType = 2;
						break;
					case RE::WEAPON_TYPE::kOneHandAxe:
						currentType = 3;
						break;
					case RE::WEAPON_TYPE::kOneHandMace:
						currentType = 4;
						break;
					case RE::WEAPON_TYPE::kTwoHandSword:
						currentType = 5;
						break;
					case RE::WEAPON_TYPE::kTwoHandAxe:
						if (!ModRegistry::bKeywordsLoaded) {
							ModRegistry::GetSingleton().LoadKeywords();
						}
						/*if (equippedWeapon->HasKeyword(ModRegistry::kywd_weapTypeBattleaxe)) {
                        currentType = 6;
                    }*/
						if (equippedWeapon->HasKeyword(ModRegistry::kywd_weapTypeWarhammer)) {
							currentType = 10;
						} else {
							// just fall back to battleaxe
							currentType = 6;
						}
						break;
					case RE::WEAPON_TYPE::kBow:
						currentType = 7;
						break;
					case RE::WEAPON_TYPE::kStaff:
						currentType = 8;
						break;
					case RE::WEAPON_TYPE::kCrossbow:
						currentType = 9;
						break;
					}
				}
				break;
			case RE::FormType::Armor:
				if (auto equippedShield = equippedForm->As<RE::TESObjectARMO>()) {
					currentType = 11;
				}
				break;
			case RE::FormType::Spell:
				if (const auto equippedSpell = equippedForm->As<RE::SpellItem>()) {
					const RE::ActorValue associatedSkill = equippedSpell->GetAssociatedSkill();
					switch (associatedSkill) {
					case RE::ActorValue::kAlteration:
						currentType = 12;
						break;
					case RE::ActorValue::kIllusion:
						currentType = 13;
						break;
					case RE::ActorValue::kDestruction:
						currentType = 14;
						break;
					case RE::ActorValue::kConjuration:
						currentType = 15;
						break;
					case RE::ActorValue::kRestoration:
						currentType = 16;
						break;
					}
				}
				break;
			case RE::FormType::Scroll:
				if (auto equippedScroll = equippedForm->As<RE::ScrollItem>()) {
					currentType = 17;
				}
				break;
			case RE::FormType::Light:
				if (auto equippedTorch = equippedForm->As<RE::TESObjectLIGH>()) {
					currentType = 18;
				}
				break;
			}
		} else {
			// nothing equipped
			currentType = 0;
		}

		return currentType;
	}

	const std::map<int32_t, std::string_view>& IsEquippedTypeCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[-1] = "Other"sv;
			enumMap[0] = "Unarmed"sv;
			enumMap[1] = "Sword"sv;
			enumMap[2] = "Dagger"sv;
			enumMap[3] = "War Axe"sv;
			enumMap[4] = "Mace"sv;
			enumMap[5] = "Greatsword"sv;
			enumMap[6] = "Battleaxe"sv;
			enumMap[7] = "Bow"sv;
			enumMap[8] = "Staff"sv;
			enumMap[9] = "Crossbow"sv;
			enumMap[10] = "Warhammer"sv;
			enumMap[11] = "Shield"sv;
			enumMap[12] = "Alteration Spell"sv;
			enumMap[13] = "Illusion Spell"sv;
			enumMap[14] = "Destruction Spell"sv;
			enumMap[15] = "Conjuration Spell"sv;
			enumMap[16] = "Restoration Spell"sv;
			enumMap[17] = "Scroll"sv;
			enumMap[18] = "Torch"sv;
			return enumMap;
		}();

		return map;
	}

	void IsEquippedHasKeywordCondition::InitializeLegacy(const char* a_argument)
	{
		keywordComponent->keyword.ParseLegacy(a_argument);
	}

	RE::BSString IsEquippedHasKeywordCondition::GetArgument() const
	{
		const auto keywordArgument = keywordComponent->GetArgument();
		return std::format("{} in {} hand", keywordArgument.data(), boolComponent->GetBoolValue() ? "left"sv : "right"sv).data();
	}



	RE::BGSKeywordForm* IsEquippedHasKeywordCondition::GetEquippedKeywordForm(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto equippedForm = actor->GetEquippedObject(boolComponent->GetBoolValue())) {
					return equippedForm->As<RE::BGSKeywordForm>();
				}
			}
		}

		return nullptr;
	}



	RE::TESForm* IsEquippedPowerCondition::GetEquippedPower(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->GetActorRuntimeData().selectedPower;
			}
		}

		return nullptr;
	}


	ResourceCondition::ResourceCondition(std::shared_ptr<const Definition> a_definition) :
		_definition(std::move(a_definition))
	{
		using Test = DCMF_API::Resources::ConditionTest;
		const auto* operand = _definition->operandName.empty() ? "Value" : _definition->operandName.c_str();
		switch (_definition->test) {
		case Test::Compare:
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>(operand);
			break;
		case Test::HasForm:
			formComponent = AddComponent<FormConditionComponent>(operand, "", _definition->formType);
			break;
		case Test::HasText:
			textComponent = AddComponent<TextConditionComponent>(operand);
			textComponent->SetAllowSpaces(true);
			break;
		case Test::IsTrue:
			break;
		}
	}

	RE::BSString ResourceCondition::GetArgument() const
	{
		if (comparisonComponent) {
			return std::format("{} {} {}", _definition->name, ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator), numericComponent->value.GetArgument()).data();
		}
		if (formComponent) {
			return formComponent->GetArgument();
		}
		if (textComponent) {
			return textComponent->GetArgument();
		}
		return ""sv;
	}



	bool AddResourceCondition(ResourceCondition::Definition a_definition)
	{
		auto& registry = ModRegistry::GetSingleton();
		if (a_definition.name.empty() || registry.HasConditionFactory(a_definition.name)) {
			return false;
		}
		const auto name = a_definition.name;
		{
			std::scoped_lock lock(g_pluginCategoryLock);
			g_pluginCategories.emplace(name, a_definition.plugin);
		}
		registry.AddPluginConditionFactory(name, [definition = std::make_shared<const ResourceCondition::Definition>(std::move(a_definition))]() -> ICondition* {
			return new ResourceCondition(definition);
		});
		return true;
	}










	void HasMagicEffectCondition::InitializeLegacy(const char* a_argument)
	{
		formComponent->form.ParseLegacy(a_argument);
		boolComponent->SetBoolValue(false);
	}

	RE::BSString HasMagicEffectCondition::GetArgument() const
	{
		auto ret = formComponent->form.GetArgument();
		if (boolComponent->GetBoolValue()) {
			ret.append(" | Active Only"sv);
		}
		return ret.data();
	}


	void HasMagicEffectWithKeywordCondition::InitializeLegacy(const char* a_argument)
	{
		keywordComponent->keyword.ParseLegacy(a_argument);
		boolComponent->SetBoolValue(false);
	}

	RE::BSString HasMagicEffectWithKeywordCondition::GetArgument() const
	{
		auto ret = keywordComponent->keyword.GetArgument();
		if (boolComponent->GetBoolValue()) {
			ret.append(" | Active Only"sv);
		}
		return ret.data();
	}




	void CompareValues::InitializeLegacy(const char* a_argument)
	{
		std::string_view argument(a_argument);
		if (const size_t splitPos = argument.find(','); splitPos != std::string_view::npos) {
			const std::string_view firstArg = Utils::TrimWhitespace(argument.substr(0, splitPos));
			const std::string_view secondArg = Utils::TrimWhitespace(argument.substr(splitPos + 1));

			const bool bIsActorValue = numericComponentA->value.GetType() == Components::NumericValue::Type::kActorValue;
			numericComponentA->value.ParseLegacy(firstArg, bIsActorValue);
			numericComponentB->value.ParseLegacy(secondArg);
		} else {
			logger::error("Invalid argument: {}", argument);
		}
	}

	RE::BSString CompareValues::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("{} {} {}", numericComponentA->value.GetArgument(), separator, numericComponentB->value.GetArgument()).data();
	}



	void LevelCondition::InitializeLegacy(const char* a_argument)
	{
		numericComponent->value.ParseLegacy(a_argument);
	}

	RE::BSString LevelCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Level {} {}", separator, numericComponent->value.GetArgument()).data();
	}





	RE::TESNPC* IsActorBaseCondition::GetActorBase(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return Utils::GetActorBase(actor);
			}
		}

		return nullptr;
	}



	RE::TESRace* IsRaceCondition::GetRace(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->GetRace();
			}
		}

		return nullptr;
	}



	RE::BSString CurrentGameTimeCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Time {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	float CurrentGameTimeCondition::GetHours()
	{
		const float time = GetCurrentGameTime();

		float days;
		return modff(time, &days) * 24.f;
	}

	void RandomCondition::Initialize(void* a_value)
	{
		ConditionBase::Initialize(a_value);

		auto& value = *static_cast<rapidjson::Value*>(a_value);
		const auto object = value.GetObj();

		// backwards compatibility with saved pre 2.3.0 random conditions
		if (const auto randomIt = object.FindMember(rapidjson::StringRef("Random value")); randomIt != object.MemberEnd() && randomIt->value.IsObject()) {
			const auto randomObj = randomIt->value.GetObj();

			if (const auto minIt = randomObj.FindMember("min"); minIt != randomObj.MemberEnd() && minIt->value.IsNumber()) {
				minRandomComponent->SetStaticValue(minIt->value.GetFloat());
			}

			if (const auto maxIt = randomObj.FindMember("max"); maxIt != randomObj.MemberEnd() && maxIt->value.IsNumber()) {
				maxRandomComponent->SetStaticValue(maxIt->value.GetFloat());
			}
		}
	}

	void RandomCondition::InitializeLegacy(const char* a_argument)
	{
		numericComponent->value.ParseLegacy(a_argument);
		comparisonComponent->SetComparisonOperator(ComparisonOperator::kLessEqual);
		stateComponent->SetShouldResetOnLoopOrEcho(false);
		minRandomComponent->SetStaticValue(0.f);
		maxRandomComponent->SetStaticValue(1.f);
	}

	RE::BSString RandomCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		const auto stateArgument = stateComponent->GetArgument();

		return std::format("Random [{}, {}] {} {} | {}", minRandomComponent->value.GetArgument(), maxRandomComponent->value.GetArgument(), separator, numericComponent->value.GetArgument(), stateArgument.data()).data();
	}





	RE::TESClass* IsClassCondition::GetTESClass(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto actorBase = actor->GetActorBase()) {
					return actorBase->npcClass;
				}
			}
		}

		return nullptr;
	}



	RE::TESCombatStyle* IsCombatStyleCondition::GetCombatStyle(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto actorBase = actor->GetActorBase()) {
					return actorBase->GetCombatStyle();
				}
			}
		}

		return nullptr;
	}



	RE::BGSVoiceType* IsVoiceTypeCondition::GetVoiceType(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto actorBase = actor->GetActorBase()) {
					return actorBase->GetVoiceType();
				}
			}
		}

		return nullptr;
	}










	bool IsInLocationCondition::IsLocation(RE::BGSLocation* a_location, RE::BGSLocation* a_locationToCompare)
	{
		if (a_location == a_locationToCompare) {
			return true;
		}

		if (a_location->parentLoc) {
			return IsLocation(a_location->parentLoc, a_locationToCompare);
		}

		return false;
	}

	void HasRefTypeCondition::InitializeLegacy(const char* a_argument)
	{
		locRefTypeComponent->locRefType.ParseLegacy(a_argument);
	}

	RE::BSString HasRefTypeCondition::GetArgument() const
	{
		return locRefTypeComponent->GetArgument();
	}







	void FactionRankCondition::InitializeLegacy(const char* a_argument)
	{
		std::string_view argument(a_argument);
		if (const size_t splitPos = argument.find(','); splitPos != std::string_view::npos) {
			const std::string_view firstArg = Utils::TrimWhitespace(argument.substr(0, splitPos));
			const std::string_view secondArg = Utils::TrimWhitespace(argument.substr(splitPos + 1));

			numericComponent->value.ParseLegacy(firstArg);
			factionComponent->form.ParseLegacy(secondArg);
		} else {
			logger::error("Invalid argument: {}", argument);
		}
	}

	RE::BSString FactionRankCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("{} rank {} {}", factionComponent->form.GetArgument(), separator, numericComponent->value.GetArgument()).data();
	}



	int32_t FactionRankCondition::GetFactionRank(RE::TESObjectREFR* a_refr) const
	{
		int32_t rank = -2;

		if (factionComponent->IsValid() && a_refr) {
			if (auto faction = FormAs<RE::TESFaction>(factionComponent->GetTESFormValue(a_refr))) {
				if (auto actor = a_refr->As<RE::Actor>()) {
					bool bIsPlayer = actor->IsPlayerRef();

					if (actor->IsInFaction(faction)) {
						rank = Actor_GetFactionRank(actor, faction, bIsPlayer);
					}
				}
			}
		}

		return rank;
	}

	void IsMovementDirectionCondition::InitializeLegacy(const char* a_argument)
	{
		numericComponent->value.ParseLegacy(a_argument);
	}

	void IsMovementDirectionCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		numericComponent->value.getEnumMap = &IsMovementDirectionCondition::GetEnumMap;
	}

	RE::BSString IsMovementDirectionCondition::GetArgument() const
	{
		return numericComponent->GetArgument();
	}


	const std::map<int32_t, std::string_view>& IsMovementDirectionCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "None"sv;
			enumMap[1] = "Forward"sv;
			enumMap[2] = "Right"sv;
			enumMap[3] = "Back"sv;
			enumMap[4] = "Left"sv;
			return enumMap;
		}();

		return map;
	}



	RE::TESShout* IsEquippedShoutCondition::GetEquippedShout(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (auto actor = a_refr->As<RE::Actor>()) {
				return Actor_GetEquippedShout(actor);
			}
		}

		return nullptr;
	}

	RE::BSString SubmergeLevelCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Submerge level {} {}", separator, numericComponent->value.GetArgument()).data();
	}





	RE::TESPackage* IsCurrentPackageCondition::GetCurrentPackage(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->GetCurrentPackage();
			}
		}

		return nullptr;
	}

	void IsWornInSlotHasKeywordCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		slotComponent->value.getEnumMap = &IsWornInSlotHasKeywordCondition::GetEnumMap;
	}

	RE::BSString IsWornInSlotHasKeywordCondition::GetArgument() const
	{
		std::string slotName = "(Invalid)";
		const auto slot = static_cast<uint32_t>(slotComponent->GetNumericValue(nullptr));

		const auto& map = GetEnumMap();
		if (const auto it = map.find(slot); it != map.end()) {
			slotName = it->second;
		}

		return std::format("{}, {}", slotName, keywordComponent->keyword.GetArgument()).data();
	}


	const std::map<int32_t, std::string_view>& IsWornInSlotHasKeywordCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;

			enumMap[0] = "Head"sv;
			enumMap[1] = "Hair"sv;
			enumMap[2] = "Body"sv;
			enumMap[3] = "Hands"sv;
			enumMap[4] = "Forearms"sv;
			enumMap[5] = "Amulet"sv;
			enumMap[6] = "Ring"sv;
			enumMap[7] = "Feet"sv;
			enumMap[8] = "Calves"sv;
			enumMap[9] = "Shield"sv;
			enumMap[10] = "Tail"sv;
			enumMap[11] = "LongHair"sv;
			enumMap[12] = "Circlet"sv;
			enumMap[13] = "Ears"sv;
			enumMap[14] = "ModMouth"sv;
			enumMap[15] = "ModNeck"sv;
			enumMap[16] = "ModChestPrimary"sv;
			enumMap[17] = "ModBack"sv;
			enumMap[18] = "ModMisc1"sv;
			enumMap[19] = "ModPelvisPrimary"sv;
			enumMap[20] = "DecapitateHead"sv;
			enumMap[21] = "Decapitate"sv;
			enumMap[22] = "ModPelvisSecondary"sv;
			enumMap[23] = "ModLegRight"sv;
			enumMap[24] = "ModLegLeft"sv;
			enumMap[25] = "ModFaceJewelry"sv;
			enumMap[26] = "ModChestSecondary"sv;
			enumMap[27] = "ModShoulder"sv;
			enumMap[28] = "ModArmLeft"sv;
			enumMap[29] = "ModArmRight"sv;
			enumMap[30] = "ModMisc2"sv;
			enumMap[31] = "FX01"sv;

			return enumMap;
		}();

		return map;
	}

	RE::BSString ScaleCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Scale {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	RE::BSString HeightCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		return std::format("Height {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	RE::BSString WeightCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		return std::format("Weight {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	void MovementSpeedCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		movementTypeComponent->value.getEnumMap = &MovementSpeedCondition::GetEnumMap;
	}

	RE::BSString MovementSpeedCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		std::string movementSpeedTypeName = "(Invalid)";
		const auto movementType = static_cast<uint32_t>(movementTypeComponent->GetNumericValue(nullptr));

		const auto& map = GetEnumMap();
		if (const auto it = map.find(movementType); it != map.end()) {
			movementSpeedTypeName = it->second;
		}

		return std::format("{} speed {} {}", movementSpeedTypeName, separator, numericComponent->value.GetArgument()).data();
	}



	float MovementSpeedCondition::GetMovementSpeed(RE::Actor* a_actor) const
	{
		if (a_actor) {
			const auto movementType = static_cast<int8_t>(movementTypeComponent->GetNumericValue(a_actor));
			switch (movementType) {
			case 0:
				return a_actor->GetRunSpeed();
			case 1:
				return a_actor->GetJogSpeed();
			case 2:
				return a_actor->GetFastWalkSpeed();
			case 3:
				return a_actor->GetWalkSpeed();
			}
		}

		return 0.f;
	}

	const std::map<int32_t, std::string_view>& MovementSpeedCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Run"sv;
			enumMap[1] = "Jog"sv;
			enumMap[2] = "Fast walk"sv;
			enumMap[3] = "Walk"sv;
			return enumMap;
		}();

		return map;
	}

	RE::BSString CurrentMovementSpeedCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Movement speed {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	RE::BSString WindSpeedCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Wind speed {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	float WindSpeedCondition::GetWindSpeed()
	{
		if (const auto tes = RE::TES::GetSingleton()) {
			if (const auto sky = tes->sky) {
				return sky->windSpeed;
			}
		}

		return 0.f;
	}

	RE::BSString WindAngleDifferenceCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Angles difference {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	float WindAngleDifferenceCondition::GetWindAngleDifference(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto tes = RE::TES::GetSingleton()) {
				if (const auto sky = tes->sky) {
					const auto windAngle = sky->windAngle;
					const auto refAngle = a_refr->GetAngleZ();

					auto ret = Utils::NormalRelativeAngle(refAngle - windAngle);
					if (degreesComponent->GetBoolValue()) {
						ret = RE::rad_to_deg(ret);
					}
					if (absoluteComponent->GetBoolValue()) {
						ret = std::fabs(ret);
					}

					return ret;
				}
			}
		}

		return 0.f;
	}

	RE::BSString CrimeGoldCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		const auto factionArgument = factionComponent->form.GetArgument();
		return std::format("Crime gold in {} {} {}", factionArgument, separator, numericComponent->value.GetArgument()).data();
	}




	void IsCombatStateCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		combatStateComponent->value.getEnumMap = &IsCombatStateCondition::GetEnumMap;
	}

	RE::BSString IsCombatStateCondition::GetArgument() const
	{
		const auto combatState = static_cast<RE::ACTOR_COMBAT_STATE>(combatStateComponent->GetNumericValue(nullptr));

		return GetCombatStateName(combatState);
	}



	RE::ACTOR_COMBAT_STATE IsCombatStateCondition::GetCombatState(RE::Actor* a_actor) const
	{
		if (a_actor) {
			return Actor_GetCombatState(a_actor);
		}

		return RE::ACTOR_COMBAT_STATE::kNone;
	}

	std::string_view IsCombatStateCondition::GetCombatStateName(RE::ACTOR_COMBAT_STATE a_state) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_state)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& IsCombatStateCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Not in combat"sv;
			enumMap[1] = "In combat "sv;
			enumMap[2] = "Searching"sv;

			return enumMap;
		}();

		return map;
	}

	RE::BSString InventoryCountCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		const auto formArgument = formComponent->form.GetArgument();
		return std::format("{} count {} {}", formArgument, separator, numericComponent->value.GetArgument()).data();
	}



	int InventoryCountCondition::GetItemCount(RE::TESObjectREFR* a_refr) const
	{
		int count = 0;

		if (formComponent->IsValid() && a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				const auto wanted = FormIdOf(formComponent->GetTESFormValue(a_refr));
				const auto inv = actor->GetInventory([wanted](const RE::TESBoundObject& a_object) {
					return a_object.GetFormID() == wanted;
				});

				for (const auto& invData : inv | std::views::values) {
					const auto& [itemCount, entry] = invData;
					count += itemCount;
				}
			}
		}

		return count;
	}

	RE::BSString FallDistanceCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Fall distance {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	float FallDistanceCondition::GetFallDistance(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (auto charController = actor->GetCharController()) {
					if (charController->fallTime > 0.f) {
						return bhkCharacterController_CalcFallDistance(charController);
					}
				}
			}
		}

		return 0.f;
	}

	RE::BSString FallDamageCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Fall damage {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	float FallDamageCondition::GetFallDamage(RE::TESObjectREFR* a_refr)
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (auto charController = actor->GetCharController()) {
					if (charController->fallTime > 0.f) {
						float fallDistance = bhkCharacterController_CalcFallDistance(charController);
						return TESObjectREFR_CalcFallDamage(a_refr, fallDistance, 0.25f);  // The game seems to use a 0.25f mult
					}
				}
			}
		}

		return 0.f;
	}

	void CurrentPackageTypeCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		packageTypeComponent->value.getEnumMap = &CurrentPackageTypeCondition::GetEnumMap;
	}

	RE::BSString CurrentPackageTypeCondition::GetArgument() const
	{
		const auto packageProcedureType = static_cast<RE::PACKAGE_TYPE>(packageTypeComponent->GetNumericValue(nullptr));

		return GetPackageTypeName(packageProcedureType);
	}



	RE::PACKAGE_TYPE CurrentPackageTypeCondition::GetPackageType(RE::Actor* a_actor) const
	{
		if (a_actor) {
			if (const auto currentPackage = a_actor->GetCurrentPackage()) {
				return *currentPackage->packData.packType;
			}
		}

		return RE::PACKAGE_TYPE::kNone;
	}

	std::string_view CurrentPackageTypeCondition::GetPackageTypeName(RE::PACKAGE_TYPE a_type) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_type)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& CurrentPackageTypeCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[-1] = "None"sv;
			enumMap[0] = "Explore"sv;
			enumMap[1] = "Follow"sv;
			enumMap[2] = "Escort"sv;
			enumMap[3] = "Eat"sv;
			enumMap[4] = "Sleep"sv;
			enumMap[5] = "Wander"sv;
			enumMap[6] = "Travel"sv;
			enumMap[7] = "Accompany"sv;
			enumMap[8] = "UseItemAt"sv;
			enumMap[9] = "Ambush"sv;
			enumMap[10] = "FleeNotCombat"sv;
			enumMap[11] = "CastMagic"sv;
			enumMap[12] = "Sandbox"sv;
			enumMap[13] = "Patrol"sv;
			enumMap[14] = "Guard"sv;
			enumMap[15] = "Dialogue"sv;
			enumMap[16] = "UseWeapon"sv;
			enumMap[17] = "Find"sv;
			enumMap[18] = "Package"sv;
			enumMap[19] = "PackageTemplate"sv;
			enumMap[20] = "Activate"sv;
			enumMap[21] = "Alarm"sv;
			enumMap[22] = "Flee"sv;
			enumMap[23] = "Trespass"sv;
			enumMap[24] = "Spectator"sv;
			enumMap[25] = "ReactToDead"sv;
			enumMap[26] = "GetUpFromChairBed"sv;
			enumMap[27] = "DoNothing"sv;
			enumMap[28] = "InGameDialogue"sv;
			enumMap[29] = "Surface"sv;
			enumMap[30] = "SearchForAttacker"sv;
			enumMap[31] = "AvoidPlayer"sv;
			enumMap[32] = "ReactToDestroyedObject"sv;
			enumMap[33] = "ReactToGrenadeOrMine"sv;
			enumMap[34] = "StealWarning"sv;
			enumMap[35] = "PickPocketWarning"sv;
			enumMap[36] = "MovementBlocked"sv;
			enumMap[37] = "VampireFeed"sv;
			enumMap[38] = "Cannibal"sv;
			enumMap[39] = "Landing"sv;
			enumMap[40] = "Unused"sv;
			enumMap[41] = "MountActor"sv;
			enumMap[42] = "DismountActor"sv;
			enumMap[43] = "ClearMountPosition"sv;
			return enumMap;
		}();

		return map;
	}










	void TargetConditionBase::PostInitialize()
	{
		ConditionBase::PostInitialize();
		targetTypeComponent->value.getEnumMap = &TargetConditionBase::GetEnumMap;
	}

	std::string_view TargetConditionBase::GetTargetTypeName(Utils::TargetType a_targetType) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_targetType)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& TargetConditionBase::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Target"sv;
			enumMap[1] = "Combat target"sv;
			enumMap[2] = "Dialogue target"sv;
			enumMap[3] = "Follow target"sv;
			enumMap[4] = "Headtrack target"sv;
			enumMap[5] = "Package target"sv;
			enumMap[6] = "Any target"sv;
			return enumMap;
		}();

		return map;
	}

	RE::BSString HasTargetCondition::GetArgument() const
	{
		const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(nullptr));

		return GetTargetTypeName(targetType);
	}


	RE::BSString CurrentTargetDistanceCondition::GetArgument() const
	{
		const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(nullptr));
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Distance to {} {} {}", GetTargetTypeName(targetType), separator, numericComponent->value.GetArgument()).data();
	}



	RE::TESObjectREFRPtr CurrentTargetDistanceCondition::GetTarget(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				RE::TESObjectREFRPtr target = nullptr;
				const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(a_refr));
				if (Utils::GetCurrentTarget(actor, targetType, target)) {
					return target;
				}
			}
		}

		return nullptr;
	}

	void CurrentTargetRelationshipCondition::PostInitialize()
	{
		TargetConditionBase::PostInitialize();
		numericComponent->value.getEnumMap = &CurrentTargetRelationshipCondition::GetRelationshipRankEnumMap;
	}

	RE::BSString CurrentTargetRelationshipCondition::GetArgument() const
	{
		const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(nullptr));
		const auto relationshipLevel = static_cast<int32_t>(numericComponent->GetNumericValue(nullptr));

		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("{} relationship rank {} {}", GetTargetTypeName(targetType), separator, GetRelationshipRankName(relationshipLevel)).data();
	}



	RE::TESObjectREFRPtr CurrentTargetRelationshipCondition::GetTarget(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				RE::TESObjectREFRPtr target = nullptr;
				const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(a_refr));
				if (Utils::GetCurrentTarget(actor, targetType, target)) {
					return target;
				}
			}
		}

		return nullptr;
	}

	RE::TESNPC* CurrentTargetRelationshipCondition::GetActorBase(RE::TESObjectREFR* a_refr) const
	{
		if (const auto actor = a_refr->As<RE::Actor>()) {
			return actor->GetActorBase();
		}

		return nullptr;
	}

	std::string_view CurrentTargetRelationshipCondition::GetRelationshipRankName(int32_t a_relationshipRank) const
	{
		const auto& map = GetRelationshipRankEnumMap();
		if (const auto it = map.find(a_relationshipRank); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& CurrentTargetRelationshipCondition::GetRelationshipRankEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[-4] = "Archnemesis"sv;
			enumMap[-3] = "Enemy"sv;
			enumMap[-2] = "Foe"sv;
			enumMap[-1] = "Rival"sv;
			enumMap[0] = "Acquaintance"sv;
			enumMap[1] = "Friend"sv;
			enumMap[2] = "Confidant"sv;
			enumMap[3] = "Ally"sv;
			enumMap[4] = "Lover"sv;
			return enumMap;
		}();

		return map;
	}

	RE::BSString EquippedObjectWeightCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Object in {} hand's weight {} {}", boolComponent->GetBoolValue() ? "left"sv : "right"sv, separator, numericComponent->value.GetArgument()).data();
	}



	RE::TESForm* EquippedObjectWeightCondition::GetEquippedForm(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->GetEquippedObject(boolComponent->GetBoolValue());
			}
		}

		return nullptr;
	}

	void CastingSourceConditionBase::PostInitialize()
	{
		ConditionBase::PostInitialize();
		castingSourceComponent->value.getEnumMap = &CastingSourceConditionBase::GetCastingSourceEnumMap;
	}

	std::string_view CastingSourceConditionBase::GetCastingSourceName(RE::MagicSystem::CastingSource a_source) const
	{
		const auto& map = GetCastingSourceEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_source)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& CastingSourceConditionBase::GetCastingSourceEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Left hand"sv;
			enumMap[1] = "Right hand"sv;
			enumMap[2] = "Other"sv;
			enumMap[3] = "Instant"sv;
			return enumMap;
		}();

		return map;
	}

	void CurrentCastingTypeCondition::PostInitialize()
	{
		CastingSourceConditionBase::PostInitialize();
		castingTypeComponent->value.getEnumMap = &CurrentCastingTypeCondition::GetCastingTypeEnumMap;
	}

	RE::BSString CurrentCastingTypeCondition::GetArgument() const
	{
		const auto castingSource = static_cast<RE::MagicSystem::CastingSource>(castingSourceComponent->GetNumericValue(nullptr));
		const auto castingType = static_cast<RE::MagicSystem::CastingType>(castingTypeComponent->GetNumericValue(nullptr));

		return std::format("{} source casting type is {}", GetCastingSourceName(castingSource), GetCastingTypeName(castingType)).data();
	}



	bool CurrentCastingTypeCondition::GetCastingType(RE::Actor* a_actor, RE::MagicSystem::CastingSource a_source, RE::MagicSystem::CastingType& a_outType) const
	{
		if (a_source < RE::MagicSystem::CastingSource::kLeftHand || a_source > RE::MagicSystem::CastingSource::kInstant) {
			return false;
		}

		if (a_actor) {
			const RE::MagicItem* spellItem = nullptr;
			if (const auto magicCaster = a_actor->GetMagicCaster(a_source)) {
				spellItem = magicCaster->currentSpell;
			}

			if (!spellItem) {
				spellItem = a_actor->GetActorRuntimeData().selectedSpells[static_cast<int32_t>(a_source)];
			}

			if (spellItem) {
				a_outType = spellItem->GetCastingType();
				return true;
			}
		}

		return false;
	}

	std::string_view CurrentCastingTypeCondition::GetCastingTypeName(RE::MagicSystem::CastingType a_type) const
	{
		const auto& map = GetCastingTypeEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_type)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& CurrentCastingTypeCondition::GetCastingTypeEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Constant effect"sv;
			enumMap[1] = "Fire and forget"sv;
			enumMap[2] = "Concentration"sv;
			enumMap[3] = "Scroll"sv;
			return enumMap;
		}();

		return map;
	}

	void CurrentDeliveryTypeCondition::PostInitialize()
	{
		CastingSourceConditionBase::PostInitialize();
		deliveryTypeComponent->value.getEnumMap = &CurrentDeliveryTypeCondition::GetDeliveryTypeEnumMap;
	}

	RE::BSString CurrentDeliveryTypeCondition::GetArgument() const
	{
		const auto castingSource = static_cast<RE::MagicSystem::CastingSource>(castingSourceComponent->GetNumericValue(nullptr));
		const auto deliveryType = static_cast<RE::MagicSystem::Delivery>(deliveryTypeComponent->GetNumericValue(nullptr));

		return std::format("{} source delivery type is {}", GetCastingSourceName(castingSource), GetDeliveryTypeName(deliveryType)).data();
	}



	bool CurrentDeliveryTypeCondition::GetDeliveryType(RE::Actor* a_actor, RE::MagicSystem::CastingSource a_source, RE::MagicSystem::Delivery& a_outDeliveryType) const
	{
		if (a_source < RE::MagicSystem::CastingSource::kLeftHand || a_source > RE::MagicSystem::CastingSource::kInstant) {
			return false;
		}

		if (a_actor) {
			const RE::MagicItem* spellItem = nullptr;
			if (const auto magicCaster = a_actor->GetMagicCaster(a_source)) {
				spellItem = magicCaster->currentSpell;
			}

			if (!spellItem) {
				spellItem = a_actor->GetActorRuntimeData().selectedSpells[static_cast<int32_t>(a_source)];
			}

			if (spellItem) {
				a_outDeliveryType = spellItem->GetDelivery();
				return true;
			}
		}

		return false;
	}

	std::string_view CurrentDeliveryTypeCondition::GetDeliveryTypeName(RE::MagicSystem::Delivery a_deliveryType) const
	{
		const auto& map = GetDeliveryTypeEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_deliveryType)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& CurrentDeliveryTypeCondition::GetDeliveryTypeEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Self"sv;
			enumMap[1] = "Touch"sv;
			enumMap[2] = "Aimed"sv;
			enumMap[3] = "Target actor"sv;
			enumMap[4] = "Target location"sv;
			return enumMap;
		}();

		return map;
	}

	RE::BSString IsQuestStageDoneCondition::GetArgument() const
	{
		return std::format("{} stage {}", questComponent->form.GetArgument(), stageIndexComponent->value.GetArgument()).data();
	}


	void CurrentWeatherHasFlagCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		weatherFlagComponent->value.getEnumMap = &CurrentWeatherHasFlagCondition::GetEnumMap;
	}

	RE::BSString CurrentWeatherHasFlagCondition::GetArgument() const
	{
		const auto flag = static_cast<int32_t>(weatherFlagComponent->GetNumericValue(nullptr));

		return GetFlagName(flag);
	}



	std::string_view CurrentWeatherHasFlagCondition::GetFlagName(int32_t a_index) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(a_index); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& CurrentWeatherHasFlagCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Pleasant"sv;
			enumMap[1] = "Cloudy"sv;
			enumMap[2] = "Rainy"sv;
			enumMap[3] = "Snow"sv;
			return enumMap;
		}();

		return map;
	}

	RE::BSString InventoryCountHasKeywordCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		const auto keywordArgument = keywordComponent->keyword.GetArgument();
		return std::format("{} count {} {}", keywordArgument, separator, numericComponent->value.GetArgument()).data();
	}



	int InventoryCountHasKeywordCondition::GetItemCount(RE::TESObjectREFR* a_refr) const
	{
		int count = 0;

		if (keywordComponent->IsValid() && a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				const auto inv = actor->GetInventory([this](const RE::TESBoundObject& a_object) {
					if (const auto bgsKeywordForm = a_object.As<RE::BGSKeywordForm>()) {
						return keywordComponent->HasKeyword(bgsKeywordForm);
					}
					return false;
				});

				for (const auto& invData : inv | std::views::values) {
					const auto& [itemCount, entry] = invData;
					count += itemCount;
				}
			}
		}

		return count;
	}

	RE::BSString CurrentTargetRelativeAngleCondition::GetArgument() const
	{
		const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(nullptr));
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Relative angle to {} {} {}", GetTargetTypeName(targetType), separator, numericComponent->value.GetArgument()).data();
	}



	RE::TESObjectREFRPtr CurrentTargetRelativeAngleCondition::GetTarget(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				RE::TESObjectREFRPtr target = nullptr;
				const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(a_refr));
				if (Utils::GetCurrentTarget(actor, targetType, target)) {
					return target;
				}
			}
		}

		return nullptr;
	}

	float CurrentTargetRelativeAngleCondition::GetRelativeAngle(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				RE::TESObjectREFRPtr target = nullptr;
				const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(a_refr));
				if (Utils::GetCurrentTarget(actor, targetType, target)) {
					const auto refAngle = a_refr->GetAngleZ();
					const auto targetAngle = target->GetAngleZ();

					auto ret = Utils::NormalRelativeAngle(refAngle - targetAngle);
					if (degreesComponent->GetBoolValue()) {
						ret = RE::rad_to_deg(ret);
					}
					if (absoluteComponent->GetBoolValue()) {
						ret = std::fabs(ret);
					}

					return ret;
				}
			}
		}

		return 0.f;
	}

	RE::BSString CurrentTargetLineOfSightCondition::GetArgument() const
	{
		const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(nullptr));

		if (boolComponent->GetBoolValue()) {
			return std::format("Is in line of sight of {}", GetTargetTypeName(targetType)).data();
		}

		return std::format("{} is in line of sight", GetTargetTypeName(targetType)).data();
	}


	RE::TESObjectREFRPtr CurrentTargetLineOfSightCondition::GetTarget(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				RE::TESObjectREFRPtr target = nullptr;
				const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(a_refr));
				if (Utils::GetCurrentTarget(actor, targetType, target)) {
					return target;
				}
			}
		}

		return nullptr;
	}

	RE::BSString CurrentRotationSpeedCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Rotation speed {} {}", separator, numericComponent->value.GetArgument()).data();
	}










	void AttackStateCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		attackStateComponent->value.getEnumMap = &AttackStateCondition::GetEnumMap;
	}

	RE::BSString AttackStateCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		std::string attackStateName = "(Invalid)";
		const auto attackState = static_cast<uint32_t>(attackStateComponent->GetNumericValue(nullptr));

		const auto& map = GetEnumMap();
		if (const auto it = map.find(attackState); it != map.end()) {
			attackStateName = it->second;
		}

		return std::format("Attack state {} {}", separator, attackStateName).data();
	}



	RE::ATTACK_STATE_ENUM AttackStateCondition::GetAttackState(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->AsActorState()->GetAttackState();
			}
		}

		return RE::ATTACK_STATE_ENUM::kNone;
	}

	std::string_view AttackStateCondition::GetAttackStateName(RE::ATTACK_STATE_ENUM a_attackState) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_attackState)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& AttackStateCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "None"sv;
			enumMap[1] = "Draw"sv;
			enumMap[2] = "Swing"sv;
			enumMap[3] = "Hit"sv;
			enumMap[4] = "Next attack"sv;
			enumMap[5] = "Follow through"sv;
			enumMap[6] = "Bash"sv;
			enumMap[7] = "[Unused?]"sv;
			enumMap[8] = "Bow draw"sv;
			enumMap[9] = "Bow attached"sv;
			enumMap[10] = "Bow drawn"sv;
			enumMap[11] = "Bow releasing"sv;
			enumMap[12] = "Bow released"sv;
			enumMap[13] = "Bow next attack"sv;
			enumMap[14] = "Bow follow through"sv;
			enumMap[15] = "Fire"sv;
			enumMap[16] = "Firing"sv;
			enumMap[17] = "Fired"sv;

			return enumMap;
		}();

		return map;
	}



	IsAnimationPlayingCondition::IsAnimationPlayingCondition()
	{
		animationComponent = AddComponent<AnimationConditionComponent>("Animation", "The animation file, relative to the animations folder. Pick lists the reference's.");
		AnimationGraph::SetPickers(animationComponent->value);
	}



	HasMorphCondition::HasMorphCondition()
	{
		textComponent = AddComponent<TextConditionComponent>("Morph name", "The morph's own name, as the TRI file spells it: a BodySlide slider such as Breasts, or a face morph such as NoseHeight.");
		// Every scanned morph, a body group and a face group; the value is the row, mapped back to its label on pick
		textComponent->text.SetPickerItems([] {
			std::vector<UI::UICommon::PickerItem> out;
			const auto& entries = Apply::Entries::GetSingleton();
			for (const auto [kind, group] : { std::pair{ Apply::Kind::kBodyMorph, "Body morphs" }, std::pair{ Apply::Kind::kFaceMorph, "Face morphs" } }) {
				entries.ForEach(kind, [&](Apply::ChannelId, const Apply::Entry& a_entry) {
					out.push_back({ a_entry.name, group, static_cast<std::uint64_t>(out.size()) });
				});
			}
			return out;
		},
			// The channel the name is, body first, as a file would write it
			[](std::string_view a_name) -> std::string {
				const auto& entries = Apply::Entries::GetSingleton();
				for (const auto kind : { Apply::Kind::kBodyMorph, Apply::Kind::kFaceMorph }) {
					if (const auto id = entries.Find(kind, a_name); id.IsValid()) {
						return entries.Identifier(id);
					}
				}
				return {};
			});
	}



	SkinTypeCondition::SkinTypeCondition(Scan::SkinPart a_part) :
		_part(a_part)
	{
		textComponent = AddComponent<TextConditionComponent>("Type", "A row of Scanned Data's Body types: 3BA, BHUNP, UBE, High Poly Head... or vanilla for an actor no row names.");
		const auto part = _part;
		textComponent->text.SetPickerItems([part] {
			std::vector<UI::UICommon::PickerItem> out;
			for (const auto& name : BodyTypes::Names(part)) {
				out.push_back({ name, {}, static_cast<std::uint64_t>(out.size()) });
			}
			return out;
		},
			[part](std::string_view a_text) -> std::string {
				for (const auto& name : BodyTypes::Names(part)) {
					if (Utils::CompareStringsIgnoreCase(name, a_text)) {
						return name;
					}
				}
				return {};
			});
	}

	RE::BSString SkinTypeCondition::GetDescription() const
	{
		return _part == Scan::SkinPart::kHead ?
		           "Checks which head the actor has, by its face part's model against the head rows of Scanned Data's Body types. From the records: no 3D needed."sv.data() :
		           "Checks which body the actor has: its naked body from its race or skin, resolved through the BodySlide TRI built beside it and the rows of Scanned Data's Body types. From the records: no 3D needed, and what it wears does not change it."sv.data();
	}



	UVLayoutCondition::UVLayoutCondition(Scan::SkinPart a_part) :
		_part(a_part)
	{
		textComponent = AddComponent<TextConditionComponent>("Layout", "The layout, as the UV boxes of Scanned Data's Body types spell it.");
		const auto part = _part;
		textComponent->text.SetPickerItems([part] {
			std::vector<UI::UICommon::PickerItem> out;
			for (const auto& layout : BodyTypes::Layouts(part)) {
				out.push_back({ layout, {}, static_cast<std::uint64_t>(out.size()) });
			}
			return out;
		},
			[part](std::string_view a_text) -> std::string {
				for (const auto& layout : BodyTypes::Layouts(part)) {
					if (Utils::CompareStringsIgnoreCase(layout, a_text)) {
						return layout;
					}
				}
				return {};
			});
	}

	RE::BSString UVLayoutCondition::GetDescription() const
	{
		return _part == Scan::SkinPart::kHead ?
		           "Checks which UV layout the actor's head textures are painted for: Vanilla, UBE, COtR, Khajiit... From its face part through Scanned Data's Body types; no 3D needed."sv.data() :
		           "Checks which UV layout the actor's body textures are painted for: CBBE, UNP, UBE, Vanilla Female, Vanilla Male... From its naked body through Scanned Data's Body types; no 3D needed, and what it wears does not change it."sv.data();
	}




	RE::TESObjectREFR* TARGETCondition::GetRefrToEvaluate(RE::TESObjectREFR* a_refr) const
	{
		return GetTarget(a_refr).get();
	}


	RE::TESObjectREFRPtr TARGETCondition::GetTarget(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				RE::TESObjectREFRPtr target = nullptr;
				const auto targetType = static_cast<Utils::TargetType>(targetTypeComponent->GetNumericValue(a_refr));
				if (Utils::GetCurrentTarget(actor, targetType, target)) {
					return target;
				}
			}
		}

		return nullptr;
	}

	RE::TESObjectREFR* PLAYERCondition::GetRefrToEvaluate([[maybe_unused]] RE::TESObjectREFR* a_refr) const
	{
		return RE::PlayerCharacter::GetSingleton();
	}


	RE::BSString LightLevelCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Light level {} {}", separator, numericComponent->value.GetArgument()).data();
	}





	void LifeStateCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		lifeStateComponent->value.getEnumMap = &LifeStateCondition::GetEnumMap;
	}

	RE::BSString LifeStateCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		std::string lifeStateName = "(Invalid)";
		const auto lifeState = static_cast<uint32_t>(lifeStateComponent->GetNumericValue(nullptr));

		const auto& map = GetEnumMap();
		if (const auto it = map.find(lifeState); it != map.end()) {
			lifeStateName = it->second;
		}

		return std::format("Life state {} {}", separator, lifeStateName).data();
	}



	RE::ACTOR_LIFE_STATE LifeStateCondition::GetLifeState(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->AsActorState()->GetLifeState();
			}
		}

		return RE::ACTOR_LIFE_STATE::kAlive;
	}

	std::string_view LifeStateCondition::GetLifeStateName(RE::ACTOR_LIFE_STATE a_lifeState) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_lifeState)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& LifeStateCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Alive"sv;
			enumMap[1] = "Dying"sv;
			enumMap[2] = "Dead"sv;
			enumMap[3] = "Unconscious"sv;
			enumMap[4] = "Reanimate"sv;
			enumMap[5] = "Recycle"sv;
			enumMap[6] = "Restrained"sv;
			enumMap[7] = "Essential down"sv;
			enumMap[8] = "Bleedout"sv;

			return enumMap;
		}();

		return map;
	}

	void SitSleepStateCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		sitSleepStateComponent->value.getEnumMap = &SitSleepStateCondition::GetEnumMap;
	}

	RE::BSString SitSleepStateCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		std::string lifeStateName = "(Invalid)";
		const auto lifeState = static_cast<uint32_t>(sitSleepStateComponent->GetNumericValue(nullptr));

		const auto& map = GetEnumMap();
		if (const auto it = map.find(lifeState); it != map.end()) {
			lifeStateName = it->second;
		}

		return std::format("Sit/sleep state {} {}", separator, lifeStateName).data();
	}



	RE::SIT_SLEEP_STATE SitSleepStateCondition::GetSitSleepState(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				return actor->AsActorState()->GetSitSleepState();
			}
		}

		return RE::SIT_SLEEP_STATE::kNormal;
	}

	std::string_view SitSleepStateCondition::GetSitSleepStateName(RE::SIT_SLEEP_STATE a_sitSleepState) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(static_cast<int32_t>(a_sitSleepState)); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& SitSleepStateCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Normal"sv;
			enumMap[1] = "Wants to sit"sv;
			enumMap[2] = "Waiting for sit anim"sv;
			enumMap[3] = "Sitting/Riding mount"sv;
			enumMap[4] = "Wants to stand"sv;
			enumMap[5] = "Wants to sleep"sv;
			enumMap[6] = "Waiting for sleep anim"sv;
			enumMap[7] = "Sleeping"sv;
			enumMap[8] = "Wants to wake"sv;

			return enumMap;
		}();

		return map;
	}





	RE::BGSKeyword* IsAttackTypeKeywordCondition::GetAttackType(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto highProcess = actor->GetHighProcess()) {
					if (const auto attackData = highProcess->attackData) {
						return attackData->data.attackType;
					}
				}
			}
		}

		return nullptr;
	}

	void IsAttackTypeFlagCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		attackFlagComponent->value.getEnumMap = &IsAttackTypeFlagCondition::GetEnumMap;
	}

	RE::BSString IsAttackTypeFlagCondition::GetArgument() const
	{
		const auto flag = static_cast<int32_t>(attackFlagComponent->GetNumericValue(nullptr));

		return GetFlagName(flag);
	}



	std::string_view IsAttackTypeFlagCondition::GetFlagName(int32_t a_index) const
	{
		const auto& map = GetEnumMap();
		if (const auto it = map.find(a_index); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}

	const std::map<int32_t, std::string_view>& IsAttackTypeFlagCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "IgnoreWeapon"sv;
			enumMap[1] = "BashAttack"sv;
			enumMap[2] = "PowerAttack"sv;
			enumMap[3] = "ChargeAttack"sv;
			enumMap[4] = "RotatingAttack"sv;
			enumMap[5] = "ContinuousAttack"sv;
			enumMap[6] = "OverrideData"sv;

			return enumMap;
		}();

		return map;
	}

	RE::NiPointer<RE::BGSAttackData> IsAttackTypeFlagCondition::GetAttackData(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto highProcess = actor->GetHighProcess()) {
					if (const auto attackData = highProcess->attackData) {
						return attackData;
					}
				}
			}
		}

		return nullptr;
	}



	RE::BSString IsEquippedHasEnchantmentCondition::GetArgument() const
	{
		const auto formArgument = formComponent->GetArgument();
		std::string ret = std::format("{} on {} hand", formArgument.data(), leftHandComponent->GetBoolValue() ? "left"sv : "right"sv).data();

		if (chargedComponent->GetBoolValue()) {
			ret.append(" | Charged Only"sv);
		}
		return ret.data();
	}



	RE::BSString IsEquippedHasEnchantmentWithKeywordCondition::GetArgument() const
	{
		const auto formArgument = keywordComponent->GetArgument();
		std::string ret = std::format("{} on {} hand", formArgument.data(), leftHandComponent->GetBoolValue() ? "left"sv : "right"sv).data();

		if (chargedComponent->GetBoolValue()) {
			ret.append(" | Charged Only"sv);
		}
		return ret.data();
	}








	bool IdleTimeCondition::IdleTimeConditionStateData::Update(float a_deltaTime)
	{
		if (const auto ref = _refHandle.get()) {
			if (const auto actor = ref->As<RE::Actor>()) {
				if (actor->IsMoving() || actor->IsAttacking() || actor->IsBlocking()) {
					_idleTime = 0.f;
					return false;
				}

				if (actor->AsActorState()->GetWeaponState() > RE::WEAPON_STATE::kSheathed) {
					// check spellcasting
					auto checkHand = [](RE::Actor* a_actor, bool a_bLeftHand) {
						if (const auto equippedObject = a_actor->GetEquippedObject(a_bLeftHand)) {
							if (const auto spell = equippedObject->As<RE::SpellItem>()) {
								if (a_actor->IsCasting(spell)) {
									return true;
								}
							} else if (const auto weapon = equippedObject->As<RE::TESObjectWEAP>()) {
								if (weapon->IsStaff()) {
									int iState = 0;
									a_actor->GetGraphVariableInt("iState", iState);
									if (iState == 10) {  // using staff
										return true;
									}
								}
							}
						}

						return false;
					};

					if (checkHand(actor, false)) {
						_idleTime = 0.f;
						return false;
					}

					if (checkHand(actor, true)) {
						_idleTime = 0.f;
						return false;
					}
				}

				bool bIsShouting = false;
				actor->GetGraphVariableBool("IsShouting", bIsShouting);
				if (bIsShouting) {
					_idleTime = 0.f;
					return false;
				}

				_idleTime += a_deltaTime;
			}
		}

		return false;
	}

	RE::BSString IdleTimeCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);

		return std::format("Idle time {} {}", separator, numericComponent->value.GetArgument()).data();
	}



	RE::BSString MagicEffectElapsedTimeCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		auto ret = std::format("{} | Elapsed time {} {}", formComponent->form.GetArgument(), separator, numericComponent->value.GetArgument());
		if (boolComponent->GetBoolValue()) {
			ret.append(" | Active Only"sv);
		}

		return ret.data();
	}



	void IsWornInSlotCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		slotComponent->value.getEnumMap = &IsWornInSlotCondition::GetEnumMap;
	}

	RE::BSString IsWornInSlotCondition::GetArgument() const
	{
		std::string slotName = "(Invalid)";
		const auto slot = static_cast<uint32_t>(slotComponent->GetNumericValue(nullptr));

		const auto& map = GetEnumMap();
		if (const auto it = map.find(slot); it != map.end()) {
			slotName = it->second;
		}

		return slotName.data();
	}


	const std::map<int32_t, std::string_view>& IsWornInSlotCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;

			enumMap[0] = "Head"sv;
			enumMap[1] = "Hair"sv;
			enumMap[2] = "Body"sv;
			enumMap[3] = "Hands"sv;
			enumMap[4] = "Forearms"sv;
			enumMap[5] = "Amulet"sv;
			enumMap[6] = "Ring"sv;
			enumMap[7] = "Feet"sv;
			enumMap[8] = "Calves"sv;
			enumMap[9] = "Shield"sv;
			enumMap[10] = "Tail"sv;
			enumMap[11] = "LongHair"sv;
			enumMap[12] = "Circlet"sv;
			enumMap[13] = "Ears"sv;
			enumMap[14] = "ModMouth"sv;
			enumMap[15] = "ModNeck"sv;
			enumMap[16] = "ModChestPrimary"sv;
			enumMap[17] = "ModBack"sv;
			enumMap[18] = "ModMisc1"sv;
			enumMap[19] = "ModPelvisPrimary"sv;
			enumMap[20] = "DecapitateHead"sv;
			enumMap[21] = "Decapitate"sv;
			enumMap[22] = "ModPelvisSecondary"sv;
			enumMap[23] = "ModLegRight"sv;
			enumMap[24] = "ModLegLeft"sv;
			enumMap[25] = "ModFaceJewelry"sv;
			enumMap[26] = "ModChestSecondary"sv;
			enumMap[27] = "ModShoulder"sv;
			enumMap[28] = "ModArmLeft"sv;
			enumMap[29] = "ModArmRight"sv;
			enumMap[30] = "ModMisc2"sv;
			enumMap[31] = "FX01"sv;

			return enumMap;
		}();

		return map;
	}

	RE::BSString InventoryWeightCondition::GetArgument() const
	{
		const auto separator = ComparisonConditionComponent::GetOperatorString(comparisonComponent->comparisonOperator);
		const std::string prefix = boolComponent->GetBoolValue() ? "Encumbrance Percentage" : "Total Inventory Weight";
		return std::format("{} {} {}", prefix, separator, numericComponent->value.GetArgument()).data();
	}



	float InventoryWeightCondition::GetInventoryWeight(RE::TESObjectREFR* a_refr) const
	{
		if (a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				if (const auto inventoryChanges = a_refr->GetInventoryChanges(true)) {
					const float weight = inventoryChanges->GetInventoryWeight();
					if (boolComponent->GetBoolValue()) {
						return weight / actor->GetTotalCarryWeight() * 100.f;
					} else {
						return weight;
					}
				}
			}
		}

		return 0.f;
	}




	void CastingSpellCondition::PostInitialize()
	{
		ConditionBase::PostInitialize();
		castingSourceComponent->value.getEnumMap = &CastingSpellCondition::GetEnumMap;
	}

	RE::BSString CastingSpellCondition::GetArgument() const
	{
		const auto castingSource = static_cast<int32_t>(castingSourceComponent->GetNumericValue(nullptr));

		const auto& map = GetEnumMap();
		if (const auto it = map.find(castingSource); it != map.end()) {
			return it->second;
		}

		return "(Invalid)"sv;
	}


	const std::map<int32_t, std::string_view>& CastingSpellCondition::GetEnumMap()
	{
		static const auto map = []() {
			std::map<int32_t, std::string_view> enumMap;
			enumMap[0] = "Left hand"sv;
			enumMap[1] = "Right hand"sv;
			enumMap[2] = "Dual"sv;
			return enumMap;
		}();

		return map;
	}

	RE::BSString HasBoundWeaponEquippedCondition::GetArgument() const
	{
		return std::format("Bound weapon in {} hand", boolComponent->GetBoolValue() ? "left"sv : "right"sv).data();
	}


	// ---- compiled ----

	namespace
	{
		constexpr float kNoAnswer = std::numeric_limits<float>::quiet_NaN();

		// A form the row names, against what the actor has: one test of the resource, merged with its siblings asking the
		// same; a feed's form, what the feed holds for the actor, met against it
		Program::Expr FormIn(Program::Builder& a_builder, [[maybe_unused]] const ConditionBase* a_condition, const FormConditionComponent* a_form, const std::string& a_key)
		{
			if (a_form->form.IsFeed()) {
				return a_builder.Meets(a_builder.ResourceSet(a_key), a_builder.ResourceSet(a_form->form.GetFeed()));
			}
			const auto wanted = FormIdOf(a_form->GetTESFormValue());
			return wanted ? a_builder.HasAny(a_builder.ResourceSet(a_key), { wanted }) : a_builder.False();
		}

		// The keywords the row names, any of them in the resource's set
		template <class Value>
		Program::Expr KeywordsIn(Program::Builder& a_builder, const Value& a_keywords, const std::string& a_key)
		{
			std::vector<std::uint64_t> wanted;
			a_keywords.ForEachKeyword([&](auto a_keyword) {
				wanted.push_back(a_keyword->GetFormID());
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return a_builder.HasAny(a_builder.ResourceSet(a_key), std::move(wanted));
		}

		// A keyword form's keywords, for a set resource
		void KeywordsOf(const RE::BGSKeywordForm* a_form, std::vector<RE::FormID>& a_out)
		{
			if (!a_form) {
				return;
			}
			for (std::uint32_t i = 0; i < a_form->numKeywords; ++i) {
				if (a_form->keywords[i]) {
					a_out.push_back(a_form->keywords[i]->GetFormID());
				}
			}
		}

		// The slot a row names, read once at compile; none outside the 32 biped slots
		std::optional<std::uint32_t> StaticSlot(const NumericConditionComponent* a_slot)
		{
			const auto slot = static_cast<std::int32_t>(a_slot->value.GetValue(nullptr));
			return slot >= 0 && slot < 32 ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(slot)) : std::nullopt;
		}

		constexpr float kWornInterval = 5.f;

		// What kind of item is held, as IsEquippedType numbers it
		int8_t EquippedTypeOf(RE::TESForm* a_equipped)
		{
			int8_t currentType = -1;

			if (const auto equippedForm = a_equipped) {
				switch (*equippedForm->formType) {
				case RE::FormType::Weapon:
					if (const auto equippedWeapon = equippedForm->As<RE::TESObjectWEAP>()) {
						switch (equippedWeapon->GetWeaponType()) {
						case RE::WEAPON_TYPE::kHandToHandMelee:
							currentType = 0;
							break;
						case RE::WEAPON_TYPE::kOneHandSword:
							currentType = 1;
							break;
						case RE::WEAPON_TYPE::kOneHandDagger:
							currentType = 2;
							break;
						case RE::WEAPON_TYPE::kOneHandAxe:
							currentType = 3;
							break;
						case RE::WEAPON_TYPE::kOneHandMace:
							currentType = 4;
							break;
						case RE::WEAPON_TYPE::kTwoHandSword:
							currentType = 5;
							break;
						case RE::WEAPON_TYPE::kTwoHandAxe:
							if (!ModRegistry::bKeywordsLoaded) {
								ModRegistry::GetSingleton().LoadKeywords();
							}
							/*if (equippedWeapon->HasKeyword(ModRegistry::kywd_weapTypeBattleaxe)) {
	                        currentType = 6;
	                    }*/
							if (equippedWeapon->HasKeyword(ModRegistry::kywd_weapTypeWarhammer)) {
								currentType = 10;
							} else {
								// just fall back to battleaxe
								currentType = 6;
							}
							break;
						case RE::WEAPON_TYPE::kBow:
							currentType = 7;
							break;
						case RE::WEAPON_TYPE::kStaff:
							currentType = 8;
							break;
						case RE::WEAPON_TYPE::kCrossbow:
							currentType = 9;
							break;
						}
					}
					break;
				case RE::FormType::Armor:
					if (auto equippedShield = equippedForm->As<RE::TESObjectARMO>()) {
						currentType = 11;
					}
					break;
				case RE::FormType::Spell:
					if (const auto equippedSpell = equippedForm->As<RE::SpellItem>()) {
						const RE::ActorValue associatedSkill = equippedSpell->GetAssociatedSkill();
						switch (associatedSkill) {
						case RE::ActorValue::kAlteration:
							currentType = 12;
							break;
						case RE::ActorValue::kIllusion:
							currentType = 13;
							break;
						case RE::ActorValue::kDestruction:
							currentType = 14;
							break;
						case RE::ActorValue::kConjuration:
							currentType = 15;
							break;
						case RE::ActorValue::kRestoration:
							currentType = 16;
							break;
						}
					}
					break;
				case RE::FormType::Scroll:
					if (auto equippedScroll = equippedForm->As<RE::ScrollItem>()) {
						currentType = 17;
					}
					break;
				case RE::FormType::Light:
					if (auto equippedTorch = equippedForm->As<RE::TESObjectLIGH>()) {
						currentType = 18;
					}
					break;
				}
			} else {
				// nothing equipped
				currentType = 0;
			}

			return currentType;
		}

		std::string HexOf(std::uint64_t a_value)
		{
			return std::format("{:X}", a_value);
		}

		// A text in a key: its hash, the registry's keys being letters and digits
		std::string KeyOfText(std::string_view a_text)
		{
			return HexOf(Resources::TextKey(a_text));
		}

		// A setting read once at compile: a number written in the row, or what it holds then
		float Fixed(const NumericConditionComponent* a_number)
		{
			return a_number->GetNumericValue(nullptr);
		}

		// The form a row names, when it is written in the row; a feed's form cannot be part of a key
		RE::TESForm* FixedForm(const FormConditionComponent* a_form)
		{
			if (a_form->form.IsFeed()) {
				logger::warn("conditions: a feed cannot choose what this condition reads; the row never holds");
				return nullptr;
			}
			return a_form->GetTESFormValue();
		}

		// A number read off the actor, nothing when there is none
		template <class F>
		std::function<float(RE::TESObjectREFR*)> OffActor(F a_read)
		{
			return [a_read](RE::TESObjectREFR* a_refr) {
				auto* actor = a_refr ? a_refr->As<RE::Actor>() : nullptr;
				return actor ? a_read(actor) : std::numeric_limits<float>::quiet_NaN();
			};
		}

		// The actor's target of a kind, as TARGET resolves it
		RE::TESObjectREFRPtr TargetOf(RE::Actor* a_actor, std::int32_t a_type)
		{
			RE::TESObjectREFRPtr target = nullptr;
			return a_actor && Utils::GetCurrentTarget(a_actor, static_cast<Utils::TargetType>(a_type), target) ? target : nullptr;
		}

		// The spell in a casting source, cast or selected
		const RE::MagicItem* SpellIn(RE::Actor* a_actor, RE::MagicSystem::CastingSource a_source)
		{
			if (a_source < RE::MagicSystem::CastingSource::kLeftHand || a_source > RE::MagicSystem::CastingSource::kInstant) {
				return nullptr;
			}
			const RE::MagicItem* spell = nullptr;
			if (const auto caster = a_actor->GetMagicCaster(a_source)) {
				spell = caster->currentSpell;
			}
			return spell ? spell : a_actor->GetActorRuntimeData().selectedSpells[static_cast<std::int32_t>(a_source)];
		}

		// Each active effect of an effect, its seconds so far
		template <class F>
		void ForEachActiveOf(RE::Actor* a_actor, const RE::EffectSetting* a_effect, F&& a_visit)
		{
			auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;
			if (!target) {
				return;
			}
			const auto visit = [&](RE::ActiveEffect* a_active) {
				if (a_active && !a_active->flags.any(RE::ActiveEffect::Flag::kInactive) && a_active->GetBaseObject() == a_effect) {
					a_visit(a_active->elapsedSeconds);
				}
			};
			if (REL::Module::IsVR()) {
				target->VisitActiveEffects([&](RE::ActiveEffect* a_active) {
					visit(a_active);
					return RE::BSContainer::ForEachResult::kContinue;
				});
			} else if (auto* effects = target->GetActiveEffectList()) {
				for (auto* active : *effects) {
					visit(active);
				}
			}
		}

		// A yes or no read off the actor, or off the reference: 1 or 0, nothing when there is no actor to ask
		Program::Expr ActorFlag(Program::Builder& a_builder, std::string a_key, std::string_view a_description, bool (*a_read)(RE::Actor*))
		{
			return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(std::move(a_key), a_description, [a_read](RE::TESObjectREFR* a_refr) {
				auto* actor = a_refr ? a_refr->As<RE::Actor>() : nullptr;
				return actor ? (a_read(actor) ? 1.f : 0.f) : std::numeric_limits<float>::quiet_NaN();
			})));
		}

		Program::Expr ReferenceFlag(Program::Builder& a_builder, std::string a_key, std::string_view a_description, bool (*a_read)(RE::TESObjectREFR*))
		{
			return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(std::move(a_key), a_description, [a_read](RE::TESObjectREFR* a_refr) {
				return a_refr ? (a_read(a_refr) ? 1.f : 0.f) : std::numeric_limits<float>::quiet_NaN();
			})));
		}
		constexpr float kFactionInterval = 1.f;

		// The effects on an actor, each the effect's base, all of them or the active ones alone, as HasMagicEffect reads them
		template <class F>
		void ForEachEffect(RE::Actor* a_actor, bool a_bActiveOnly, F&& a_visit)
		{
			auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;
			if (!target) {
				return;
			}
			const auto visit = [&](RE::ActiveEffect* a_effect) {
				if (a_effect && (!a_bActiveOnly || !a_effect->flags.any(RE::ActiveEffect::Flag::kInactive))) {
					if (auto* base = a_effect->GetBaseObject()) {
						a_visit(base);
					}
				}
			};
			if (REL::Module::IsVR()) {
				target->VisitActiveEffects([&](RE::ActiveEffect* a_effect) {
					visit(a_effect);
					return RE::BSContainer::ForEachResult::kContinue;
				});
			} else if (auto* effects = target->GetActiveEffectList()) {
				for (auto* effect : *effects) {
					visit(effect);
				}
			}
		}

		RE::Actor* ActorOf(RE::TESObjectREFR* a_refr)
		{
			return a_refr ? a_refr->As<RE::Actor>() : nullptr;
		}

		std::string ArgumentOf(const IConditionComponent* a_component)
		{
			return a_component->GetArgument().c_str();
		}
	}

	// What the other rows are made of
	Program::Expr InvalidCondition::Compile(Program::Builder& a_builder) const { return a_builder.False(); }
	Program::Expr InvalidNonEssentialCondition::Compile(Program::Builder& a_builder) const { return a_builder.Constant(GetEssential() == EssentialState::kNonEssential_True ? 1.f : 0.f); }
	Program::Expr DeprecatedCondition::Compile(Program::Builder& a_builder) const { return a_builder.False(); }
	Program::Expr ORCondition::Compile(Program::Builder& a_builder) const { return a_builder.Any(conditionsComponent->conditionSet.get()); }
	Program::Expr ANDCondition::Compile(Program::Builder& a_builder) const { return a_builder.All(conditionsComponent->conditionSet.get()); }
	Program::Expr XORCondition::Compile(Program::Builder& a_builder) const { return a_builder.One(conditionsComponent->conditionSet.get()); }

	Program::Expr PRESETCondition::Compile(Program::Builder& a_builder) const
	{
		return conditionsComponent->conditionPreset ? a_builder.All(conditionsComponent->conditionPreset) : a_builder.False();
	}

	Program::Expr TARGETCondition::Compile(Program::Builder& a_builder) const
	{
		// The rows inside read off the target, and off nothing when there is none
		a_builder.PushSubject({ Program::Step::Kind::kTarget, static_cast<std::int32_t>(targetTypeComponent->GetNumericValue(nullptr)) });
		const auto answer = a_builder.All(conditionsComponent->conditionSet.get());
		a_builder.PopSubject();
		return answer;
	}

	Program::Expr PLAYERCondition::Compile(Program::Builder& a_builder) const
	{
		a_builder.PushSubject({ Program::Step::Kind::kPlayer });
		const auto answer = a_builder.All(conditionsComponent->conditionSet.get());
		a_builder.PopSubject();
		return answer;
	}

	// Two numbers compared, each a constant, a global or an actor value
	Program::Expr CompareValues::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Number(numericComponentA->value), comparisonComponent->comparisonOperator, a_builder.Number(numericComponentB->value));
	}

	// A reading compared against a number: the reading is taken once however many thresholds ask about it
	Program::Expr LevelCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.level", "The actor's level.", [](RE::TESObjectREFR* a_refr) {
			const auto actor = ActorOf(a_refr);
			return actor ? static_cast<float>(actor->GetLevel()) : kNoAnswer;
		})),
			comparisonComponent, numericComponent);
	}

	Program::Expr CurrentGameTimeCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.ResourceWorld(BuiltinResources::Number("dcmf.gameTime", "The hour of the day.", [](RE::TESObjectREFR*) { return GetHours(); }, Resources::Scope::World)), comparisonComponent, numericComponent);
	}

	Program::Expr FactionRankCondition::Compile(Program::Builder& a_builder) const
	{
		auto* faction = FormAs<RE::TESFaction>(FixedForm(factionComponent));
		if (!faction) {
			return a_builder.False();
		}
		const auto key = BuiltinResources::Number("dcmf.factionRank." + HexOf(faction->GetFormID()), "The actor's rank in the faction, -2 when not in it.", OffActor([faction](RE::Actor* a_actor) {
			return static_cast<float>(a_actor->IsInFaction(faction) ? Actor_GetFactionRank(a_actor, faction, a_actor->IsPlayerRef()) : -2);
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr SubmergeLevelCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.submergeLevel", "How far under water the reference is, 0 to 1.", [](RE::TESObjectREFR* a_refr) {
			return a_refr ? TESObjectREFR_GetSubmergeLevel(a_refr, a_refr->GetPositionZ(), a_refr->GetParentCell()) : kNoAnswer;
		})),
			comparisonComponent, numericComponent);
	}

	Program::Expr ScaleCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.scale", "The reference's scale.", [](RE::TESObjectREFR* a_refr) { return a_refr ? a_refr->GetScale() : kNoAnswer; })), comparisonComponent, numericComponent);
	}

	Program::Expr HeightCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.height", "The reference's height.", [](RE::TESObjectREFR* a_refr) {
			if (!a_refr) {
				return kNoAnswer;
			}
			const auto actor = ActorOf(a_refr);
			return actor ? actor->GetHeight() : a_refr->GetHeight();
		})),
			comparisonComponent, numericComponent);
	}

	Program::Expr WeightCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.weight", "The reference's weight.", [](RE::TESObjectREFR* a_refr) {
			if (!a_refr) {
				return kNoAnswer;
			}
			const auto actor = ActorOf(a_refr);
			return actor ? actor->GetWeight() : a_refr->GetWeight();
		})),
			comparisonComponent, numericComponent);
	}

	Program::Expr MovementSpeedCondition::Compile(Program::Builder& a_builder) const
	{
		const auto type = static_cast<std::int32_t>(Fixed(movementTypeComponent));
		const auto key = BuiltinResources::Number(std::format("dcmf.movementSpeedOf.{}", type), "The actor's speed for the movement: 0 run, 1 jog, 2 fast walk, 3 walk.", OffActor([type](RE::Actor* a_actor) {
			switch (type) {
			case 0:
				return a_actor->GetRunSpeed();
			case 1:
				return a_actor->GetJogSpeed();
			case 2:
				return a_actor->GetFastWalkSpeed();
			case 3:
				return a_actor->GetWalkSpeed();
			default:
				return 0.f;
			}
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr CurrentMovementSpeedCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.movementSpeed", "How fast the actor moves now.", [](RE::TESObjectREFR* a_refr) {
			const auto actor = ActorOf(a_refr);
			return actor ? actor->AsActorState()->DoGetMovementSpeed() : kNoAnswer;
		})),
			comparisonComponent, numericComponent);
	}

	Program::Expr WindSpeedCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.ResourceWorld(BuiltinResources::Number("dcmf.windSpeed", "The wind's speed.", [](RE::TESObjectREFR*) { return GetWindSpeed(); }, Resources::Scope::World)), comparisonComponent, numericComponent);
	}

	Program::Expr WindAngleDifferenceCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bDegrees = degreesComponent->GetBoolValue();
		const bool bAbsolute = absoluteComponent->GetBoolValue();
		const auto key = BuiltinResources::Number(std::format("dcmf.windAngle.{}.{}", bDegrees ? "degrees" : "radians", bAbsolute ? "absolute" : "signed"), "The angle between the reference's facing and the wind.", [bDegrees, bAbsolute](RE::TESObjectREFR* a_refr) {
			const auto tes = RE::TES::GetSingleton();
			const auto sky = tes ? tes->sky : nullptr;
			if (!a_refr || !sky) {
				return 0.f;
			}
			auto angle = Utils::NormalRelativeAngle(a_refr->GetAngleZ() - sky->windAngle);
			angle = bDegrees ? RE::rad_to_deg(angle) : angle;
			return bAbsolute ? std::fabs(angle) : angle;
		});
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr CrimeGoldCondition::Compile(Program::Builder& a_builder) const
	{
		auto* faction = FormAs<RE::TESFaction>(FixedForm(factionComponent));
		if (!faction) {
			return a_builder.False();
		}
		const auto key = BuiltinResources::Number("dcmf.crimeGold." + HexOf(faction->GetFormID()), "The actor's bounty with the faction.", OffActor([faction](RE::Actor* a_actor) { return static_cast<float>(a_actor->GetCrimeGoldValue(faction)); }));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr InventoryCountCondition::Compile(Program::Builder& a_builder) const
	{
		auto* form = FixedForm(formComponent);
		const auto wanted = FormIdOf(form);
		const auto key = BuiltinResources::Number("dcmf.itemCount." + HexOf(wanted), "How many of the item the actor carries.", [wanted](RE::TESObjectREFR* a_refr) {
			const auto actor = ActorOf(a_refr);
			if (!actor) {
				return a_refr ? 0.f : std::numeric_limits<float>::quiet_NaN();
			}
			int count = 0;
			for (const auto& [object, data] : actor->GetInventory([wanted](const RE::TESBoundObject& a_object) { return a_object.GetFormID() == wanted; })) {
				count += data.first;
			}
			return static_cast<float>(count);
		});
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr FallDistanceCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.fallDistance", "How far the actor has fallen.", [](RE::TESObjectREFR* a_refr) { return GetFallDistance(a_refr); })), comparisonComponent, numericComponent);
	}

	Program::Expr FallDamageCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.fallDamage", "The damage the actor's fall would deal.", [](RE::TESObjectREFR* a_refr) { return GetFallDamage(a_refr); })), comparisonComponent, numericComponent);
	}

	Program::Expr EquippedObjectWeightCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bLeft = boolComponent->GetBoolValue();
		const auto key = BuiltinResources::Number(bLeft ? "dcmf.equippedWeight.left" : "dcmf.equippedWeight.right", "The weight of what the actor holds in that hand.", OffActor([bLeft](RE::Actor* a_actor) {
			const auto form = a_actor->GetEquippedObject(bLeft);
			return form ? form->GetWeight() : 0.f;
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr InventoryCountHasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		std::vector<RE::BGSKeyword*> keywords;
		std::string ids;
		keywordComponent->keyword.ForEachKeyword([&](auto a_keyword) {
			keywords.push_back(static_cast<RE::BGSKeyword*>(a_keyword));
			ids += HexOf(a_keyword->GetFormID()) + ".";
			return RE::BSContainer::ForEachResult::kContinue;
		});
		const auto key = BuiltinResources::Number("dcmf.itemCountWithKeyword." + KeyOfText(ids), "How many items with any of the keywords the actor carries.", [keywords](RE::TESObjectREFR* a_refr) {
			const auto actor = ActorOf(a_refr);
			if (!actor) {
				return a_refr ? 0.f : std::numeric_limits<float>::quiet_NaN();
			}
			int count = 0;
			const auto inventory = actor->GetInventory([&](const RE::TESBoundObject& a_object) {
				const auto keywordForm = a_object.As<RE::BGSKeywordForm>();
				return keywordForm && std::ranges::any_of(keywords, [&](RE::BGSKeyword* a_keyword) { return keywordForm->HasKeyword(a_keyword); });
			});
			for (const auto& [object, data] : inventory) {
				count += data.first;
			}
			return static_cast<float>(count);
		});
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr CurrentRotationSpeedCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.rotationSpeed", "How fast the actor turns now.", [](RE::TESObjectREFR* a_refr) {
			const auto actor = ActorOf(a_refr);
			return actor ? actor->AsActorState()->DoGetRotationSpeed() : kNoAnswer;
		})),
			comparisonComponent, numericComponent);
	}

	Program::Expr AttackStateCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::Number("dcmf.attackState", "The actor's attack state.", OffActor([](RE::Actor* a_actor) { return static_cast<float>(a_actor->AsActorState()->GetAttackState()); }));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, attackStateComponent);
	}

	Program::Expr LightLevelCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::Number("dcmf.lightLevel", "How lit the actor is.", [](RE::TESObjectREFR* a_refr) {
			const auto actor = ActorOf(a_refr);
			return actor ? Actor_GetLightLevel(actor) : kNoAnswer;
		})),
			comparisonComponent, numericComponent);
	}

	Program::Expr LifeStateCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::Number("dcmf.lifeState", "The actor's life state.", OffActor([](RE::Actor* a_actor) { return static_cast<float>(a_actor->AsActorState()->GetLifeState()); }));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, lifeStateComponent);
	}

	Program::Expr SitSleepStateCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::Number("dcmf.sitSleepState", "The actor's sit or sleep state.", OffActor([](RE::Actor* a_actor) { return static_cast<float>(a_actor->AsActorState()->GetSitSleepState()); }));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, sitSleepStateComponent);
	}

	Program::Expr InventoryWeightCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bPercent = boolComponent->GetBoolValue();
		const auto key = BuiltinResources::Number(bPercent ? "dcmf.inventoryWeight.percent" : "dcmf.inventoryWeight", "What the actor carries weighs, or its share of what it can carry.", OffActor([bPercent](RE::Actor* a_actor) {
			const auto changes = a_actor->GetInventoryChanges(true);
			if (!changes) {
				return 0.f;
			}
			const float weight = changes->GetInventoryWeight();
			return bPercent ? weight / a_actor->GetTotalCarryWeight() * 100.f : weight;
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	// Kept state per row and per clip or modifier: a reading of their own
	Program::Expr RandomCondition::Compile(Program::Builder& a_builder) const
	{
		const float low = Fixed(minRandomComponent);
		const float high = Fixed(maxRandomComponent);
		// Drawn once per actor and row, and again when the read is let go: a loop or an echo of the clip, if the row asks
		const auto key = std::format("dcmf.random.{}.{}.{}", HexOf(reinterpret_cast<std::uintptr_t>(stateComponent)), HexOf(std::bit_cast<std::uint32_t>(low)), HexOf(std::bit_cast<std::uint32_t>(high)));
		BuiltinResources::Drawn(key, low, high);
		if (stateComponent->ShouldResetOnLoopOrEcho()) {
			if (const auto* owner = a_builder.GetOwner(); owner && owner->GetKind() == SubModKind::kClip) {
				BuiltinResources::ResetWith(owner, key);
			}
		}
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr IdleTimeCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::IdleTime()), comparisonComponent, numericComponent);
	}

	Program::Expr MovementSurfaceAngleCondition::Compile(Program::Builder& a_builder) const
	{
		return a_builder.Compare(a_builder.Resource(BuiltinResources::SurfaceAngle(Fixed(smoothingFactorComponent), useNavmeshComponent->GetBoolValue(), degreesComponent->GetBoolValue())), comparisonComponent, numericComponent);
	}


	// About the world alone
	Program::Expr CurrentWeatherCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::Form("dcmf.weather", "The weather now.", [](RE::TESObjectREFR*) -> RE::TESForm* { return RE::Sky::GetSingleton()->currentWeather; }, Resources::Scope::World);
		if (formComponent->form.IsFeed()) {
			return a_builder.Meets(a_builder.ResourceSet(key), a_builder.ResourceSet(formComponent->form.GetFeed()));
		}
		// A weather, or a list of them: any of them the one now
		std::vector<std::uint64_t> wanted;
		auto* form = formComponent->GetTESFormValue();
		if (const auto list = FormAs<RE::BGSListForm>(form)) {
			list->ForEachForm([&](RE::TESForm* a_member) {
				if (a_member) {
					wanted.push_back(a_member->GetFormID());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		} else if (FormAs<RE::TESWeather>(form)) {
			wanted.push_back(form->GetFormID());
		}
		return a_builder.HasAny(a_builder.ResourceSet(key), std::move(wanted));
	}

	Program::Expr CurrentWeatherHasFlagCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::FormSet("dcmf.weatherFlags", "The flags of the weather now, by bit.", [](RE::TESObjectREFR*, std::vector<RE::FormID>& a_out) {
			const auto weather = RE::Sky::GetSingleton()->currentWeather;
			const auto flags = weather ? static_cast<std::uint32_t>(weather->data.flags.underlying()) : 0u;
			for (std::uint32_t bit = 0; bit < 32; ++bit) {
				if (flags & (1u << bit)) {
					a_out.push_back(bit);
				}
			}
		});
		return a_builder.HasAny(a_builder.ResourceSet(key), { static_cast<std::uint64_t>(Fixed(weatherFlagComponent)) });
	}

	Program::Expr IsScenePlayingCondition::Compile(Program::Builder& a_builder) const
	{
		auto* scene = FormAs<RE::BGSScene>(FixedForm(formComponent));
		if (!scene) {
			return a_builder.False();
		}
		return a_builder.Truthy(a_builder.ResourceWorld(BuiltinResources::Number("dcmf.scenePlaying." + HexOf(scene->GetFormID()), "Whether the scene is playing.", [scene](RE::TESObjectREFR*) { return scene->isPlaying ? 1.f : 0.f; }, Resources::Scope::World)));
	}

	Program::Expr IsMenuOpenCondition::Compile(Program::Builder& a_builder) const
	{
		const std::string menu = textComponent->GetTextValue().c_str();
		return a_builder.Truthy(a_builder.ResourceWorld(BuiltinResources::Number("dcmf.menuOpen." + KeyOfText(menu), "Whether the menu is open.", [menu](RE::TESObjectREFR*) { return RE::UI::GetSingleton()->IsMenuOpen(menu) ? 1.f : 0.f; }, Resources::Scope::World)));
	}


	// A fact about the subject that is itself the answer
	Program::Expr IsFormCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.self", "The reference itself.", [](RE::TESObjectREFR* a_refr) { return a_refr; }));
	}

	Program::Expr IsEquippedCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bLeft = boolComponent->GetBoolValue();
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form(bLeft ? "dcmf.equipped.left" : "dcmf.equipped.right", "What the actor holds in that hand.", [bLeft](RE::TESObjectREFR* a_refr) {
			const auto actor = ActorOf(a_refr);
			return actor ? actor->GetEquippedObject(bLeft) : nullptr;
		}));
	}

	Program::Expr IsEquippedTypeCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bLeft = boolComponent->GetBoolValue();
		const auto key = BuiltinResources::Number(bLeft ? "dcmf.equippedType.left" : "dcmf.equippedType.right", "The type of what the actor holds in that hand.", OffActor([bLeft](RE::Actor* a_actor) {
			return static_cast<float>(EquippedTypeOf(a_actor->GetEquippedObject(bLeft)));
		}));
		return a_builder.Compare(a_builder.Resource(key), Conditions::ComparisonOperator::kEqual, a_builder.Number(numericComponent->value));
	}

	Program::Expr IsEquippedHasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bLeft = boolComponent->GetBoolValue();
		return KeywordsIn(a_builder, keywordComponent->keyword, BuiltinResources::FormSet(bLeft ? "dcmf.equippedKeywords.left" : "dcmf.equippedKeywords.right", "The keywords of what the actor holds in that hand.", [bLeft](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			const auto equipped = actor ? actor->GetEquippedObject(bLeft) : nullptr;
			KeywordsOf(equipped ? equipped->As<RE::BGSKeywordForm>() : nullptr, a_out);
		}));
	}

	Program::Expr IsEquippedPowerCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.equippedPower", "The power or the voice the actor has equipped.", [](RE::TESObjectREFR* a_refr) { return GetEquippedPower(a_refr); }));
	}

	// A form in the row: one test of what the actor wears, merged with its siblings asking the same
	Program::Expr IsWornCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, std::string(BuiltinResources::kWorn));
	}

	Program::Expr IsWornHasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		std::vector<std::uint64_t> wanted;
		keywordComponent->keyword.ForEachKeyword([&](auto a_keyword) {
			wanted.push_back(a_keyword->GetFormID());
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return a_builder.HasAny(a_builder.ResourceSet(std::string(BuiltinResources::kWornKeywords)), std::move(wanted));
	}

	// As the plugin declared it: a number compared, a form held, a text held, each a test of its resource
	Program::Expr ResourceCondition::Compile(Program::Builder& a_builder) const
	{
		if (comparisonComponent) {
			return a_builder.Compare(a_builder.Resource(_definition->resource), comparisonComponent, numericComponent);
		}
		if (formComponent) {
			return FormIn(a_builder, this, formComponent, _definition->resource);
		}
		if (!textComponent) {
			return a_builder.Truthy(a_builder.Resource(_definition->resource));
		}
		const std::string text = textComponent->GetTextValue().c_str();
		return text.empty() ? a_builder.False() : a_builder.HasAny(a_builder.ResourceSet(_definition->resource), { Resources::TextKey(text) });
	}

	Program::Expr IsWornNamedCondition::Compile(Program::Builder& a_builder) const
	{
		const auto name = std::string(textComponent->GetTextValue().c_str());
		return name.empty() ? a_builder.False() : a_builder.HasAny(a_builder.ResourceSet(std::string(BuiltinResources::kWornNames)), { Resources::TextKey(name) });
	}
	Program::Expr IsWornFromPluginCondition::Compile(Program::Builder& a_builder) const
	{
		const auto plugin = std::string(textComponent->GetTextValue().c_str());
		return plugin.empty() ? a_builder.False() : a_builder.HasAny(a_builder.ResourceSet(std::string(BuiltinResources::kWornPlugins)), { Resources::TextKey(plugin) });
	}
	Program::Expr IsFemaleCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.female", "Whether the actor is female.", [](RE::Actor* a_actor) { const auto base = a_actor->GetActorBase(); return base && base->IsFemale(); });
	}
	Program::Expr IsDeadCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.dead", "Whether the actor is dead.", [](RE::Actor* a_actor) { return a_actor->IsDead(); });
	}
	Program::Expr IsChildCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.child", "Whether the actor is a child.", [](RE::Actor* a_actor) { return a_actor->IsChild(); });
	}
	Program::Expr IsPlayerTeammateCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.playerTeammate", "Whether the actor is the player's teammate.", [](RE::Actor* a_actor) { return a_actor->IsPlayerTeammate(); });
	}
	Program::Expr IsInInteriorCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.inInterior", "Whether the actor is in an interior cell.", [](RE::Actor* a_actor) { const auto cell = a_actor->GetParentCell(); return cell && cell->IsInteriorCell(); });
	}
	Program::Expr IsInFactionCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::FormSet("dcmf.factions", "The factions the actor is in.", [](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			auto* actor = ActorOf(a_refr);
			if (!actor) {
				return;
			}
			std::unordered_map<RE::FormID, std::int32_t> ranks;
			actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
				if (a_faction) {
					ranks[a_faction->GetFormID()] = a_rank;
				}
				return false;
			});
			for (const auto& [id, rank] : ranks) {
				if (rank >= 0) {
					a_out.push_back(id);
				}
			}
		}, kFactionInterval));
	}

	Program::Expr HasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		std::vector<Program::Expr> any;
		keywordComponent->keyword.ForEachKeyword([&](auto a_keyword) {
			auto* keyword = static_cast<RE::BGSKeyword*>(a_keyword);
			any.push_back(a_builder.Truthy(a_builder.Resource(BuiltinResources::Number("dcmf.hasKeyword." + HexOf(keyword->GetFormID()), "Whether the reference has the keyword.", [keyword](RE::TESObjectREFR* a_refr) {
				return a_refr ? (a_refr->HasKeyword(keyword) ? 1.f : 0.f) : std::numeric_limits<float>::quiet_NaN();
			}))));
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return any.empty() ? a_builder.False() : a_builder.Any(std::move(any));
	}

	Program::Expr HasMagicEffectCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bActive = boolComponent->GetBoolValue();
		return FormIn(a_builder, this, formComponent, BuiltinResources::FormSet(bActive ? "dcmf.effects.active" : "dcmf.effects", "The magic effects on the actor.", [bActive](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			ForEachEffect(ActorOf(a_refr), bActive, [&](RE::EffectSetting* a_effect) { a_out.push_back(a_effect->GetFormID()); });
		}));
	}

	Program::Expr HasMagicEffectWithKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bActive = boolComponent->GetBoolValue();
		return KeywordsIn(a_builder, keywordComponent->keyword, BuiltinResources::FormSet(bActive ? "dcmf.effectKeywords.active" : "dcmf.effectKeywords", "The keywords of the magic effects on the actor.", [bActive](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			ForEachEffect(ActorOf(a_refr), bActive, [&](RE::EffectSetting* a_effect) { KeywordsOf(a_effect, a_out); });
		}));
	}

	Program::Expr HasPerkCondition::Compile(Program::Builder& a_builder) const
	{
		auto* form = FormAs<RE::BGSPerk>(FixedForm(formComponent));
		if (!form) {
			return a_builder.False();
		}
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number("dcmf.hasPerk." + HexOf(form->GetFormID()), "Whether the actor has the perk.", OffActor([form](RE::Actor* a_actor) { return a_actor->HasPerk(form) ? 1.f : 0.f; }))));
	}

	Program::Expr HasSpellCondition::Compile(Program::Builder& a_builder) const
	{
		auto* form = FormAs<RE::SpellItem>(FixedForm(formComponent));
		if (!form) {
			return a_builder.False();
		}
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number("dcmf.hasSpell." + HexOf(form->GetFormID()), "Whether the actor has the spell.", OffActor([form](RE::Actor* a_actor) { return a_actor->HasSpell(form) ? 1.f : 0.f; }))));
	}

	Program::Expr IsActorBaseCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, std::string(BuiltinResources::kActorBase));
	}
	Program::Expr IsRaceCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, std::string(BuiltinResources::kRace));
	}
	Program::Expr IsUniqueCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.unique", "Whether the actor's base is unique.", [](RE::Actor* a_actor) { const auto base = a_actor->GetActorBase(); return base && base->IsUnique(); });
	}
	Program::Expr IsClassCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.class", "The actor's class.", [](RE::TESObjectREFR* a_refr) { return GetTESClass(a_refr); }));
	}

	Program::Expr IsCombatStyleCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.combatStyle", "The actor's combat style.", [](RE::TESObjectREFR* a_refr) { return GetCombatStyle(a_refr); }));
	}

	Program::Expr IsVoiceTypeCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.voiceType", "The actor's voice type.", [](RE::TESObjectREFR* a_refr) { return GetVoiceType(a_refr); }));
	}

	Program::Expr IsAttackingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.attacking", "Whether the actor is attacking.", [](RE::Actor* a_actor) { return a_actor->IsAttacking(); });
	}
	Program::Expr IsRunningCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.running", "Whether the actor is running.", [](RE::Actor* a_actor) { return a_actor->IsRunning(); });
	}
	Program::Expr IsSneakingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.sneaking", "Whether the actor is sneaking.", [](RE::Actor* a_actor) { return a_actor->IsSneaking(); });
	}
	Program::Expr IsSprintingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.sprinting", "Whether the actor is sprinting.", [](RE::Actor* a_actor) { return a_actor->AsActorState()->IsSprinting(); });
	}
	Program::Expr IsInAirCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.inAir", "Whether the actor is in the air.", [](RE::Actor* a_actor) { return a_actor->IsInMidair(); });
	}
	Program::Expr IsInCombatCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.inCombat", "Whether the actor is in combat.", [](RE::Actor* a_actor) { return a_actor->IsInCombat(); });
	}
	Program::Expr IsWeaponDrawnCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.weaponDrawn", "Whether the actor has its weapon drawn.", [](RE::Actor* a_actor) { return a_actor->AsActorState()->IsWeaponDrawn(); });
	}
	Program::Expr IsInLocationCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::FormSet("dcmf.locations", "The location the reference is in, and every location around it.", [](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			for (auto* location = a_refr ? a_refr->GetCurrentLocation() : nullptr; location; location = location->parentLoc) {
				a_out.push_back(location->GetFormID());
			}
		}));
	}

	Program::Expr HasRefTypeCondition::Compile(Program::Builder& a_builder) const
	{
		return KeywordsIn(a_builder, locRefTypeComponent->locRefType, BuiltinResources::Form("dcmf.locRefType", "The reference's location ref type.", [](RE::TESObjectREFR* a_refr) -> RE::TESForm* {
			return a_refr ? TESObjectREFR_GetLocationRefType(a_refr) : nullptr;
		}));
	}

	Program::Expr IsParentCellCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.parentCell", "The cell the reference is in.", [](RE::TESObjectREFR* a_refr) { return a_refr ? a_refr->GetParentCell() : nullptr; }));
	}

	Program::Expr IsWorldSpaceCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.worldspace", "The worldspace the reference is in.", [](RE::TESObjectREFR* a_refr) { return a_refr ? a_refr->GetWorldspace() : nullptr; }));
	}

	Program::Expr IsMovementDirectionCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::Number("dcmf.movementDirection", "Which way the actor moves: 0 still, 1 forward, 2 right, 3 back, 4 left.", OffActor([](RE::Actor* a_actor) {
			float direction = 0.f;
			if (Actor_IsMoving(a_actor)) {
				float relative = Actor_GetMovementDirectionRelativeToFacing(a_actor);
				if (relative <= RE::NI_TWO_PI) {
					if (relative < 0.f) {
						relative = fmodf(relative, RE::NI_TWO_PI) + RE::NI_TWO_PI;
					}
				} else {
					relative = fmodf(relative, RE::NI_TWO_PI);
				}
				direction = floorf(fmodf(((relative / RE::NI_TWO_PI) * 4.f) + 0.5f, 4.f)) + 1.f;
			}
			return direction;
		}));
		return a_builder.Compare(a_builder.Resource(key), Conditions::ComparisonOperator::kEqual, a_builder.Number(numericComponent->value));
	}

	Program::Expr IsEquippedShoutCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.equippedShout", "The shout the actor has equipped.", [](RE::TESObjectREFR* a_refr) { return GetEquippedShout(a_refr); }));
	}

	Program::Expr IsCurrentPackageCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.package", "The package the actor runs.", [](RE::TESObjectREFR* a_refr) { return GetCurrentPackage(a_refr); }));
	}

	Program::Expr IsWornInSlotHasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		const auto slot = StaticSlot(slotComponent);
		if (!slot) {
			return a_builder.False();
		}
		const auto bit = static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1u << *slot);
		return KeywordsIn(a_builder, keywordComponent->keyword, BuiltinResources::FormSet(BuiltinResources::WornSlotKeywordsKey(*slot), "The keywords of what the actor wears in the slot.", [bit](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			KeywordsOf(actor ? actor->GetWornArmor(bit, true) : nullptr, a_out);
		}, kWornInterval));
	}

	Program::Expr IsBlockingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.blocking", "Whether the actor is blocking.", [](RE::Actor* a_actor) { return a_actor->IsBlocking(); });
	}
	Program::Expr IsCombatStateCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::Number("dcmf.combatState", "The actor's combat state.", OffActor([](RE::Actor* a_actor) { return static_cast<float>(Actor_GetCombatState(a_actor)); }));
		return a_builder.Compare(a_builder.Resource(key), Conditions::ComparisonOperator::kEqual, a_builder.Number(combatStateComponent->value));
	}

	Program::Expr CurrentPackageTypeCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::Number("dcmf.packageType", "The type of the package the actor runs.", OffActor([](RE::Actor* a_actor) {
			const auto package = a_actor->GetCurrentPackage();
			return static_cast<float>(package ? *package->packData.packType : RE::PACKAGE_TYPE::kNone);
		}));
		return a_builder.Compare(a_builder.Resource(key), Conditions::ComparisonOperator::kEqual, a_builder.Number(packageTypeComponent->value));
	}

	Program::Expr IsOnMountCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.onMount", "Whether the actor is on a mount.", [](RE::Actor* a_actor) { return a_actor->IsOnMount(); });
	}
	Program::Expr IsRidingCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bBase = boolComponent->GetBoolValue();
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form(bBase ? "dcmf.mount.base" : "dcmf.mount.ref", "What the actor rides.", [bBase](RE::TESObjectREFR* a_refr) -> RE::TESForm* {
			const auto actor = ActorOf(a_refr);
			RE::ActorPtr mount = nullptr;
			if (!actor || !actor->GetMount(mount)) {
				return nullptr;
			}
			return bBase ? static_cast<RE::TESForm*>(mount->GetActorBase()) : static_cast<RE::TESForm*>(mount.get());
		}));
	}

	Program::Expr IsRidingHasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		return KeywordsIn(a_builder, keywordComponent->keyword, BuiltinResources::FormSet("dcmf.mountKeywords", "The keywords of what the actor rides.", [](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			if (RE::ActorPtr mount = nullptr; actor && actor->GetMount(mount)) {
				KeywordsOf(mount->As<RE::BGSKeywordForm>(), a_out);
			}
		}));
	}

	Program::Expr CurrentFurnitureCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bBase = boolComponent->GetBoolValue();
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form(bBase ? "dcmf.furniture.base" : "dcmf.furniture.ref", "The furniture the actor occupies.", [bBase](RE::TESObjectREFR* a_refr) {
			return a_refr ? Utils::GetCurrentFurnitureForm(a_refr, bBase) : nullptr;
		}));
	}

	Program::Expr CurrentFurnitureHasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bBase = boolComponent->GetBoolValue();
		return KeywordsIn(a_builder, keywordComponent->keyword, BuiltinResources::FormSet(bBase ? "dcmf.furnitureKeywords.base" : "dcmf.furnitureKeywords.ref", "The keywords of the furniture the actor occupies.", [bBase](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto furniture = a_refr ? Utils::GetCurrentFurnitureForm(a_refr, bBase) : nullptr;
			KeywordsOf(furniture ? furniture->As<RE::BGSKeywordForm>() : nullptr, a_out);
		}));
	}

	Program::Expr HasTargetCondition::Compile(Program::Builder& a_builder) const
	{
		const auto type = static_cast<std::int32_t>(Fixed(targetTypeComponent));
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(std::format("dcmf.hasTarget.{}", type), "Whether the actor has a target of the kind.", OffActor([type](RE::Actor* a_actor) { return TargetOf(a_actor, type) ? 1.f : 0.f; }))));
	}

	Program::Expr CurrentTargetDistanceCondition::Compile(Program::Builder& a_builder) const
	{
		const auto type = static_cast<std::int32_t>(Fixed(targetTypeComponent));
		const auto key = BuiltinResources::Number(std::format("dcmf.targetDistance.{}", type), "How far the actor's target of the kind is.", OffActor([type](RE::Actor* a_actor) {
			const auto target = TargetOf(a_actor, type);
			return target ? a_actor->GetPosition().GetDistance(target->GetPosition()) : std::numeric_limits<float>::quiet_NaN();
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr CurrentTargetRelationshipCondition::Compile(Program::Builder& a_builder) const
	{
		const auto type = static_cast<std::int32_t>(Fixed(targetTypeComponent));
		const auto key = BuiltinResources::Number(std::format("dcmf.targetRelationship.{}", type), "The relationship rank between the actor and its target of the kind.", OffActor([type](RE::Actor* a_actor) {
			const auto target = TargetOf(a_actor, type);
			const auto targetActor = target ? target->As<RE::Actor>() : nullptr;
			std::int32_t rank = 0;
			return targetActor && Utils::GetRelationshipRank(a_actor->GetActorBase(), targetActor->GetActorBase(), rank) ? static_cast<float>(rank) : std::numeric_limits<float>::quiet_NaN();
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr CurrentCastingTypeCondition::Compile(Program::Builder& a_builder) const
	{
		const auto source = static_cast<RE::MagicSystem::CastingSource>(static_cast<std::int32_t>(Fixed(castingSourceComponent)));
		const auto key = BuiltinResources::Number(std::format("dcmf.castingType.{}", static_cast<std::int32_t>(source)), "The casting type of the spell in the source.", OffActor([source](RE::Actor* a_actor) {
			const auto spell = SpellIn(a_actor, source);
			return spell ? static_cast<float>(spell->GetCastingType()) : std::numeric_limits<float>::quiet_NaN();
		}));
		return a_builder.Compare(a_builder.Resource(key), Conditions::ComparisonOperator::kEqual, a_builder.Number(castingTypeComponent->value));
	}

	Program::Expr CurrentDeliveryTypeCondition::Compile(Program::Builder& a_builder) const
	{
		const auto source = static_cast<RE::MagicSystem::CastingSource>(static_cast<std::int32_t>(Fixed(castingSourceComponent)));
		const auto key = BuiltinResources::Number(std::format("dcmf.deliveryType.{}", static_cast<std::int32_t>(source)), "The delivery type of the spell in the source.", OffActor([source](RE::Actor* a_actor) {
			const auto spell = SpellIn(a_actor, source);
			return spell ? static_cast<float>(spell->GetDelivery()) : std::numeric_limits<float>::quiet_NaN();
		}));
		return a_builder.Compare(a_builder.Resource(key), Conditions::ComparisonOperator::kEqual, a_builder.Number(deliveryTypeComponent->value));
	}

	Program::Expr IsQuestStageDoneCondition::Compile(Program::Builder& a_builder) const
	{
		auto* quest = FormAs<RE::TESQuest>(FixedForm(questComponent));
		if (!quest) {
			return a_builder.False();
		}
		const auto stage = static_cast<std::uint16_t>(Fixed(stageIndexComponent));
		return a_builder.Truthy(a_builder.ResourceWorld(BuiltinResources::Number(std::format("dcmf.questStage.{}.{}", HexOf(quest->GetFormID()), stage), "Whether the quest's stage is done.", [quest, stage](RE::TESObjectREFR*) { return TESQuest_GetStageDone(quest, stage) ? 1.f : 0.f; }, Resources::Scope::World)));
	}

	Program::Expr CurrentTargetRelativeAngleCondition::Compile(Program::Builder& a_builder) const
	{
		const auto type = static_cast<std::int32_t>(Fixed(targetTypeComponent));
		const bool bDegrees = degreesComponent->GetBoolValue();
		const bool bAbsolute = absoluteComponent->GetBoolValue();
		const auto key = BuiltinResources::Number(std::format("dcmf.targetAngle.{}.{}.{}", type, bDegrees ? "degrees" : "radians", bAbsolute ? "absolute" : "signed"), "The angle between the actor's facing and its target's of the kind.", OffActor([type, bDegrees, bAbsolute](RE::Actor* a_actor) {
			const auto target = TargetOf(a_actor, type);
			if (!target) {
				return std::numeric_limits<float>::quiet_NaN();
			}
			auto angle = Utils::NormalRelativeAngle(a_actor->GetAngleZ() - target->GetAngleZ());
			angle = bDegrees ? RE::rad_to_deg(angle) : angle;
			return bAbsolute ? std::fabs(angle) : angle;
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr CurrentTargetLineOfSightCondition::Compile(Program::Builder& a_builder) const
	{
		const auto type = static_cast<std::int32_t>(Fixed(targetTypeComponent));
		const bool bFromTarget = boolComponent->GetBoolValue();
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(std::format("dcmf.targetLineOfSight.{}.{}", type, bFromTarget ? "fromTarget" : "fromActor"), "Whether the actor and its target of the kind see each other the way asked.", OffActor([type, bFromTarget](RE::Actor* a_actor) {
			const auto target = TargetOf(a_actor, type);
			if (!target) {
				return 0.f;
			}
			bool unused = false;
			if (bFromTarget) {
				const auto targetActor = target->As<RE::Actor>();
				return targetActor && targetActor->HasLineOfSight(a_actor, unused) ? 1.f : 0.f;
			}
			return a_actor->HasLineOfSight(target.get(), unused) ? 1.f : 0.f;
		}))));
	}

	Program::Expr IsTalkingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.talking", "Whether the actor is talking.", [](RE::Actor* a_actor) { return Actor_IsTalking(a_actor); });
	}
	Program::Expr IsGreetingPlayerCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.greetingPlayer", "Whether the actor is greeting the player.", [](RE::Actor* a_actor) { const auto& runtime = a_actor->GetActorRuntimeData(); const auto process = runtime.currentProcess; const auto high = process ? process->high : nullptr; return high && high->greetingPlayer && runtime.dialogueItemTarget.native_handle() == 0x100000; });
	}
	Program::Expr IsInSceneCondition::Compile(Program::Builder& a_builder) const
	{
		return ReferenceFlag(a_builder, "dcmf.inScene", "Whether the reference is in a scene.", [](RE::TESObjectREFR* a_refr) { return a_refr->GetCurrentScene() != nullptr; });
	}
	Program::Expr IsInSpecifiedSceneCondition::Compile(Program::Builder& a_builder) const
	{
		return FormIn(a_builder, this, formComponent, BuiltinResources::Form("dcmf.scene", "The scene the reference is in.", [](RE::TESObjectREFR* a_refr) -> RE::TESForm* { return a_refr ? a_refr->GetCurrentScene() : nullptr; }));
	}

	Program::Expr IsDoingFavorCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.doingFavor", "Whether the actor is doing the player a favor.", [](RE::Actor* a_actor) { const auto process = a_actor->GetActorRuntimeData().currentProcess; const auto high = process ? process->high : nullptr; return high && high->inCommandState; });
	}
	Program::Expr HasMorphCondition::Compile(Program::Builder& a_builder) const
	{
		const std::string morph = textComponent->GetTextValue().c_str();
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number("dcmf.hasMorph." + KeyOfText(morph), "Whether a TRI on the actor has the morph.", [morph](RE::TESObjectREFR* a_refr) {
			const auto state = a_refr ? ActorState::Get(a_refr->GetHandle()) : nullptr;
			if (!state) {
				return 0.f;
			}
			return state->parts.With([&](const std::shared_ptr<const ActorState::Parts>& a_parts) { return a_parts && a_parts->morphs.contains(morph); }) ? 1.f : 0.f;
		})));
	}

	Program::Expr IsAnimationPlayingCondition::Compile(Program::Builder& a_builder) const
	{
		const auto animation = animationComponent->value;
		const auto key = "dcmf.animationPlaying." + KeyOfText(animationComponent->GetArgument().c_str());
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(key, "Whether the animation plays on the reference.", [animation](RE::TESObjectREFR* a_refr) {
			return a_refr ? (AnimationGraph::IsPlaying(a_refr, animation) ? 1.f : 0.f) : std::numeric_limits<float>::quiet_NaN();
		})));
	}

	Program::Expr SkinTypeCondition::Compile(Program::Builder& a_builder) const
	{
		const auto part = _part;
		const auto key = BuiltinResources::TextSet(part == Scan::SkinPart::kHead ? "dcmf.headType" : "dcmf.bodyType", "The actor's body or head type, as Scanned Data's Body types names it.", [part](RE::TESObjectREFR* a_refr, std::vector<std::string>& a_out) {
			auto* actor = ActorOf(a_refr);
			if (!actor) {
				return;
			}
			const auto path = part == Scan::SkinPart::kHead ? BodyTypes::HeadPathOf(actor) : BodyTypes::BodyPathOf(actor);
			a_out.push_back(path.empty() ? std::string(BodyTypes::kVanilla) : BodyTypes::TypeOf(part, path));
		});
		return a_builder.HasAny(a_builder.ResourceSet(key), { Resources::TextKey(textComponent->GetTextValue().c_str()) });
	}

	Program::Expr UVLayoutCondition::Compile(Program::Builder& a_builder) const
	{
		const auto part = _part;
		const auto key = BuiltinResources::TextSet(part == Scan::SkinPart::kHead ? "dcmf.headUV" : "dcmf.bodyUV", "The UV layout of the actor's body or head textures.", [part](RE::TESObjectREFR* a_refr, std::vector<std::string>& a_out) {
			auto* actor = ActorOf(a_refr);
			auto* base = actor ? actor->GetActorBase() : nullptr;
			if (!base) {
				return;
			}
			const auto path = part == Scan::SkinPart::kHead ? BodyTypes::HeadPathOf(actor) : BodyTypes::BodyPathOf(actor);
			a_out.push_back(std::string(BodyTypes::UvOf(part, path, base->GetSex() == RE::SEX::kFemale)));
		});
		return a_builder.HasAny(a_builder.ResourceSet(key), { Resources::TextKey(textComponent->GetTextValue().c_str()) });
	}

	Program::Expr LocationHasKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		return KeywordsIn(a_builder, keywordComponent->keyword, BuiltinResources::FormSet("dcmf.locationKeywords", "The keywords of the location the reference is in.", [](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto location = a_refr ? a_refr->GetCurrentLocation() : nullptr;
			KeywordsOf(location ? location->As<RE::BGSKeywordForm>() : nullptr, a_out);
		}));
	}

	Program::Expr IsAttackTypeKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		auto* wanted = keywordComponent->keyword.GetFormValue();
		if (!wanted) {
			return a_builder.False();
		}
		const auto key = BuiltinResources::Form("dcmf.attackType", "The type of the attack the actor makes.", [](RE::TESObjectREFR* a_refr) -> RE::TESForm* {
			const auto actor = ActorOf(a_refr);
			const auto high = actor ? actor->GetHighProcess() : nullptr;
			return high && high->attackData ? high->attackData->data.attackType : nullptr;
		});
		return a_builder.HasAny(a_builder.ResourceSet(key), { wanted->GetFormID() });
	}

	Program::Expr IsAttackTypeFlagCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::FormSet("dcmf.attackFlags", "The flags of the attack the actor makes, by bit.", [](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			const auto high = actor ? actor->GetHighProcess() : nullptr;
			if (!high || !high->attackData) {
				return;
			}
			const auto flags = static_cast<std::uint32_t>(high->attackData->data.flags.underlying());
			for (std::uint32_t bit = 0; bit < 32; ++bit) {
				if (flags & (1u << bit)) {
					a_out.push_back(bit);
				}
			}
		});
		return a_builder.HasAny(a_builder.ResourceSet(key), { static_cast<std::uint64_t>(Fixed(attackFlagComponent)) });
	}

	Program::Expr LocationClearedCondition::Compile(Program::Builder& a_builder) const
	{
		return ReferenceFlag(a_builder, "dcmf.locationCleared", "Whether the location the reference is in is cleared.", [](RE::TESObjectREFR* a_refr) { const auto location = a_refr->GetCurrentLocation(); return location && location->IsCleared(); });
	}
	Program::Expr IsSummonedCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.summoned", "Whether the actor is summoned.", [](RE::Actor* a_actor) { return a_actor->IsSummoned(); });
	}
	Program::Expr IsEquippedHasEnchantmentCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bLeft = leftHandComponent->GetBoolValue();
		const bool bCharged = chargedComponent->GetBoolValue();
		const auto key = BuiltinResources::FormSet(std::format("dcmf.enchantment.{}.{}", bLeft ? "left" : "right", bCharged ? "charged" : "any"), "The enchantment of what the actor holds in that hand.", [bLeft, bCharged](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			const auto item = actor ? actor->GetEquippedEntryData(bLeft) : nullptr;
			const auto enchantment = item ? item->GetEnchantment() : nullptr;
			if (!enchantment || (bCharged && !(item->GetEnchantmentCharge().has_value() && *item->GetEnchantmentCharge() > 0.0))) {
				return;
			}
			a_out.push_back(enchantment->GetFormID());
				if (enchantment->data.baseEnchantment) {
					a_out.push_back(enchantment->data.baseEnchantment->GetFormID());
				}
		});
		return FormIn(a_builder, this, formComponent, key);
	}

	Program::Expr IsEquippedHasEnchantmentWithKeywordCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bLeft = leftHandComponent->GetBoolValue();
		const bool bCharged = chargedComponent->GetBoolValue();
		const auto key = BuiltinResources::FormSet(std::format("dcmf.enchantmentKeywords.{}.{}", bLeft ? "left" : "right", bCharged ? "charged" : "any"), "The enchantment of what the actor holds in that hand, its keywords.", [bLeft, bCharged](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			const auto item = actor ? actor->GetEquippedEntryData(bLeft) : nullptr;
			const auto enchantment = item ? item->GetEnchantment() : nullptr;
			if (!enchantment || (bCharged && !(item->GetEnchantmentCharge().has_value() && *item->GetEnchantmentCharge() > 0.0))) {
				return;
			}
			KeywordsOf(enchantment->As<RE::BGSKeywordForm>(), a_out);
		});
		return KeywordsIn(a_builder, keywordComponent->keyword, key);
	}

	Program::Expr IsOverEncumberedCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.overEncumbered", "Whether the actor is over-encumbered.", [](RE::Actor* a_actor) { return a_actor->IsOverEncumbered(); });
	}
	Program::Expr IsTrespassingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.trespassing", "Whether the actor is trespassing.", [](RE::Actor* a_actor) { return a_actor->IsTrespassing(); });
	}
	Program::Expr IsGuardCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.guard", "Whether the actor is a guard.", [](RE::Actor* a_actor) { return a_actor->IsGuard(); });
	}
	Program::Expr IsCrimeSearchingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.crimeSearching", "Whether the actor is searching for a criminal.", [](RE::Actor* a_actor) { return a_actor->GetActorRuntimeData().boolFlags.all(RE::Actor::BOOL_FLAGS::kCrimeSearch); });
	}
	Program::Expr IsCombatSearchingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.combatSearching", "Whether the actor is searching in combat.", [](RE::Actor* a_actor) { return a_actor->GetActorRuntimeData().boolBits.all(RE::Actor::BOOL_BITS::kSearchingInCombat); });
	}
	Program::Expr IsAboveWaterCondition::Compile(Program::Builder& a_builder) const
	{
		const float distance = Fixed(distanceNumericComponent);
		const float depth = Fixed(depthNumericComponent);
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(std::format("dcmf.aboveWater.{}.{}", HexOf(std::bit_cast<std::uint32_t>(distance)), HexOf(std::bit_cast<std::uint32_t>(depth))), "Whether the actor is above water by the distance, the water at least the depth.", OffActor([distance, depth](RE::Actor* a_actor) {
			auto cell = a_actor->parentCell;
			if (!cell) {
				return 0.f;
			}
			RE::NiPoint3 pos = a_actor->GetPosition();
			float waterHeight;
			if (!cell->GetWaterHeight(pos, waterHeight) || pos.z <= waterHeight + distance) {
				return 0.f;
			}
			RE::NiPoint3 waterPos = pos;
			waterPos.z = waterHeight - depth;
			RE::hkpWorldRayCastInput raycastInput;
			RE::hkpWorldRayCastOutput raycastOutput;
			RE::CFilter collisionFilterInfo{};
			a_actor->GetCollisionFilterInfo(collisionFilterInfo);
			const std::uint16_t collisionGroup = collisionFilterInfo.filter >> 16;
			raycastInput.filterInfo.filter = (static_cast<std::uint32_t>(collisionGroup) << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);
			raycastInput.from = Utils::NiPointToHkVector(pos, true);
			raycastInput.to = Utils::NiPointToHkVector(waterPos, true);
			auto world = cell->GetbhkWorld();
			if (!world) {
				return 0.f;
			}
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->GetWorld1()->CastRay(raycastInput, raycastOutput);
			}
			return raycastOutput.HasHit() ? 0.f : 1.f;
		}))));
	}

	Program::Expr MagicEffectElapsedTimeCondition::Compile(Program::Builder& a_builder) const
	{
		auto* effect = FormAs<RE::EffectSetting>(FixedForm(formComponent));
		if (!effect) {
			return a_builder.False();
		}
		// Any active instance holding the comparison: above, the longest one; below, the shortest
		using Comparison = Conditions::ComparisonOperator;
		const auto comparison = comparisonComponent->comparisonOperator;
		const bool bLongest = comparison != Comparison::kLess && comparison != Comparison::kLessEqual;
		const auto key = BuiltinResources::Number(std::format("dcmf.effectElapsed.{}.{}", HexOf(effect->GetFormID()), bLongest ? "longest" : "shortest"), "Seconds the magic effect has been active on the actor.", OffActor([effect, bLongest](RE::Actor* a_actor) {
			float best = std::numeric_limits<float>::quiet_NaN();
			ForEachActiveOf(a_actor, effect, [&](float a_seconds) {
				best = std::isnan(best) ? a_seconds : bLongest ? (std::max)(best, a_seconds) : (std::min)(best, a_seconds);
			});
			return best;
		}));
		return a_builder.Compare(a_builder.Resource(key), comparisonComponent, numericComponent);
	}

	Program::Expr IsWornInSlotCondition::Compile(Program::Builder& a_builder) const
	{
		const auto slot = StaticSlot(slotComponent);
		if (!slot) {
			return a_builder.False();
		}
		return a_builder.HasAny(a_builder.ResourceSet(BuiltinResources::FormSet(std::string(BuiltinResources::kWornSlots), "The biped slots the actor wears something in.", [](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			for (std::uint32_t i = 0; actor && i < 32; ++i) {
				if (actor->GetWornArmor(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1u << i), true)) {
					a_out.push_back(i);
				}
			}
		}, kWornInterval)), { *slot });
	}

	Program::Expr IsGhostCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.ghost", "Whether the actor is a ghost.", [](RE::Actor* a_actor) { return a_actor->IsGhost(); });
	}
	Program::Expr IsSwimmingCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.swimming", "Whether the actor is swimming.", [](RE::Actor* a_actor) { const auto state = a_actor->AsActorState(); return state && state->IsSwimming(); });
	}
	Program::Expr IsStaggeredCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.staggered", "Whether the actor is staggered.", [](RE::Actor* a_actor) { const auto state = a_actor->AsActorState(); return state && state->actorState2.staggered; });
	}
	Program::Expr CastingSpellCondition::Compile(Program::Builder& a_builder) const
	{
		const auto source = static_cast<std::int32_t>(Fixed(castingSourceComponent));
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(std::format("dcmf.castingSpell.{}", source), "Whether the actor casts from the source, 2 being both hands.", OffActor([source](RE::Actor* a_actor) {
			if (source == 2) {
				const auto caster = a_actor->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand);
				return caster && caster->GetIsDualCasting() ? 1.f : 0.f;
			}
			const auto castingSource = static_cast<RE::MagicSystem::CastingSource>(source);
			if (castingSource < RE::MagicSystem::CastingSource::kLeftHand || castingSource > RE::MagicSystem::CastingSource::kRightHand) {
				return 0.f;
			}
			const auto caster = a_actor->GetMagicCaster(castingSource);
			return caster && !caster->GetIsDualCasting() && caster->state > RE::MagicCaster::State::kNone && caster->state < RE::MagicCaster::State::kUnk07 ? 1.f : 0.f;
		}))));
	}

	Program::Expr HasBoundWeaponEquippedCondition::Compile(Program::Builder& a_builder) const
	{
		const bool bLeft = boolComponent->GetBoolValue();
		return a_builder.Truthy(a_builder.Resource(BuiltinResources::Number(bLeft ? "dcmf.boundWeapon.left" : "dcmf.boundWeapon.right", "Whether the actor holds a bound weapon in that hand.", OffActor([bLeft](RE::Actor* a_actor) {
			const auto form = a_actor->GetEquippedObject(bLeft);
			const auto weapon = form && form->formType == RE::FormType::Weapon ? form->As<RE::TESObjectWEAP>() : nullptr;
			return weapon && weapon->IsBound() ? 1.f : 0.f;
		}))));
	}

	Program::Expr IsOnStairsCondition::Compile(Program::Builder& a_builder) const
	{
		return ActorFlag(a_builder, "dcmf.onStairs", "Whether the actor is on stairs.", [](RE::Actor* a_actor) { const auto controller = a_actor->GetCharController(); return controller && controller->flags.any(RE::CHARACTER_FLAGS::kOnStairs); });
	}
	Program::Expr SurfaceMaterialCondition::Compile(Program::Builder& a_builder) const
	{
		const auto key = BuiltinResources::FormSet("dcmf.surfaceMaterial", "The material the actor stands on.", [](RE::TESObjectREFR* a_refr, std::vector<RE::FormID>& a_out) {
			const auto actor = ActorOf(a_refr);
			if (const auto controller = actor ? actor->GetCharController() : nullptr) {
				a_out.push_back(static_cast<RE::FormID>(*SKSE::stl::adjust_pointer<RE::MATERIAL_ID>(controller, 0x304)));
			}
		});
		return a_builder.HasAny(a_builder.ResourceSet(key), { static_cast<std::uint64_t>(static_cast<RE::FormID>(GetRequiredMaterialID())) });
	}

}
