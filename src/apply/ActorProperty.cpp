#include "ActorProperty.h"

#include "ActorState.h"

namespace Apply
{
	void ActorProperty::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		auto* base = actor ? actor->GetActorBase() : nullptr;
		if (!actor || !base) {
			return;
		}
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();

		a_state.actorProperty.With([&](ActorPropertyData& a_data) {
			const auto& samples = sampled ? sampled->numbers[static_cast<std::size_t>(Kind::kActorProperty)] : std::vector<NumberSample>{};
			const auto find = [&](Slot a_slot) -> const NumberSample* {
				const auto it = std::ranges::find(samples, static_cast<std::uint32_t>(a_slot), &NumberSample::index);
				return it != samples.end() ? &*it : nullptr;
			};

			// Weight: the base's own value, then a reset of the 3D to take it; never on a dead or unloaded actor
			if (const auto* weight = bRetired ? nullptr : find(Slot::kWeight); weight && actor->Is3DLoaded() && !actor->IsDead()) {
				if (!a_data.bWeightHeld) {
					a_data.bWeightHeld = true;
					a_data.originalWeight = base->weight;
					a_data.appliedWeight = base->weight;
				}
				const auto value = std::clamp(weight->value, 0.f, 100.f);
				if (value != a_data.appliedWeight) {
					base->weight = value;
					a_data.appliedWeight = value;
					actor->DoReset3D(true);
				}
			} else if (a_data.bWeightHeld) {
				if (base->weight == a_data.appliedWeight) {
					base->weight = a_data.originalWeight;
					if (actor->Is3DLoaded()) {
						actor->DoReset3D(true);
					}
				}
				a_data.bWeightHeld = false;
			}

			// Alpha: the engine walks the 3D each time, so only on change
			if (const auto* alpha = bRetired ? nullptr : find(Slot::kAlpha); alpha && actor->Is3DLoaded()) {
				if (!a_data.bAlphaHeld) {
					a_data.bAlphaHeld = true;
					a_data.originalAlpha = actor->GetAlpha();
					a_data.appliedAlpha = a_data.originalAlpha;
				}
				const auto value = std::clamp(alpha->value, 0.f, 1.f);
				if (value != a_data.appliedAlpha) {
					actor->SetAlpha(value);
					a_data.appliedAlpha = value;
				}
			} else if (a_data.bAlphaHeld) {
				if (actor->Is3DLoaded()) {
					actor->SetAlpha(a_data.originalAlpha);
				}
				a_data.bAlphaHeld = false;
			}
		});
	}
}
