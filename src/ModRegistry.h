#pragma once

#include "Conditions.h"
#include "Functions.h"
#include "Jobs.h"
#include "RegisteredMods.h"
#include "SharedTypes.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ModRegistry
{
public:
	static ModRegistry& GetSingleton()
	{
		static ModRegistry singleton;
		return singleton;
	}

	// ---- the mod tree ----------------------------------------------------

	RegisteredMod* AddRegisteredMod(std::unique_ptr<RegisteredMod>& a_registeredMod);
	void ForEachRegisteredMod(const std::function<void(RegisteredMod*)>& a_func) const;
	void ForEachSortedRegisteredMod(const std::function<void(RegisteredMod*)>& a_func) const;
	void SortMods();

	// ---- factories -------------------------------------------------------

	// Whether a plugin implementing custom conditions is present. Nothing
	// registers one here, so this is always false.
	bool IsPluginLoaded(std::string_view a_pluginName, REL::Version a_pluginVersion) const;

	RegisteredMod* GetRegisteredModByName(std::string_view a_name) const;

	// The two weapon keywords IsAttackTypeKeyword and friends compare against.
	void LoadKeywords() const;

	static inline bool bKeywordsLoaded = false;
	static inline RE::BGSKeyword* kywd_weapTypeWarhammer = nullptr;
	static inline RE::BGSKeyword* kywd_weapTypeBattleaxe = nullptr;

	void AddConditionFactory(std::string_view a_name, Conditions::ConditionFactory a_factory);
	// A condition another plugin declared: its factory carries the declaration
	void AddPluginConditionFactory(std::string_view a_name, std::function<Conditions::ICondition*()> a_factory);
	void AddFunctionFactory(std::string_view a_name, Functions::FunctionFactory a_factory);

	[[nodiscard]] bool HasConditionFactory(std::string_view a_conditionName) const;
	[[nodiscard]] bool HasFunctionFactory(std::string_view a_functionName) const;
	void ForEachConditionFactory(const std::function<void(std::string_view, std::function<std::unique_ptr<Conditions::ICondition>()>)>& a_func) const;
	void ForEachFunctionFactory(const std::function<void(std::string_view, std::function<std::unique_ptr<Functions::IFunction>()>)>& a_func) const;
	[[nodiscard]] std::unique_ptr<Conditions::ICondition> CreateCondition(std::string_view a_conditionName);
	[[nodiscard]] std::unique_ptr<Functions::IFunction> CreateFunction(std::string_view a_functionName);

	[[nodiscard]] REL::Version GetPluginVersion(std::string_view a_pluginName) const;

	// ---- state -----------------------------------------------------------

	// A clip starting (a loop) or starting over (an echo) on the actor: what its conditions read that resets on either
	// is read again, so a Random rolls again
	void OnLoopOrEcho(const SubMod* a_clip, RE::TESObjectREFR* a_refr);

	// ---- jobs ------------------------------------------------------------

	template <class T, typename... Args>
	void QueueJob(Args&&... a_args)
	{
		WriteLocker locker(_jobsLock);

		static_assert(std::is_base_of_v<Jobs::GenericJob, T>);
		_jobs.push_back(std::make_unique<T>(std::forward<Args>(a_args)...));
	}

	void RunJobs();

private:
	ModRegistry() = default;
	ModRegistry(const ModRegistry&) = delete;
	ModRegistry(ModRegistry&&) = delete;
	~ModRegistry() = default;

	ModRegistry& operator=(const ModRegistry&) = delete;
	ModRegistry& operator=(ModRegistry&&) = delete;

	mutable SharedLock _modsLock;
	std::vector<std::unique_ptr<RegisteredMod>> _registeredMods;
	std::vector<RegisteredMod*> _sortedMods;

	mutable SharedLock _factoriesLock;
	std::map<std::string, Conditions::ConditionFactory, CaseInsensitiveCompare> _conditionFactories;
	std::map<std::string, std::function<Conditions::ICondition*()>, CaseInsensitiveCompare> _pluginConditionFactories;
	std::map<std::string, Functions::FunctionFactory, CaseInsensitiveCompare> _functionFactories;

	mutable SharedLock _jobsLock;
	std::vector<std::unique_ptr<Jobs::GenericJob>> _jobs;
};
