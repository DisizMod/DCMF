#include "Settings.h"
#include <SimpleIni.h>

namespace
{
	void ReadBoolSetting(const CSimpleIniA& a_ini, const char* a_sectionName, const char* a_settingName, bool& a_setting)
	{
		if (a_ini.GetValue(a_sectionName, a_settingName)) {
			a_setting = a_ini.GetBoolValue(a_sectionName, a_settingName);
		}
	}

	void ReadFloatSetting(const CSimpleIniA& a_ini, const char* a_sectionName, const char* a_settingName, float& a_setting)
	{
		if (a_ini.GetValue(a_sectionName, a_settingName)) {
			a_setting = static_cast<float>(a_ini.GetDoubleValue(a_sectionName, a_settingName));
		}
	}

	void ReadUInt32Setting(const CSimpleIniA& a_ini, const char* a_sectionName, const char* a_settingName, uint32_t& a_setting)
	{
		if (a_ini.GetValue(a_sectionName, a_settingName)) {
			a_setting = static_cast<uint32_t>(a_ini.GetLongValue(a_sectionName, a_settingName));
		}
	}
}

void Settings::Initialize()
{
	uToggleUIKeyData[0] = 0x26;  // DIK_L
	uToggleUIKeyData[1] = 0;     // ctrl
	uToggleUIKeyData[2] = 1;     // shift
	uToggleUIKeyData[3] = 0;     // alt
}

void Settings::ReadSettings()
{
	Initialize();

	const auto readIni = [&](auto a_path) {
		CSimpleIniA ini;
		ini.SetUnicode();

		if (ini.LoadFile(a_path.data()) < 0) {
			return false;
		}

		// General
		ReadUInt32Setting(ini, "General", "uTargetMode", uTargetMode);
		ReadFloatSetting(ini, "General", "fTargetDistance", fTargetDistance);
		ReadFloatSetting(ini, "General", "fTickRate", fTickRate);
		ReadBoolSetting(ini, "General", "bPauseInRaceMenu", bPauseInRaceMenu);
		ReadFloatSetting(ini, "General", "fDefaultTransitionTime", fDefaultTransitionTime);

		// Tracking
		ReadFloatSetting(ini, "Tracking", "fTrackSpine1Share", fTrackSpine1Share);
		ReadFloatSetting(ini, "Tracking", "fTrackSpine2Share", fTrackSpine2Share);
		ReadFloatSetting(ini, "Tracking", "fTrackNeckShare", fTrackNeckShare);
		ReadFloatSetting(ini, "Tracking", "fTrackSpine1Yaw", fTrackSpine1Yaw);
		ReadFloatSetting(ini, "Tracking", "fTrackSpine1Pitch", fTrackSpine1Pitch);
		ReadFloatSetting(ini, "Tracking", "fTrackSpine2Yaw", fTrackSpine2Yaw);
		ReadFloatSetting(ini, "Tracking", "fTrackSpine2Pitch", fTrackSpine2Pitch);
		ReadFloatSetting(ini, "Tracking", "fTrackNeckYaw", fTrackNeckYaw);
		ReadFloatSetting(ini, "Tracking", "fTrackNeckPitch", fTrackNeckPitch);
		ReadFloatSetting(ini, "Tracking", "fTrackHeadYaw", fTrackHeadYaw);
		ReadFloatSetting(ini, "Tracking", "fTrackHeadPitch", fTrackHeadPitch);
		ReadFloatSetting(ini, "Tracking", "fPoseModeTransition", fPoseModeTransition);

		// Face
		ReadFloatSetting(ini, "Face", "fBlinkDownTime", fBlinkDownTime);
		ReadFloatSetting(ini, "Face", "fBlinkUpTime", fBlinkUpTime);
		ReadFloatSetting(ini, "Face", "fBlinkDelayMin", fBlinkDelayMin);
		ReadFloatSetting(ini, "Face", "fBlinkDelayMax", fBlinkDelayMax);
		ReadFloatSetting(ini, "Face", "fEyeMovementHeading", fEyeMovementHeading);
		ReadFloatSetting(ini, "Face", "fEyeMovementPitch", fEyeMovementPitch);
		ReadFloatSetting(ini, "Face", "fEyeMovementGapMin", fEyeMovementGapMin);
		ReadFloatSetting(ini, "Face", "fEyeMovementGapMax", fEyeMovementGapMax);
		ReadFloatSetting(ini, "Face", "fEyeMovementSpeed", fEyeMovementSpeed);

		// Patches: a patch's switch only when the user set it, otherwise what the load order says, once it is known
		ReadBoolSetting(ini, "Patches", "bPatchUbeEyeRotationChosen", bPatchUbeEyeRotationChosen);
		if (bPatchUbeEyeRotationChosen) {
			ReadBoolSetting(ini, "Patches", "bPatchUbeEyeRotation", bPatchUbeEyeRotation);
		}
		ReadFloatSetting(ini, "Patches", "fPatchUbeEyeLeft", fPatchUbeEyeLeft);
		ReadFloatSetting(ini, "Patches", "fPatchUbeEyeRight", fPatchUbeEyeRight);
		ReadFloatSetting(ini, "Patches", "fPatchUbeEyeUp", fPatchUbeEyeUp);
		ReadFloatSetting(ini, "Patches", "fPatchUbeEyeDown", fPatchUbeEyeDown);
		ReadFloatSetting(ini, "Patches", "fPatchUbeEyeRotationStrength", fPatchUbeEyeRotationStrength);
		ReadBoolSetting(ini, "Patches", "bPatchSmpBodyCollisionChosen", bPatchSmpBodyCollisionChosen);
		if (bPatchSmpBodyCollisionChosen) {
			ReadBoolSetting(ini, "Patches", "bPatchSmpBodyCollision", bPatchSmpBodyCollision);
		}

		// UI
		ReadBoolSetting(ini, "UI", "bEnableUI", bEnableUI);
		ReadBoolSetting(ini, "UI", "bShowWelcomeBanner", bShowWelcomeBanner);
		ReadBoolSetting(ini, "UI", "bShowTimings", bShowTimings);
		ReadUInt32Setting(ini, "UI", "uToggleUIKey", uToggleUIKeyData[0]);
		ReadUInt32Setting(ini, "UI", "uToggleUIKeyCtrl", uToggleUIKeyData[1]);
		ReadUInt32Setting(ini, "UI", "uToggleUIKeyShift", uToggleUIKeyData[2]);
		ReadUInt32Setting(ini, "UI", "uToggleUIKeyAlt", uToggleUIKeyData[3]);
		ReadFloatSetting(ini, "UI", "fUIScale", fUIScale);

		ReadBoolSetting(ini, "UI", "bShowActorWindow", bShowActorWindow);
		ReadFloatSetting(ini, "UI", "fAnimationEventLogWidth", fAnimationEventLogWidth);
		ReadFloatSetting(ini, "UI", "fAnimationEventLogHeight", fAnimationEventLogHeight);
		ReadFloatSetting(ini, "UI", "fAnimationLogsOffsetX", fAnimationLogsOffsetX);
		ReadFloatSetting(ini, "UI", "fAnimationLogsOffsetY", fAnimationLogsOffsetY);

		ReadBoolSetting(ini, "UI", "bEnableModifierLog", bEnableModifierLog);
		ReadFloatSetting(ini, "UI", "fModifierLogWidth", fModifierLogWidth);
		ReadFloatSetting(ini, "UI", "fModifierLogHeight", fModifierLogHeight);
		ReadUInt32Setting(ini, "UI", "uModifierLogMaxEntries", uModifierLogMaxEntries);

		// Preview
		ReadBoolSetting(ini, "Preview", "bPreviewLockSpine", bPreviewLockSpine);
		ReadBoolSetting(ini, "Preview", "bPreviewHoldAPose", bPreviewHoldAPose);
		ReadBoolSetting(ini, "Preview", "bPreviewClearChannels", bPreviewClearChannels);

		// Debug
		ReadBoolSetting(ini, "Debug", "bEnableDebugDraws", bEnableDebugDraws);

		return true;
	};

	logger::info("Reading .ini...");
	if (readIni(iniPath)) {
		logger::info("...success");
	} else {
		logger::info("...ini not found, creating a new one");
		WriteSettings();
	}
}

void Settings::WriteSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	ini.LoadFile(iniPath.data());

	// General
	ini.SetLongValue("General", "uTargetMode", uTargetMode);
	ini.SetDoubleValue("General", "fTargetDistance", fTargetDistance);
	ini.SetDoubleValue("General", "fTickRate", fTickRate);
	ini.SetBoolValue("General", "bPauseInRaceMenu", bPauseInRaceMenu);
	ini.SetDoubleValue("General", "fDefaultTransitionTime", fDefaultTransitionTime);

	// Tracking
	ini.SetDoubleValue("Tracking", "fTrackSpine1Share", fTrackSpine1Share);
	ini.SetDoubleValue("Tracking", "fTrackSpine2Share", fTrackSpine2Share);
	ini.SetDoubleValue("Tracking", "fTrackNeckShare", fTrackNeckShare);
	ini.SetDoubleValue("Tracking", "fTrackSpine1Yaw", fTrackSpine1Yaw);
	ini.SetDoubleValue("Tracking", "fTrackSpine1Pitch", fTrackSpine1Pitch);
	ini.SetDoubleValue("Tracking", "fTrackSpine2Yaw", fTrackSpine2Yaw);
	ini.SetDoubleValue("Tracking", "fTrackSpine2Pitch", fTrackSpine2Pitch);
	ini.SetDoubleValue("Tracking", "fTrackNeckYaw", fTrackNeckYaw);
	ini.SetDoubleValue("Tracking", "fTrackNeckPitch", fTrackNeckPitch);
	ini.SetDoubleValue("Tracking", "fTrackHeadYaw", fTrackHeadYaw);
	ini.SetDoubleValue("Tracking", "fTrackHeadPitch", fTrackHeadPitch);
	ini.SetDoubleValue("Tracking", "fPoseModeTransition", fPoseModeTransition);

	// Face
	ini.SetDoubleValue("Face", "fBlinkDownTime", fBlinkDownTime);
	ini.SetDoubleValue("Face", "fBlinkUpTime", fBlinkUpTime);
	ini.SetDoubleValue("Face", "fBlinkDelayMin", fBlinkDelayMin);
	ini.SetDoubleValue("Face", "fBlinkDelayMax", fBlinkDelayMax);
	ini.SetDoubleValue("Face", "fEyeMovementHeading", fEyeMovementHeading);
	ini.SetDoubleValue("Face", "fEyeMovementPitch", fEyeMovementPitch);
	ini.SetDoubleValue("Face", "fEyeMovementGapMin", fEyeMovementGapMin);
	ini.SetDoubleValue("Face", "fEyeMovementGapMax", fEyeMovementGapMax);
	ini.SetDoubleValue("Face", "fEyeMovementSpeed", fEyeMovementSpeed);

	// Patches
	ini.SetBoolValue("Patches", "bPatchUbeEyeRotationChosen", bPatchUbeEyeRotationChosen);
	ini.SetBoolValue("Patches", "bPatchUbeEyeRotation", bPatchUbeEyeRotation);
	ini.SetDoubleValue("Patches", "fPatchUbeEyeLeft", fPatchUbeEyeLeft);
	ini.SetDoubleValue("Patches", "fPatchUbeEyeRight", fPatchUbeEyeRight);
	ini.SetDoubleValue("Patches", "fPatchUbeEyeUp", fPatchUbeEyeUp);
	ini.SetDoubleValue("Patches", "fPatchUbeEyeDown", fPatchUbeEyeDown);
	ini.SetDoubleValue("Patches", "fPatchUbeEyeRotationStrength", fPatchUbeEyeRotationStrength);
	ini.SetBoolValue("Patches", "bPatchSmpBodyCollisionChosen", bPatchSmpBodyCollisionChosen);
	ini.SetBoolValue("Patches", "bPatchSmpBodyCollision", bPatchSmpBodyCollision);

	// UI
	ini.SetBoolValue("UI", "bEnableUI", bEnableUI);
	ini.SetBoolValue("UI", "bShowWelcomeBanner", bShowWelcomeBanner);
	ini.SetBoolValue("UI", "bShowTimings", bShowTimings);
	ini.SetLongValue("UI", "uToggleUIKey", uToggleUIKeyData[0]);
	ini.SetLongValue("UI", "uToggleUIKeyCtrl", uToggleUIKeyData[1]);
	ini.SetLongValue("UI", "uToggleUIKeyShift", uToggleUIKeyData[2]);
	ini.SetLongValue("UI", "uToggleUIKeyAlt", uToggleUIKeyData[3]);
	ini.SetDoubleValue("UI", "fUIScale", fUIScale);

	ini.SetBoolValue("UI", "bShowActorWindow", bShowActorWindow);
	ini.SetDoubleValue("UI", "fAnimationEventLogWidth", fAnimationEventLogWidth);
	ini.SetDoubleValue("UI", "fAnimationEventLogHeight", fAnimationEventLogHeight);
	ini.SetDoubleValue("UI", "fAnimationLogsOffsetX", fAnimationLogsOffsetX);
	ini.SetDoubleValue("UI", "fAnimationLogsOffsetY", fAnimationLogsOffsetY);

	ini.SetBoolValue("UI", "bEnableModifierLog", bEnableModifierLog);
	ini.SetDoubleValue("UI", "fModifierLogWidth", fModifierLogWidth);
	ini.SetDoubleValue("UI", "fModifierLogHeight", fModifierLogHeight);
	ini.SetLongValue("UI", "uModifierLogMaxEntries", uModifierLogMaxEntries);

	// Preview
	ini.SetBoolValue("Preview", "bPreviewLockSpine", bPreviewLockSpine);
	ini.SetBoolValue("Preview", "bPreviewHoldAPose", bPreviewHoldAPose);
	ini.SetBoolValue("Preview", "bPreviewClearChannels", bPreviewClearChannels);

	// Debug
	ini.SetBoolValue("Debug", "bEnableDebugDraws", bEnableDebugDraws);

	ini.SaveFile(iniPath.data());
}

void Settings::ApplyLoadOrderDefaults()
{
	// UBE's eyes: on when UBE is in the load order, unless the user chose
	bUbeInstalled = false;
	if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
		bUbeInstalled = dataHandler->LookupModByName("UBE_AllRace.esp") != nullptr;
	}
	if (!bPatchUbeEyeRotationChosen) {
		bPatchUbeEyeRotation = bUbeInstalled;
	}
	logger::info("patches: UBE {}, its eye patch {}{}", bUbeInstalled ? "found" : "not found", bPatchUbeEyeRotation ? "on" : "off", bPatchUbeEyeRotationChosen ? " by the user's choice" : "");

	// SMP's body collision: on when FSMP is loaded, unless the user chose
	bSmpInstalled = GetModuleHandleW(L"hdtSMP64.dll") != nullptr;
	if (!bPatchSmpBodyCollisionChosen) {
		bPatchSmpBodyCollision = bSmpInstalled;
	}
	logger::info("patches: SMP {}, its body collision patch {}{}", bSmpInstalled ? "found" : "not found", bPatchSmpBodyCollision ? "on" : "off", bPatchSmpBodyCollisionChosen ? " by the user's choice" : "");
}
