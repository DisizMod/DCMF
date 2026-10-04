#include "DetectedProblems.h"

#include "ModRegistry.h"
#include "RegisteredMods.h"


void DetectedProblems::UpdateShowErrorBanner() const
{
}

void DetectedProblems::MarkOutdatedVersion()
{
	_bIsOutdated = true;
}

void DetectedProblems::AddMissingPluginName(std::string_view a_pluginName, REL::Version a_pluginVersion)
{
	WriteLocker locker(_dataLock);

	_missingPlugins.emplace(std::string(a_pluginName), a_pluginVersion);
}

void DetectedProblems::AddInvalidPluginName(std::string_view a_pluginName, REL::Version a_pluginVersion)
{
	WriteLocker locker(_dataLock);

	_invalidPlugins.emplace(std::string(a_pluginName), a_pluginVersion);
}

bool DetectedProblems::CheckForSubModsSharingPriority()
{
	WriteLocker locker(_dataLock);

	_subModsSharingPriority.clear();

	std::map<int32_t, std::set<const SubMod*>> byPriority;

	ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_registeredMod) {
		a_registeredMod->ForEachSubMod([&](SubMod* a_subMod) {
			if (!a_subMod->IsDisabled()) {
				byPriority[a_subMod->GetPriority()].insert(a_subMod);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return RE::BSVisit::BSVisitControl::kContinue;
	});

	for (auto& [priority, subMods] : byPriority) {
		if (subMods.size() > 1) {
			_subModsSharingPriority.emplace(priority, subMods);
		}
	}

	return !_subModsSharingPriority.empty();
}

bool DetectedProblems::CheckForSubModsWithInvalidEntries()
{
	WriteLocker locker(_dataLock);

	_subModsWithInvalidEntries.clear();

	ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_registeredMod) {
		a_registeredMod->ForEachSubMod([&](SubMod* a_subMod) {
			if (a_subMod->HasInvalidConditions() || a_subMod->HasInvalidFunctions()) {
				_subModsWithInvalidEntries.insert(a_subMod);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return RE::BSVisit::BSVisitControl::kContinue;
	});

	return !_subModsWithInvalidEntries.empty();
}

bool DetectedProblems::CheckForRegisteredModsWithInvalidEntries()
{
	WriteLocker locker(_dataLock);

	_registeredModsWithInvalidEntries.clear();

	ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_registeredMod) {
		if (a_registeredMod->HasInvalidConditions(true) || a_registeredMod->HasInvalidFunctions()) {
			_registeredModsWithInvalidEntries.insert(a_registeredMod);
		}
		return RE::BSVisit::BSVisitControl::kContinue;
	});

	return !_registeredModsWithInvalidEntries.empty();
}

bool DetectedProblems::CheckForProblems()
{
	bool bFound = CheckForSubModsSharingPriority();
	bFound |= CheckForSubModsWithInvalidEntries();
	bFound |= CheckForRegisteredModsWithInvalidEntries();

	return bFound;
}

DetectedProblems::Severity DetectedProblems::GetProblemSeverity() const
{
	ReadLocker locker(_dataLock);

	if (_bIsOutdated || !_missingPlugins.empty() || !_invalidPlugins.empty() ||
		!_subModsWithInvalidEntries.empty() || !_registeredModsWithInvalidEntries.empty()) {
		return Severity::kError;
	}

	if (!_subModsSharingPriority.empty()) {
		return Severity::kWarning;
	}

	return Severity::kNone;
}

std::string_view DetectedProblems::GetProblemMessage() const
{
	ReadLocker locker(_dataLock);

	if (_bIsOutdated) {
		return "DCMF is outdated!"sv;
	}

	if (!_missingPlugins.empty()) {
		return "Missing plugins detected!"sv;
	}

	if (!_invalidPlugins.empty()) {
		return "Outdated plugins detected!"sv;
	}

	if (!_subModsWithInvalidEntries.empty() || !_registeredModsWithInvalidEntries.empty()) {
		return "Invalid conditions or functions detected!"sv;
	}

	if (!_subModsSharingPriority.empty()) {
		return "Mods sharing priority detected!"sv;
	}

	return ""sv;
}

void DetectedProblems::ForEachMissingPlugin(const std::function<void(const std::pair<std::string, REL::Version>&)>& a_func) const
{
	ReadLocker locker(_dataLock);

	for (const auto& entry : _missingPlugins) {
		a_func(entry);
	}
}

void DetectedProblems::ForEachInvalidPlugin(const std::function<void(const std::pair<std::string, REL::Version>&)>& a_func) const
{
	ReadLocker locker(_dataLock);

	for (const auto& entry : _invalidPlugins) {
		a_func(entry);
	}
}

void DetectedProblems::ForEachSubModSharingPriority(const std::function<void(const SubMod*)>& a_func) const
{
	ReadLocker locker(_dataLock);

	for (const auto& subMods : _subModsSharingPriority | std::views::values) {
		for (const auto& subMod : subMods) {
			a_func(subMod);
		}
	}
}

void DetectedProblems::ForEachSubModWithInvalidEntries(const std::function<void(const SubMod*)>& a_func) const
{
	ReadLocker locker(_dataLock);

	for (const auto& subMod : _subModsWithInvalidEntries) {
		a_func(subMod);
	}
}

void DetectedProblems::ForEachRegisteredModWithInvalidEntries(const std::function<void(const RegisteredMod*)>& a_func) const
{
	ReadLocker locker(_dataLock);

	for (const auto& registeredMod : _registeredModsWithInvalidEntries) {
		a_func(registeredMod);
	}
}
