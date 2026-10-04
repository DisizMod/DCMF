# The plugin: its own SKSE entry point and UI hooks.

# We link everything statically, so there are no runtime DLLs to copy beside the
# plugin, and vcpkg's applocal step would fail the post-build deploy.
set(VCPKG_APPLOCAL_DEPS OFF)

add_commonlibsse_plugin(DCMF
    SOURCES
    main.cpp
    UIHooks.cpp
    BaseConditions.cpp
    BaseFunctions.cpp
    SharedTypes.cpp
    Settings.cpp
    Utils.cpp
    Variants.cpp
    RegisteredMods.cpp
    Actors.cpp
    AnimationGraph.cpp
    ActorState.cpp
    BodyTypes.cpp
    OverlaySlots.cpp
    SkinSets.cpp
    Resources.cpp
    BuiltinResources.cpp
    Query.cpp
    apply/Entries.cpp
    apply/Applier.cpp
    apply/BodyMorph.cpp
    apply/FaceMorph.cpp
    apply/Bone.cpp
    apply/Mfg.cpp
    apply/ActorProperty.cpp
    apply/HeadPart.cpp
    apply/Material.cpp
    apply/Overlay.cpp
    apply/DynamicData.cpp
    apply/Skin.cpp
    apply/HeadTrack.cpp
    BaseChannels.cpp
    BaseTriggers.cpp
    Hook.cpp
    Program.cpp
    Scheduler.cpp
    Resolve.cpp
    ClipPlayer.cpp
    Preview.cpp
    Pose.cpp
    mfgfix/MfgFix.cpp
    Timing.cpp
    Triggers.cpp
    Channels.cpp
    Conditions.cpp
    Functions.cpp
    DetectedProblems.cpp
    Jobs.cpp
    ModRegistry.cpp
    ModifierLog.cpp
    UI/UIWindow.cpp
    UI/UICommon.cpp
    UI/UIManager.cpp
    UI/UIMain.cpp
    UI/UIActor.cpp
    UI/UIModifierLog.cpp
    UI/UITimeline.cpp
    UI/UIErrorBanner.cpp
    UI/UIWelcomeBanner.cpp
    UI/UIScanProgress.cpp
    UI/MeshThumbs.cpp
    scan/Scan.cpp
    scan/HeadPartSources.cpp
    scan/FaceMorphSources.cpp
    scan/BodyMorphSources.cpp
    scan/CategorySources.cpp
    scan/PresetSources.cpp
    scan/SkinModelSources.cpp
    scan/Nif.cpp
    scan/Tri.cpp
    scan/Xml.cpp
    scan/Pex.cpp
    NAME "DCMF"
    AUTHOR "DisizMod"
    VERSION ${PROJECT_VERSION}
    USE_ADDRESS_LIBRARY
)

target_include_directories(DCMF PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_precompile_headers(DCMF PRIVATE PCH.h)

# The real version, from the one place it is declared -- project(DCMF VERSION)
# in the root CMakeLists. The plugin target takes DCMF_VERSION the same way;
# the three numbers are here as well so Plugin::VERSION can be constexpr.
target_compile_definitions(DCMF PRIVATE
    DCMF_VERSION="${PROJECT_VERSION}"
    DCMF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    DCMF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    DCMF_VERSION_PATCH=${PROJECT_VERSION_PATCH})

find_package(imgui CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(Boost REQUIRED)

target_link_libraries(DCMF PRIVATE
    imgui::imgui
    minhook::minhook
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib)

target_include_directories(DCMF SYSTEM PRIVATE
    ${RAPIDJSON_INCLUDE_DIRS})

# The copied sources are somebody else's and are not held to this project's
# warning level; /W4 /WX would fail on them for style we must not change.
if(MSVC)
    target_compile_options(DCMF PRIVATE /W0 /wd4996)
endif()

# Emit a PDB for Release too, so crash logs can be symbolised.
target_compile_options(DCMF PRIVATE "$<$<CONFIG:Release>:/Zi>")
target_link_options(DCMF PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")

# Deploy as an ordinary MO2 mod so it can be toggled in the load order.
set(DCMF_DEPLOY_DIR
    "$ENV{LOCALAPPDATA}/ModOrganizer/Skyrim Special Edition/mods/DCMF"
    CACHE PATH "Mod folder the built DLL is copied into")

if(DCMF_DEPLOY_DIR)
    add_custom_command(TARGET DCMF POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DCMF_DEPLOY_DIR}/SKSE/Plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:DCMF>" "${DCMF_DEPLOY_DIR}/SKSE/Plugins/"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_PDB_FILE:DCMF>" "${DCMF_DEPLOY_DIR}/SKSE/Plugins/"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/../dist/DCMF.bodytypes.json" "${DCMF_DEPLOY_DIR}/SKSE/Plugins/"
        COMMENT "Deploying DCMF.dll to ${DCMF_DEPLOY_DIR}")
endif()
