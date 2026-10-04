#pragma once

#include "ConditionTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class SubMod;

enum class VariantMode : uint8_t
{
	kRandom = 0,
	kSequential = 1
};

class Variant
{
public:
	Variant(uint16_t a_index, std::string_view a_filename, int32_t a_order) :
		_index(a_index),
		_filename(a_filename),
		_order(a_order)
	{}

	uint16_t GetIndex() const { return _index; }
	std::string_view GetFilename() const { return _filename; }
	bool IsDisabled() const { return _bDisabled; }
	void SetDisabled(bool a_bDisable) { _bDisabled = a_bDisable; }
	bool ShouldPlayOnce() const { return _bPlayOnce; }
	void SetPlayOnce(bool a_bPlayOnce) { _bPlayOnce = a_bPlayOnce; }

	float GetWeight() const { return _weight; }
	void SetWeight(float a_weight) { _weight = a_weight; }

	int32_t GetOrder() const { return _order; }
	void SetOrder(int32_t a_order) { _order = a_order; }

	bool ShouldSaveToJson(VariantMode a_variantMode) const;

	void ResetSettings();

protected:
	uint16_t _index = static_cast<uint16_t>(-1);
	std::string _filename;
	bool _bDisabled = false;
	bool _bPlayOnce = false;

	float _weight = 1.f;
	int32_t _order;
};

class Variants
{
public:
	Variants(std::vector<Variant>& a_variants) :
		_variants(std::move(a_variants))
	{
		UpdateVariantCache();
	}

	Variants(std::vector<Variant>&& a_variants) :
		_variants(std::move(a_variants))
	{
		UpdateVariantCache();
	}

	void UpdateVariantCache();
	void ResetSettings();

	[[nodiscard]] Conditions::StateDataScope GetVariantStateScope() const { return _variantStateScope; }
	void SetVariantStateScope(Conditions::StateDataScope a_scope) { _variantStateScope = a_scope; }

	[[nodiscard]] VariantMode GetVariantMode() const { return _variantMode; }
	void SetVariantMode(VariantMode a_variantMode) { _variantMode = a_variantMode; }

	[[nodiscard]] SubMod* GetParentSubMod() const;
	void SetParentSubMod(SubMod* a_subMod) { _parentSubMod = a_subMod; }

	// A clip's variants come and go: added at the end, removed by index with the rest renumbered
	Variant& AddVariant(std::string_view a_name);
	void RemoveVariant(uint16_t a_index);
	[[nodiscard]] Variant* GetVariant(uint16_t a_index);

	bool ShouldBlendBetweenVariants() const { return _bShouldBlendBetweenVariants; }
	void SetShouldBlendBetweenVariants(bool a_bEnable) { _bShouldBlendBetweenVariants = a_bEnable; }

	bool ShouldResetRandomOnLoopOrEcho() const { return _bShouldResetRandomOnLoopOrEcho; }
	void SetShouldResetRandomOnLoopOrEcho(bool a_bEnable) { _bShouldResetRandomOnLoopOrEcho = a_bEnable; }

	bool ShouldSharePlayedHistory() const { return _bShouldSharePlayedHistory; }
	void SetShouldSharePlayedHistory(bool a_bEnable) { _bShouldSharePlayedHistory = a_bEnable; }

	RE::BSVisit::BSVisitControl ForEachVariant(const std::function<RE::BSVisit::BSVisitControl(Variant&)>& a_func);
	RE::BSVisit::BSVisitControl ForEachVariant(const std::function<RE::BSVisit::BSVisitControl(const Variant&)>& a_func) const;

	void SwapVariants(int32_t a_variantIndexA, int32_t a_variantIndexB);

	size_t GetVariantCount() const { return _variants.size(); }
	size_t GetActiveVariantCount() const;
	size_t GetSequentialVariantCount() const;

	Variant* GetActiveVariant(size_t a_variantIndex) const;

protected:
	SubMod* _parentSubMod = nullptr;
	std::vector<Variant> _variants;
	bool _bShouldBlendBetweenVariants = true;
	bool _bShouldResetRandomOnLoopOrEcho = true;
	bool _bShouldSharePlayedHistory = false;

	mutable SharedLock _lock;
	std::vector<Variant*> _activeVariants;
	std::vector<Variant*> _sequentialVariants;
	std::vector<Variant*> _randomVariants;

	VariantMode _variantMode = VariantMode::kRandom;

	float _totalWeight = 0.f;
	std::vector<float> _cumulativeWeights;
	Conditions::StateDataScope _variantStateScope = Conditions::StateDataScope::kActor;
};
