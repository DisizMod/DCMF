#include "Actors.h"

#include "Program.h"
#include "Utils.h"

#include <limits>

namespace Actors
{
	namespace
	{
		// ActorTypeNPC, Skyrim.esm. On the race of everything that counts as a
		// person and on nothing that counts as a creature.
		constexpr RE::FormID kActorTypeNPC = 0x13794;

		// Two tests, and both are wanted: ActorTypeNPC is the intent, since it
		// leaves out draugr and falmer, which have FaceGen heads and are not
		// people; the FaceGen flag is the guard, since a race without one has
		// nothing for a morph to move.
		//
		// The keyword is read off the race, not the actor base: that is where it
		// is authored, and whether TESNPC's own HasKeyword consults the race is
		// the game's business, not something to lean on.
		[[nodiscard]] bool IsPerson(RE::Actor* a_actor, const RE::BGSKeyword* a_actorTypeNPC)
		{
			const auto* race = a_actor->GetRace();
			return race && a_actorTypeNPC &&
			       race->data.flags.all(RE::RACE_DATA::Flag::kFaceGenHead) &&
			       race->HasKeyword(a_actorTypeNPC);
		}
	}

	void Capture(const Filter& a_filter, std::vector<RE::ActorHandle>& a_outHandles)
	{
		a_outHandles.clear();

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		if (player->Is3DLoaded()) {
			a_outHandles.emplace_back(player->GetHandle());
		}

		if (!a_filter.bAllActors) {
			return;
		}

		auto* lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return;
		}

		const auto* actorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(kActorTypeNPC);
		const auto playerPosition = player->GetPosition();
		const auto maxDistanceSquared = a_filter.maxDistance * a_filter.maxDistance;

		for (auto& handle : lists->highActorHandles) {
			const auto actor = handle.get();
			if (!actor || actor.get() == player || !actor->Is3DLoaded()) {
				continue;
			}

			if (a_filter.maxDistance > 0.f &&
				actor->GetPosition().GetSquaredDistance(playerPosition) > maxDistanceSquared) {
				continue;
			}

			if (!IsPerson(actor.get(), actorTypeNPC)) {
				continue;
			}

			a_outHandles.emplace_back(handle);
		}
	}

	void CaptureWorld(const Program::Compiled& a_program, Facts::WorldFacts& a_out)
	{
		a_out.values.assign(a_program.probes.size(), std::numeric_limits<float>::quiet_NaN());
		for (const auto probe : a_program.worldProbes) {
			a_out.values[probe] = a_program.probes[probe].read(nullptr);
		}
	}

	bool Gather(const RE::ActorHandle& a_handle, const Program::Compiled& a_program, Facts::ActorFacts& a_out)
	{
		const auto actor = a_handle.get();
		if (!actor || !actor->Is3DLoaded()) {
			return false;
		}

		a_out.Reset();
		a_out.formID = actor->GetFormID();

		// Each subject once, then each reading off its subject; the world's are the tick's
		static std::vector<RE::TESObjectREFRPtr> subjects;
		Program::ResolveSubjects(a_program, actor.get(), subjects);
		a_out.values.assign(a_program.probes.size(), std::numeric_limits<float>::quiet_NaN());
		a_out.sets.resize(a_program.probes.size());
		for (const auto index : a_program.actorProbes) {
			const auto& probe = a_program.probes[index];
			if (probe.bSet) {
				a_out.sets[index].clear();
				if (auto* subject = subjects[probe.subject].get()) {
					probe.readSet(subject, a_out.sets[index]);
				}
			} else {
				a_out.values[index] = probe.read(subjects[probe.subject].get());
			}
		}

		return true;
	}
}
