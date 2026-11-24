cmake_minimum_required(VERSION 3.22)
project(CamelSpaceRevival VERSION 2.0.0 LANGUAGES C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS target")
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE" CACHE PATH "Existing JUCE checkout")
add_subdirectory("${JUCE_PATH}" JUCE)
include(CTest)
set(RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/local/runtime")
if(NOT EXISTS "${RUNTIME}/programs.json")
    message(FATAL_ERROR "Run tools/prepare_resources.py with the original plug-in and support package first")
endif()
set(HELPER "${CMAKE_CURRENT_BINARY_DIR}/camelspace-helper")
add_custom_command(OUTPUT "${HELPER}"
    COMMAND /usr/bin/clang++ -arch x86_64 -std=c++20 -O2 -mmacosx-version-min=11.0
        -framework Cocoa "${CMAKE_CURRENT_SOURCE_DIR}/src/Helper.mm" -o "${HELPER}"
    COMMAND /usr/bin/codesign --force --sign - --timestamp=none "${HELPER}"
    DEPENDS src/Helper.mm src/LegacyABI.h src/BridgeProtocol.h VERBATIM)
add_custom_target(CamelSpaceHelper ALL DEPENDS "${HELPER}")
juce_add_plugin(CamelSpace2
    COMPANY_NAME "Atjazz Record Company" BUNDLE_ID "uk.richardpeace.camelspace2"
    PLUGIN_MANUFACTURER_CODE RpRv PLUGIN_CODE CaS2 PRODUCT_NAME "Camel Space 2"
    DESCRIPTION "Atjazz Record Company edition of Camel Space 2"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE VST3_CATEGORIES "Fx;Modulation"
    FORMATS VST3 COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(CamelSpace2)
target_sources(CamelSpace2 PRIVATE src/PluginProcessor.cpp src/BridgeClient.cpp src/SkinPanel.cpp)
target_include_directories(CamelSpace2 PRIVATE src)
target_compile_definitions(CamelSpace2 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0)
juce_add_binary_data(CamelSpaceBrandData NAMESPACE CamelSpaceBrandData
    HEADER_NAME CamelSpaceBrandData.h SOURCES assets/atjazz-record-company.png assets/panel-dark.png)
target_link_libraries(CamelSpace2 PRIVATE juce::juce_audio_utils CamelSpaceBrandData
    PUBLIC juce::juce_recommended_config_flags)
add_dependencies(CamelSpace2_VST3 CamelSpaceHelper)
add_custom_command(TARGET CamelSpace2_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${RUNTIME}" "$<TARGET_BUNDLE_DIR:CamelSpace2_VST3>/Contents/Resources"
    COMMAND "${CMAKE_COMMAND}" -E copy "${HELPER}" "$<TARGET_BUNDLE_DIR:CamelSpace2_VST3>/Contents/Resources/camelspace-helper"
    COMMAND /usr/bin/codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:CamelSpace2_VST3>"
    VERBATIM)
add_executable(CamelSpaceBridgeTests tests/BridgeTests.cpp src/BridgeClient.cpp)
target_include_directories(CamelSpaceBridgeTests PRIVATE src)
add_dependencies(CamelSpaceBridgeTests CamelSpaceHelper)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/evidence")
add_test(NAME OriginalEngineBridge COMMAND CamelSpaceBridgeTests "${HELPER}"
    "${RUNTIME}/Original/CamelSpace.vst" "${RUNTIME}/CamelSpaceData"
    "${CMAKE_CURRENT_BINARY_DIR}/evidence/parameter-metadata.txt")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME SupportRecovery COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_recover_support.py)
juce_add_console_app(CamelSpaceVST3Tests PRODUCT_NAME "CamelSpace VST3 acceptance")
juce_generate_juce_header(CamelSpaceVST3Tests)
target_sources(CamelSpaceVST3Tests PRIVATE tests/VST3Tests.cpp)
target_compile_definitions(CamelSpaceVST3Tests PRIVATE JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CamelSpaceVST3Tests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
add_test(NAME NativeVST3 COMMAND CamelSpaceVST3Tests "$<TARGET_BUNDLE_DIR:CamelSpace2_VST3>"
    "${CMAKE_CURRENT_BINARY_DIR}/evidence")
add_executable(CamelSpaceProcessorTests tests/ProcessorTests.cpp)
target_include_directories(CamelSpaceProcessorTests PRIVATE src "${JUCE_PATH}/modules"
    "${CMAKE_CURRENT_BINARY_DIR}/CamelSpace2_artefacts/JuceLibraryCode")
target_link_libraries(CamelSpaceProcessorTests PRIVATE CamelSpace2)
add_test(NAME ProcessorRecallAndFiles COMMAND CamelSpaceProcessorTests
    "$<TARGET_BUNDLE_DIR:CamelSpace2_VST3>/Contents/Resources" "${CMAKE_CURRENT_BINARY_DIR}/evidence")
add_executable(CamelSpaceSkinTests tests/SkinPanelTests.cpp)
target_include_directories(CamelSpaceSkinTests PRIVATE src "${JUCE_PATH}/modules"
    "${CMAKE_CURRENT_BINARY_DIR}/CamelSpace2_artefacts/JuceLibraryCode")
target_link_libraries(CamelSpaceSkinTests PRIVATE CamelSpace2)
add_test(NAME SkinControls COMMAND CamelSpaceSkinTests "${RUNTIME}/CamelSpaceData"
    "${CMAKE_CURRENT_BINARY_DIR}/evidence/parameter-metadata.txt" "${CMAKE_CURRENT_BINARY_DIR}/evidence/skin")
set_tests_properties(SkinControls PROPERTIES DEPENDS OriginalEngineBridge)
set(EQUIVALENCE "${CMAKE_CURRENT_BINARY_DIR}/audio-equivalence")
add_custom_command(OUTPUT "${EQUIVALENCE}"
    COMMAND /usr/bin/clang++ -arch x86_64 -std=c++20 -O2 -framework Cocoa
        -I "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/AudioEquivalenceTests.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/BridgeClient.cpp" -o "${EQUIVALENCE}"
    DEPENDS tests/AudioEquivalenceTests.mm src/BridgeClient.cpp src/BridgeClient.h
        src/BridgeProtocol.h src/LegacyABI.h VERBATIM)
add_custom_target(CamelSpaceAudioReference ALL DEPENDS "${EQUIVALENCE}")
add_test(NAME OriginalAudioEquivalence COMMAND "${EQUIVALENCE}" "${HELPER}"
    "${RUNTIME}/Original/CamelSpace.vst" "${RUNTIME}/CamelSpaceData"
    "${CMAKE_CURRENT_BINARY_DIR}/evidence/audio-equivalence.csv")
add_executable(CamelSpaceProtocolTests tests/ProtocolTests.cpp)
target_include_directories(CamelSpaceProtocolTests PRIVATE src)
add_test(NAME LegacyParserBounds COMMAND CamelSpaceProtocolTests)
