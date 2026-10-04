#include "AnimationGraph.h"
#include "BodyTypes.h"
#include "Hook.h"
#include "apply/Applier.h"
#include "apply/DynamicData.h"
#include "mfgfix/MfgFix.h"
#include "ClipPlayer.h"
#include "Conditions.h"
#include "Functions.h"
#include "Resources.h"
#include "BuiltinResources.h"
#include "Triggers.h"
#include "apply/Entries.h"
#include "scan/Scan.h"
#include "Settings.h"
#include "UI/UIManager.h"
#include "UIHooks.h"

#include <spdlog/sinks/basic_file_sink.h>

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostLoad:
		// After every plugin's own load, so a site skee64 patched is called on through
		Apply::DynamicData::Install();
		break;
	case SKSE::MessagingInterface::kDataLoaded:
		UI::UIManager::GetSingleton().DisplayWelcomeBanner();
		BuiltinResources::Register();
		Settings::ApplyLoadOrderDefaults();
		Conditions::RegisterConditions();
		Functions::RegisterFunctions();
		Triggers::RegisterTriggers();
		// The fixed channel entries are there before any scan: enough for a modifier to drive
		Apply::Entries::GetSingleton().Sync();
		Apply::Install();
		ClipPlayer::Install();
		AnimationGraph::Install();
		BodyTypes::Load();
		Scan::Scanner::GetSingleton().Start();
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		Resources::InvalidateAll();
		break;
	}
}

namespace
{
	void InitializeLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= fmt::format("{}.log"sv, Plugin::SHORT_NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v"s);
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("{} v{}"sv, Plugin::SHORT_NAME, Plugin::VERSION_STRING);

	SKSE::Init(a_skse);

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	Settings::Initialize();
	Settings::ReadSettings();

	// Mfg Fix's job is ours: said when it is loaded beside us, and its scripts' natives answered when it is not
	MfgFix::Detect();
	if (const auto* papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(MfgFix::Register)) {
		logger::error("mfg: the Papyrus natives could not be registered");
	}

	// Once, for every call this plugin patches: this CommonLibSSE-NG replaces the trampoline on each AllocTrampoline
	SKSE::AllocTrampoline(14 * 12);

	UIHooks::Install();

	if (!Hook::Install()) {
		logger::error("scheduler hook: {}", Hook::Status());
	}

	return true;
}
