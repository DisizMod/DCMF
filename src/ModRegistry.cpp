#include "ModRegistry.h"
#include "BuiltinResources.h"

#include "Conditions.h"
#include "DetectedProblems.h"
#include "Functions.h"

#include <algorithm>

RegisteredMod* ModRegistry::AddRegisteredMod(std::unique_ptr<RegisteredMod>& a_registeredMod)
{
	WriteLocker locker(_modsLock);

	auto& entry = _registeredMods.emplace_back(std::move(a_registeredMod));
	_sortedMods.push_back(entry.get());

	return entry.get();
}

void ModRegistry::ForEachRegisteredMod(const std::function<void(RegisteredMod*)>& a_func) const
{
	ReadLocker locker(_modsLock);

	for (const auto& registeredMod : _registeredMods) {
		a_func(registeredMod.get());
	}
}

void ModRegistry::ForEachSortedRegisteredMod(const std::function<void(RegisteredMod*)>& a_func) const
{
	ReadLocker locker(_modsLock);

	for (const auto registeredMod : _sortedMods) {
		a_func(registeredMod);
	}
}

void ModRegistry::SortMods()
{
	WriteLocker locker(_modsLock);

	std::ranges::sort(_sortedMods, [](const RegisteredMod* a_lhs, const RegisteredMod* a_rhs) {
		return CaseInsensitiveCompare{}(std::string(a_lhs->GetName()), std::string(a_rhs->GetName()));
	});
}

void ModRegistry::AddConditionFactory(std::string_view a_name, Conditions::ConditionFactory a_factory)
{
	WriteLocker locker(_factoriesLock);

	_conditionFactories.emplace(std::string(a_name), a_factory);
}

void ModRegistry::AddPluginConditionFactory(std::string_view a_name, std::function<Conditions::ICondition*()> a_factory)
{
	WriteLocker locker(_factoriesLock);

	_pluginConditionFactories.emplace(std::string(a_name), std::move(a_factory));
}

void ModRegistry::AddFunctionFactory(std::string_view a_name, Functions::FunctionFactory a_factory)
{
	WriteLocker locker(_factoriesLock);

	_functionFactories.emplace(std::string(a_name), a_factory);
}

bool ModRegistry::HasConditionFactory(std::string_view a_conditionName) const
{
	ReadLocker locker(_factoriesLock);

	return _conditionFactories.contains(std::string(a_conditionName)) || _pluginConditionFactories.contains(std::string(a_conditionName));
}

bool ModRegistry::HasFunctionFactory(std::string_view a_functionName) const
{
	ReadLocker locker(_factoriesLock);

	return _functionFactories.contains(std::string(a_functionName));
}

void ModRegistry::ForEachConditionFactory(const std::function<void(std::string_view, std::function<std::unique_ptr<Conditions::ICondition>()>)>& a_func) const
{
	ReadLocker locker(_factoriesLock);

	for (const auto& [name, factory] : _conditionFactories) {
		a_func(name, [factory]() { return std::unique_ptr<Conditions::ICondition>(factory()); });
	}
	for (const auto& [name, factory] : _pluginConditionFactories) {
		a_func(name, [factory]() { return std::unique_ptr<Conditions::ICondition>(factory()); });
	}
}

void ModRegistry::ForEachFunctionFactory(const std::function<void(std::string_view, std::function<std::unique_ptr<Functions::IFunction>()>)>& a_func) const
{
	ReadLocker locker(_factoriesLock);

	for (const auto& [name, factory] : _functionFactories) {
		a_func(name, [factory]() { return std::unique_ptr<Functions::IFunction>(factory()); });
	}
}

std::unique_ptr<Conditions::ICondition> ModRegistry::CreateCondition(std::string_view a_conditionName)
{
	ReadLocker locker(_factoriesLock);

	if (const auto it = _conditionFactories.find(std::string(a_conditionName)); it != _conditionFactories.end()) {
		return std::unique_ptr<Conditions::ICondition>(it->second());
	}
	if (const auto it = _pluginConditionFactories.find(std::string(a_conditionName)); it != _pluginConditionFactories.end()) {
		return std::unique_ptr<Conditions::ICondition>(it->second());
	}

	return nullptr;
}

std::unique_ptr<Functions::IFunction> ModRegistry::CreateFunction(std::string_view a_functionName)
{
	ReadLocker locker(_factoriesLock);

	if (const auto it = _functionFactories.find(std::string(a_functionName)); it != _functionFactories.end()) {
		return std::unique_ptr<Functions::IFunction>(it->second());
	}

	return nullptr;
}

REL::Version ModRegistry::GetPluginVersion(std::string_view) const
{
	return { 0, 0, 0 };
}

void ModRegistry::OnLoopOrEcho(const SubMod* a_clip, RE::TESObjectREFR* a_refr)
{
	if (!a_clip || !a_refr) {
		return;
	}
	BuiltinResources::OnLoopOrEcho(a_clip, a_refr);
}

void ModRegistry::RunJobs()
{
	std::vector<std::unique_ptr<Jobs::GenericJob>> jobs;
	{
		WriteLocker locker(_jobsLock);
		jobs.swap(_jobs);
	}

	for (const auto& job : jobs) {
		job->Run();
	}
}

bool ModRegistry::IsPluginLoaded(std::string_view, REL::Version) const
{
	return false;
}

RegisteredMod* ModRegistry::GetRegisteredModByName(std::string_view a_name) const
{
	ReadLocker locker(_modsLock);

	for (const auto& mod : _registeredMods) {
		if (mod->GetName() == a_name) {
			return mod.get();
		}
	}

	return nullptr;
}

void ModRegistry::LoadKeywords() const
{
	kywd_weapTypeWarhammer = RE::TESForm::LookupByID<RE::BGSKeyword>(0x6D930);
	kywd_weapTypeBattleaxe = RE::TESForm::LookupByID<RE::BGSKeyword>(0x6D932);
	bKeywordsLoaded = true;
}
