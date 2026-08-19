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
    Source/Percussion/GrooveEngine.cpp
    Source/Percussion/StyleDetector.cpp
    Source/Audio/VirtualPercussionEngine.cpp
    Source/AI/LogSpectFeatures.cpp
    Source/AI/OnnxSession.cpp
    Source/AI/OnnxBeatModel.cpp
    Source/AI/StubBeatModel.cpp
    Source/AI/BeatDecoder.cpp
    Source/AI/TempoEstimator.cpp
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
    TARGETED_DEVICE_FAMILY "2"
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
    )
endif()

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
