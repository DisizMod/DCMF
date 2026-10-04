#pragma once

#include "TrueHUDAPI.h"

struct Settings
{
	static void Initialize();
	static void ReadSettings();
	static void WriteSettings();
	// Once the game's data is loaded: the defaults that follow the load order, where the user has not chosen
	static void ApplyLoadOrderDefaults();

	// General
	static inline uint32_t uParsingWorkerCount = 4;
	static inline uint32_t uTargetMode = 1;  // Scheduler::TargetMode
	static inline float fTargetDistance = 0.f;
	static inline float fTickRate = 4.f;
	static inline float fDefaultTransitionTime = 0.3f;
	static inline bool bPauseInRaceMenu = true;  // every actor let go while RaceMenu is open, so what it reads and saves is the actor's own

	// Tracking: how a turn is shared down the spine, sequential gains, and how far each bone may go, degrees
	static inline float fTrackSpine1Share = 0.15f;
	static inline float fTrackSpine2Share = 0.2f;
	static inline float fTrackNeckShare = 0.2f;
	static inline float fTrackSpine1Yaw = 10.f;
	static inline float fTrackSpine1Pitch = 10.f;
	static inline float fTrackSpine2Yaw = 15.f;
	static inline float fTrackSpine2Pitch = 15.f;
	static inline float fTrackNeckYaw = 15.f;
	static inline float fTrackNeckPitch = 15.f;
	static inline float fTrackHeadYaw = 40.f;
	static inline float fTrackHeadPitch = 40.f;
	static inline float fPoseModeTransition = 0.5f;  // seconds a segment takes from one mode to the next

	// Blink, NG's timings: the lid's way down and up, and the gap between blinks, drawn short more often than long
	static inline float fBlinkDownTime = 0.04f;
	static inline float fBlinkUpTime = 0.14f;
	static inline float fBlinkDelayMin = 0.5f;
	static inline float fBlinkDelayMax = 8.f;

	// Eye movement: how far the eyes wander off their aim, as fractions of the engine's bounds, how often, and how fast they settle
	static inline float fEyeMovementHeading = 0.15f;
	static inline float fEyeMovementPitch = 0.1f;
	static inline float fEyeMovementGapMin = 0.4f;
	static inline float fEyeMovementGapMax = 3.f;
	static inline float fEyeMovementSpeed = 12.f;

	// Patches: fixes for heads the engine's own animation does not fit. UBE's eyes barely follow the engine's Look
	// morphs; driven from the eye angles through its extension's Look morphs at these weights and its rotation pair
	static inline bool bPatchUbeEyeRotation = false;
	static inline bool bPatchUbeEyeRotationChosen = false;  // set by the user, kept over what the load order says
	static inline bool bUbeInstalled = false;               // UBE_AllRace.esp in the load order
	static inline float fPatchUbeEyeLeft = 0.7f;
	static inline float fPatchUbeEyeRight = 0.7f;
	static inline float fPatchUbeEyeUp = 0.8f;
	static inline float fPatchUbeEyeDown = 2.f;
	static inline float fPatchUbeEyeRotationStrength = 0.05f;
	// SMP collides with the body as the modifiers shape it, once none is mid-transition; clips and the Actor window left out
	static inline bool bPatchSmpBodyCollision = false;
	static inline bool bPatchSmpBodyCollisionChosen = false;  // set by the user, kept over what the load order says
	static inline bool bSmpInstalled = false;                 // hdtSMP64.dll loaded

	// UI
	static inline bool bEnableUI = true;
	static inline bool bShowWelcomeBanner = true;
	static inline bool bShowTimings = false;
	static inline uint32_t uToggleUIKeyData[4];
	static inline float fUIScale = 1.f;

	static inline float fAnimationQueueLingerTime = 5.f;

	static inline bool bShowActorWindow = false;

	// The Modifier & Clip Log
	static inline bool bEnableModifierLog = false;
	static inline float fModifierLogWidth = 650.f;
	static inline float fModifierLogEntryFadeTime = 2.f;
	static inline float fModifierLogHeight = 600.f;
	static inline uint32_t uModifierLogMaxEntries = 100;
	static inline float fAnimationEventLogWidth = 500.f;
	static inline float fAnimationEventLogHeight = 600.f;

	static inline float fAnimationLogsOffsetX = 0.f;
	static inline float fAnimationLogsOffsetY = 30.f;

	// What a preview does to the actor besides the previewed item
	static inline bool bPreviewLockSpine = false;
	static inline bool bPreviewHoldAPose = false;
	static inline bool bPreviewClearChannels = false;

	// Debug
	static inline bool bEnableDebugDraws = false;

	// Internal
	constexpr static inline float fStateDataLifetime = 0.5f;
	constexpr static inline float fSequentialVariantLifetime = 0.5f;
	constexpr static inline float fQueueFadeTime = 1.f;
	constexpr static inline float fWelcomeBannerFadeTime = 1.f;

	constexpr static inline std::string_view iniPath = "Data/SKSE/Plugins/DCMF.ini";
	constexpr static inline std::string_view imguiIni = "Data/SKSE/Plugins/DCMF_imgui.ini";

	static inline TRUEHUD_API::IVTrueHUD4* g_trueHUD = nullptr;
};
