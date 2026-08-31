cmake_minimum_required (VERSION 3.22)

# Objective-C/C++ exists only on Apple platforms. Declaring it unconditionally
# fails configure on a Linux host, where the console test target is the only
# thing that can be built anyway.
if (APPLE)
    project (VirtualPercussionist VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project (VirtualPercussionist VERSION 0.1.0 LANGUAGES C CXX)
endif()

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "Minimum OS version")

set (JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set (JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory (third_party/JUCE)

set (VP_CORE_SOURCES
    Source/Tracking/TempoFollower.cpp
    Source/Tracking/BeatTracker.cpp
    Source/Percussion/PercussionEngine.cpp
    Source/Percussion/GrooveEngine.cpp
    Source/Percussion/StyleDetector.cpp
    Source/Audio/VirtualPercussionEngine.cpp
    Source/Audio/LatencyProbe.cpp
    Source/AI/LogSpectFeatures.cpp
    Source/AI/OnnxSession.cpp
    Source/AI/OnnxBeatModel.cpp
    Source/AI/StubBeatModel.cpp
    Source/AI/BeatDecoder.cpp
    Source/AI/TempoEstimator.cpp
    Source/AI/BeatHmm.cpp
    Source/AI/NeuralBeatTracker.cpp
    Source/AI/ModelLocator.cpp
    Source/Stretch/StretchFactor.cpp
    Source/Stretch/TimeStretchEngine.cpp
    Source/Platform/NativeAudioBridge.cpp
)

option (VP_USE_ONNX "Link ONNX Runtime Mobile for neural beat tracking" OFF)
option (VP_USE_SIGNALSMITH "Use vendored Signalsmith Stretch" OFF)
set (ONNXRUNTIME_ROOT "" CACHE PATH "Root of ONNX Runtime host SDK (include/ + lib/)")
set (ONNXRUNTIME_IOS_XCF "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-ios/onnxruntime.xcframework"
     CACHE PATH "ONNX Runtime iOS xcframework")

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if (EXISTS "${ONNXRUNTIME_IOS_XCF}/ios-arm64/onnxruntime.framework/Headers/onnxruntime_c_api.h")
        set (VP_USE_ONNX ON CACHE BOOL "Link ONNX Runtime Mobile for neural beat tracking" FORCE)
        set (VP_ORT_IOS TRUE)
    else()
        set (VP_ORT_IOS FALSE)
        message (WARNING "iOS ONNX Runtime missing. Run ./scripts/fetch_onnxruntime.sh --ios")
    endif()
else()
    set (VP_ORT_IOS FALSE)
    if (NOT ONNXRUNTIME_ROOT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include/onnxruntime_c_api.h")
        set (ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
    endif()
    if (ONNXRUNTIME_ROOT AND EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_c_api.h")
        set (VP_USE_ONNX ON CACHE BOOL "Link ONNX Runtime Mobile for neural beat tracking" FORCE)
    endif()
endif()

set (VP_BEAT_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Models/beatnet.onnx")
set (VP_HAS_BEAT_MODEL_FILE FALSE)
if (EXISTS "${VP_BEAT_MODEL}")
    set (VP_HAS_BEAT_MODEL_FILE TRUE)
endif()


juce_add_gui_app (VirtualPercussionist
    PRODUCT_NAME "Virtual Percussionist"
    COMPANY_NAME "Virtual Percussionist"
    BUNDLE_ID com.virtualpercussionist.app
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Virtual Percussionist listens to your drum kit to play along in time."
    BACKGROUND_AUDIO_ENABLED TRUE
    # iPhone and iPad. 1 is iPhone, 2 is iPad. This is the one that actually
    # decides anything here: it becomes UIDeviceFamily in the Info.plist, which
    # is what makes an iPhone a device this app will install on.
    #
    # A Mac gets the app either as the native build (no CMAKE_SYSTEM_NAME=iOS)
    # or, on Apple silicon, as the iPad build under "Designed for iPad" - and
    # that one is not a build setting at all. See below.
    TARGETED_DEVICE_FAMILY "1,2"
    STATUS_BAR_HIDDEN FALSE
    IPAD_SCREEN_ORIENTATIONS "UIInterfaceOrientationPortrait;UIInterfaceOrientationPortraitUpsideDown;UIInterfaceOrientationLandscapeLeft;UIInterfaceOrientationLandscapeRight"
    IPHONE_SCREEN_ORIENTATIONS "UIInterfaceOrientationPortrait;UIInterfaceOrientationLandscapeLeft;UIInterfaceOrientationLandscapeRight"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set (VP_DEVELOPMENT_TEAM "28H5MJ7244" CACHE STRING "Apple Development Team ID")
    set_target_properties (VirtualPercussionist PROPERTIES
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${VP_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
        # The same device family as above, set straight onto the Xcode target.
        #
        # juce_add_gui_app already forwards it, but through a generator
        # expression reading a JUCE property, and that is one indirection more
        # than is worth trusting for the setting that decides whether the app
        # installs on a phone at all. Written twice, it cannot be read wrong.
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        # Which destinations Xcode offers *locally*. With the visionOS SDK
        # installed, Xcode adds "Apple Vision (Designed for iPad)" by itself to
        # any target built against the iOS SDK; naming the platforms keeps the
        # list to the two this app is for. Delete this line if the Mac
        # (Designed for iPad) destination ever goes missing with it - that
        # destination builds the iphoneos slice and should survive, but it is
        # the one thing here worth checking on a real Xcode.
        XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
    )
endif()

# And the part no build setting decides.
#
# Whether an iPhone/iPad app is offered on Apple Vision Pro, and whether it is
# offered on Macs with Apple silicon, are *App Store Connect* settings, not
# Xcode ones. Apple's own help pages are explicit about it: the app is available
# on both by default and the developer has to opt out, in Pricing and
# Availability. There is no build setting that turns Vision off, and any that
# looks like it does is being ignored.
#
#   App Store Connect -> the app -> Pricing and Availability
#     "iPhone and iPad Apps on Apple Vision Pro"   -> deselect
#     "iPhone and iPad Apps on Apple Silicon Mac"  -> leave selected
#
# So: iPhone and iPad come from TARGETED_DEVICE_FAMILY above and are settled
# here. Mac stays on and Vision goes off in App Store Connect, and nowhere else.
# See docs/PLATFORM.md.

juce_generate_juce_header (VirtualPercussionist)

file (GLOB VP_PERC_SAMPLES "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Percussion/*.wav")
if (VP_PERC_SAMPLES)
    juce_add_binary_data (VpPercussionSamples
        HEADER_NAME VpPercussionData.h
        NAMESPACE VpPercussionData
        SOURCES ${VP_PERC_SAMPLES}
    )
    set (VP_HAS_PERC_SAMPLES TRUE)
else()
    set (VP_HAS_PERC_SAMPLES FALSE)
    message (STATUS "No Assets/Percussion/*.wav - percussion falls back to synthesis")
endif()

if (VP_HAS_BEAT_MODEL_FILE)
    juce_add_binary_data (VpBeatModel
        HEADER_NAME VpBeatModelData.h
        NAMESPACE VpBeatModelData
        SOURCES "${VP_BEAT_MODEL}"
    )
endif()

target_sources (VirtualPercussionist PRIVATE
    ${VP_CORE_SOURCES}
    Source/App/Main.cpp
    Source/UI/MainComponent.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources (VirtualPercussionist PRIVATE Source/Platform/IosMicPermission.mm)
else()
    target_sources (VirtualPercussionist PRIVATE Source/Platform/IosMicPermission.cpp)
endif()

target_include_directories (VirtualPercussionist PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_definitions (VirtualPercussionist PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VirtualPercussionist,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:VirtualPercussionist,JUCE_VERSION>"
    # The rates the app offers, declared rather than discovered. Left undefined,
    # juce_Audio_ios.cpp *finds* them: on every device open it calls
    # setPreferredSampleRate at 4 kHz, then 192 kHz, then every kilohertz in
    # between - around 190 of them, back to back. Each one re-clocks whatever is
    # on the USB port, so on a mixer that sweep is audible as a crack, and it
    # pulls the hardware rate out from under anything else playing through the
    # same route, which is why the track being played along to stops.
    # A rate the interface does not have is not a problem: iOS answers with the
    # one it does have, the device reports it, and the engine prepares at it.
    "JUCE_IOS_AUDIO_EXPLICIT_SAMPLERATES=44100.0,48000.0,88200.0,96000.0"
)

target_link_libraries (VirtualPercussionist PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    juce_add_console_app (VPTests
        PRODUCT_NAME "VPTests"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )

    target_sources (VPTests PRIVATE
        ${VP_CORE_SOURCES}
        Tests/TestMain.cpp
        Tests/TestAiBeat.cpp
    )

    target_include_directories (VPTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests
    )

    target_compile_definitions (VPTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries (VPTests PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )
endif()

# Host-only diagnostic. Not part of the test suite: it renders a minute of
# audio per case and drives the whole engine over it, which is far too slow to
# sit in a run anyone waits on. Build it explicitly when a tracking change needs
# measuring against material that behaves like a record.
if (NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    juce_add_console_app (VPProbe
        PRODUCT_NAME "VPProbe"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPProbe PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPProbe PRIVATE ${VP_CORE_SOURCES} scripts/probe_song.cpp)
    target_include_directories (VPProbe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPProbe PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPTiming
        PRODUCT_NAME "VPTiming"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPTiming PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPTiming PRIVATE ${VP_CORE_SOURCES} scripts/probe_timing.cpp)
    target_include_directories (VPTiming PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPTiming PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPTiming PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPLive
        PRODUCT_NAME "VPLive"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPLive PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPLive PRIVATE ${VP_CORE_SOURCES} scripts/probe_live.cpp)
    target_include_directories (VPLive PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPLive PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPLive PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPSing
        PRODUCT_NAME "VPSing"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPSing PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPSing PRIVATE ${VP_CORE_SOURCES} scripts/probe_sing.cpp)
    target_include_directories (VPSing PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPSing PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPSing PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPAlign
        PRODUCT_NAME "VPAlign"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPAlign PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPAlign PRIVATE ${VP_CORE_SOURCES} scripts/probe_align.cpp)
    target_include_directories (VPAlign PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPAlign PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPAlign PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    # Not a measurement: a few bars of the part as a WAV, so a musical decision
    # can be reviewed by ear. See scripts/render_groove.cpp.
    juce_add_console_app (VPRender
        PRODUCT_NAME "VPRender"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPRender PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPRender PRIVATE ${VP_CORE_SOURCES} scripts/render_groove.cpp)
    target_include_directories (VPRender PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPRender PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPBar
        PRODUCT_NAME "VPBar"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPBar PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPBar PRIVATE ${VP_CORE_SOURCES} scripts/probe_bar.cpp)
    target_include_directories (VPBar PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPBar PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPBar PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPCpu
        PRODUCT_NAME "VPCpu"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPCpu PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPCpu PRIVATE ${VP_CORE_SOURCES} scripts/probe_cpu.cpp)
    target_include_directories (VPCpu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPCpu PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPCpu PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPDecoderProbe
        PRODUCT_NAME "VPDecoderProbe"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPDecoderProbe PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPDecoderProbe PRIVATE ${VP_CORE_SOURCES} scripts/probe_decoder.cpp)
    target_include_directories (VPDecoderProbe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_definitions (VPDecoderProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPDecoderProbe PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPReplay
        PRODUCT_NAME "VPReplay"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPReplay PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPReplay PRIVATE ${VP_CORE_SOURCES} scripts/probe_replay.cpp)
    target_include_directories (VPReplay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPReplay PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPReplay PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPOps
        PRODUCT_NAME "VPOps"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPOps PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPOps PRIVATE ${VP_CORE_SOURCES} scripts/probe_ops.cpp)
    target_include_directories (VPOps PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPOps PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPOps PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPRoom
        PRODUCT_NAME "VPRoom"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPRoom PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPRoom PRIVATE ${VP_CORE_SOURCES} scripts/probe_room.cpp)
    target_include_directories (VPRoom PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPRoom PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPRoom PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )

    juce_add_console_app (VPActivations
        PRODUCT_NAME "VPActivations"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )
    set_target_properties (VPActivations PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources (VPActivations PRIVATE ${VP_CORE_SOURCES} scripts/probe_activations.cpp)
    target_include_directories (VPActivations PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
    target_compile_definitions (VPActivations PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPActivations PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        PUBLIC
        juce::juce_recommended_config_flags
    )
endif()

function (vp_apply_ai_options target)
    if (VP_USE_ONNX)
        target_compile_definitions (${target} PRIVATE VP_USE_ONNX=1)
        if (VP_ORT_IOS)
            target_compile_definitions (${target} PRIVATE VP_ORT_COREML=1)
            target_include_directories (${target} PRIVATE
                "${ONNXRUNTIME_IOS_XCF}/ios-arm64/onnxruntime.framework/Headers")
            target_link_libraries (${target} PRIVATE
                "-framework CoreML"
                "-framework Foundation"
                "-framework Accelerate")
            # Official NuGet ships a static archive inside onnxruntime.framework.
            # Pin the slice by SDK so simulator does not get the device archive.
            set_target_properties (${target} PROPERTIES
                XCODE_ATTRIBUTE_OTHER_LDFLAGS[sdk=iphoneos*]
                    "$(inherited) ${ONNXRUNTIME_IOS_XCF}/ios-arm64/onnxruntime.framework/onnxruntime"
                XCODE_ATTRIBUTE_OTHER_LDFLAGS[sdk=iphonesimulator*]
                    "$(inherited) ${ONNXRUNTIME_IOS_XCF}/ios-arm64_x86_64-simulator/onnxruntime.framework/onnxruntime")
        elseif (ONNXRUNTIME_ROOT)
            target_include_directories (${target} PRIVATE ${ONNXRUNTIME_ROOT}/include)
            target_link_directories (${target} PRIVATE ${ONNXRUNTIME_ROOT}/lib)
            target_link_libraries (${target} PRIVATE onnxruntime)
            if (APPLE)
                set_target_properties (${target} PROPERTIES
                    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
                    INSTALL_RPATH "@loader_path;${ONNXRUNTIME_ROOT}/lib")
            elseif (UNIX)
                # Same thing on a Linux host, which is where the benches run in
                # CI and in a container: without it the binary links and then
                # cannot find libonnxruntime.so at startup.
                set_target_properties (${target} PROPERTIES
                    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
                    INSTALL_RPATH "$ORIGIN;${ONNXRUNTIME_ROOT}/lib")
            endif()
        else()
            target_link_libraries (${target} PRIVATE onnxruntime)
        endif()
    else()
        target_compile_definitions (${target} PRIVATE VP_USE_ONNX=0)
    endif()
    if (VP_HAS_BEAT_MODEL_FILE)
        target_compile_definitions (${target} PRIVATE VP_HAS_BEAT_MODEL=1)
        target_link_libraries (${target} PRIVATE VpBeatModel)
    endif()
    if (VP_HAS_PERC_SAMPLES)
        target_compile_definitions (${target} PRIVATE VP_HAS_PERC_SAMPLES=1)
        target_link_libraries (${target} PRIVATE VpPercussionSamples juce::juce_audio_formats)
    endif()
    if (VP_USE_SIGNALSMITH)
        target_compile_definitions (${target} PRIVATE VP_USE_SIGNALSMITH=1)
        target_include_directories (${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch)
    else()
        target_compile_definitions (${target} PRIVATE VP_USE_SIGNALSMITH=0)
    endif()
endfunction()

vp_apply_ai_options (VirtualPercussionist)
if (TARGET VPTests)
    vp_apply_ai_options (VPTests)
endif()
if (TARGET VPProbe)
    vp_apply_ai_options (VPProbe)
endif()
if (TARGET VPActivations)
    vp_apply_ai_options (VPActivations)
endif()
if (TARGET VPRoom)
    vp_apply_ai_options (VPRoom)
endif()
if (TARGET VPOps)
    vp_apply_ai_options (VPOps)
endif()
if (TARGET VPReplay)
    vp_apply_ai_options (VPReplay)
endif()
if (TARGET VPDecoderProbe)
    vp_apply_ai_options (VPDecoderProbe)
endif()
if (TARGET VPCpu)
    vp_apply_ai_options (VPCpu)
endif()

if (TARGET VPTiming)
    vp_apply_ai_options (VPTiming)
endif()

if (TARGET VPAlign)
    vp_apply_ai_options (VPAlign)
endif()
if (TARGET VPLive)
    vp_apply_ai_options (VPLive)
endif()
if (TARGET VPSing)
    vp_apply_ai_options (VPSing)
endif()
if (TARGET VPRender)
    vp_apply_ai_options (VPRender)
endif()

if (TARGET VPBar)
    vp_apply_ai_options (VPBar)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if (VP_ORT_IOS AND VP_HAS_BEAT_MODEL_FILE)
        message (STATUS "iPad AI ready: ONNX Runtime (CoreML EP) + bundled beatnet.onnx")
    elseif (NOT VP_ORT_IOS)
        message (WARNING "iPad build has no ONNX Runtime. Run ./scripts/fetch_onnxruntime.sh --ios")
    elseif (NOT VP_HAS_BEAT_MODEL_FILE)
        message (WARNING "iPad build has no Assets/Models/beatnet.onnx — TAP only until you train/export")
    endif()
endif()

if (APPLE AND VP_USE_ONNX AND ONNXRUNTIME_ROOT AND NOT VP_ORT_IOS)
    file (GLOB VP_ORT_DYLIBS "${ONNXRUNTIME_ROOT}/lib/libonnxruntime*.dylib")
    foreach (vp_ai_target VirtualPercussionist VPTests)
        if (TARGET ${vp_ai_target} AND VP_ORT_DYLIBS)
            add_custom_command (TARGET ${vp_ai_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${VP_ORT_DYLIBS}
                    "$<TARGET_FILE_DIR:${vp_ai_target}>")
        endif()
    endforeach()
endif()

if (EXISTS "$ENV{VP_STYLE_SRC}")
    juce_add_console_app (VPStyle PRODUCT_NAME "VPStyle" NEEDS_WEB_BROWSER FALSE NEEDS_CURL FALSE)
    target_sources (VPStyle PRIVATE ${VP_CORE_SOURCES} "$ENV{VP_STYLE_SRC}")
    target_include_directories (VPStyle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source "$ENV{VP_PROBE_DIR}")
    target_compile_definitions (VPStyle PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (VPStyle PRIVATE juce::juce_dsp juce::juce_audio_basics juce::juce_audio_formats
                           juce::juce_core PUBLIC juce::juce_recommended_config_flags)
    vp_apply_ai_options (VPStyle)
endif()
