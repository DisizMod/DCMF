#include "Variants.h"

#include <algorithm>


bool Variant::ShouldSaveToJson(VariantMode a_variantMode) const
{
	if (_bDisabled) {
		return true;
	}

	if (_bPlayOnce) {
		return true;
	}

	if (a_variantMode == VariantMode::kRandom && _weight != 1.f) {
		return true;
	}

	if (a_variantMode == VariantMode::kSequential) {
		return true;
	}

	return false;
}

void Variant::ResetSettings()
{
	_weight = 1.f;
	_bDisabled = false;
	_bPlayOnce = false;
}

void Variants::UpdateVariantCache()
{
	WriteLocker locker(_lock);

	_activeVariants.clear();
	_sequentialVariants.clear();
	_randomVariants.clear();

	switch (_variantMode) {
	case VariantMode::kRandom:
		{
			_totalWeight = 0.f;
			_cumulativeWeights.clear();

			std::ranges::sort(_variants, [](const Variant& a, const Variant& b) {
				if (a.ShouldPlayOnce() && b.ShouldPlayOnce()) {
					return a.GetOrder() < b.GetOrder();
				}
				if (a.ShouldPlayOnce() != b.ShouldPlayOnce()) {
					return a.ShouldPlayOnce();
				}
				return a.GetWeight() > b.GetWeight();
			});

			for (auto& variant : _variants) {
				if (!variant.IsDisabled()) {
					_activeVariants.emplace_back(&variant);
					if (variant.ShouldPlayOnce()) {
						_sequentialVariants.emplace_back(&variant);
					} else {
						_totalWeight += variant.GetWeight();
						_randomVariants.emplace_back(&variant);
					}
				}
			}

			float weightSum = 0.f;
			for (const auto randomVariant : _randomVariants) {
				weightSum += randomVariant->GetWeight();
				_cumulativeWeights.emplace_back(_totalWeight > 0.f ? weightSum / _totalWeight : 0.f);
			}
		}
		break;

	case VariantMode::kSequential:
		std::ranges::sort(_variants, [](const Variant& a, const Variant& b) {
			return a.GetOrder() < b.GetOrder();
		});

		for (auto& variant : _variants) {
			if (!variant.IsDisabled()) {
				_activeVariants.emplace_back(&variant);
				_sequentialVariants.emplace_back(&variant);
			}
		}

		break;
	}
}

void Variants::ResetSettings()
{
	_bShouldBlendBetweenVariants = true;
	_bShouldResetRandomOnLoopOrEcho = true;
	_variantMode = VariantMode::kRandom;
	_variantStateScope = Conditions::StateDataScope::kActor;

	for (Variant& variant : _variants) {
		variant.ResetSettings();
	}

	UpdateVariantCache();
}

SubMod* Variants::GetParentSubMod() const
{
	return _parentSubMod;
}

Variant& Variants::AddVariant(std::string_view a_name)
{
	Variant& added = [&]() -> Variant& {
		WriteLocker locker(_lock);
		const auto index = static_cast<uint16_t>(_variants.size());
		return _variants.emplace_back(index, a_name, static_cast<int32_t>(index));
	}();
	UpdateVariantCache();
	return added;
}

void Variants::RemoveVariant(uint16_t a_index)
{
	{
		WriteLocker locker(_lock);
		std::erase_if(_variants, [&](const Variant& a_variant) { return a_variant.GetIndex() == a_index; });
		for (uint16_t i = 0; i < _variants.size(); ++i) {
			_variants[i] = Variant(i, _variants[i].GetFilename(), i);
		}
	}
	UpdateVariantCache();
}

Variant* Variants::GetVariant(uint16_t a_index)
{
	ReadLocker locker(_lock);
	const auto it = std::ranges::find_if(_variants, [&](const Variant& a_variant) { return a_variant.GetIndex() == a_index; });
	return it != _variants.end() ? &*it : nullptr;
}

RE::BSVisit::BSVisitControl Variants::ForEachVariant(const std::function<RE::BSVisit::BSVisitControl(Variant&)>& a_func)
{
	using Result = RE::BSVisit::BSVisitControl;

	for (auto& variant : _variants) {
		const auto result = a_func(variant);
		if (result == Result::kStop) {
			return result;
		}
	}

	return Result::kContinue;
}

RE::BSVisit::BSVisitControl Variants::ForEachVariant(const std::function<RE::BSVisit::BSVisitControl(const Variant&)>& a_func) const
{
	using Result = RE::BSVisit::BSVisitControl;

	for (auto& variant : _variants) {
		const auto result = a_func(variant);
		if (result == Result::kStop) {
			return result;
		}
	}

	return Result::kContinue;
}

void Variants::SwapVariants(int32_t a_variantIndexA, int32_t a_variantIndexB)
{
	if (a_variantIndexA < static_cast<int32_t>(_variants.size()) &&
		a_variantIndexB < static_cast<int32_t>(_variants.size())) {
		_variants[a_variantIndexA].SetOrder(a_variantIndexB);
		_variants[a_variantIndexB].SetOrder(a_variantIndexA);
	}

	UpdateVariantCache();
}

size_t Variants::GetActiveVariantCount() const
{
	ReadLocker locker(_lock);

	return _activeVariants.size();
}

size_t Variants::GetSequentialVariantCount() const
{
	ReadLocker locker(_lock);

	return _sequentialVariants.size();
}

Variant* Variants::GetActiveVariant(size_t a_variantIndex) const
{
	ReadLocker locker(_lock);

	if (a_variantIndex < _activeVariants.size()) {
		return _activeVariants[a_variantIndex];
	}

	return nullptr;
}
