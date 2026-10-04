#include "HeadPart.h"

#include "ActorState.h"

namespace Apply
{
	const std::vector<HeadPart::Slot>& HeadPart::Slots()
	{
		using Type = RE::BGSHeadPart::HeadPartType;
		static const std::vector<Slot> slots{
			{ "hair"sv, Type::kHair },
			{ "eyes"sv, Type::kEyes },
			{ "brows"sv, Type::kEyebrows },
			{ "beard"sv, Type::kFacialHair },
			{ "scars"sv, Type::kScar },
		};
		return slots;
	}

	void HeadPart::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		auto* base = actor ? actor->GetActorBase() : nullptr;
		if (!actor || !base) {
			return;
		}
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();
		const auto& slots = Slots();

		a_state.headPart.With([&](HeadPartData& a_data) {
			const auto& samples = sampled ? sampled->references[static_cast<std::size_t>(Kind::kHeadPart)] : std::vector<ReferenceSample>{};

			if (!samples.empty() && !actor->IsPlayerRef()) {
				if (!a_data.bRefused) {
					a_data.bRefused = true;
					logger::info("headpart: {:08X} is not the player; its parts are left alone", actor->GetFormID());
				}
				return;
			}

			bool bChanged = false;

			// What is held now, swapped in where it differs from what was last put there
			for (const auto& sample : bRetired ? std::vector<ReferenceSample>{} : samples) {
				if (sample.index >= slots.size()) {
					logger::error("headpart: sample {} is past the {} head part slots, which are the whole table", sample.index, slots.size());
					continue;
				}
				auto* part = RE::TESForm::LookupByID<RE::BGSHeadPart>(static_cast<RE::FormID>(sample.value));
				if (!part || part->type.get() != slots[sample.index].type) {
					continue;
				}
				auto& held = a_data.held[sample.index];
				if (!held.bHeld) {
					auto* original = base->GetCurrentHeadPartByType(slots[sample.index].type);
					held.bHeld = true;
					held.original = original ? original->GetFormID() : 0;
					held.applied = held.original;
				}
				if (held.applied != part->GetFormID()) {
					base->ChangeHeadPart(part);
					held.applied = part->GetFormID();
					bChanged = true;
				}
			}

			// What was held and is not now goes back to the part the actor had
			for (std::size_t i = 0; i < slots.size(); ++i) {
				auto& held = a_data.held[i];
				if (!held.bHeld) {
					continue;
				}
				const bool bStillHeld = !bRetired && std::ranges::any_of(samples, [&](const ReferenceSample& a_sample) { return a_sample.index == i; });
				if (bStillHeld) {
					continue;
				}
				if (auto* original = RE::TESForm::LookupByID<RE::BGSHeadPart>(held.original); original && held.applied != held.original) {
					base->ChangeHeadPart(original);
					bChanged = true;
				}
				held = {};
			}

			if (bChanged && actor->Is3DLoaded()) {
				actor->DoReset3D(false);
			}
		});
	}
}
