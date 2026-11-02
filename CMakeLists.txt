cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")

project(Talk2Me VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: локально можно передать -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(T2M_FORMATS VST3 AU Standalone)
else()
    set(T2M_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Talk2Me
    COMPANY_NAME "Personal"
    BUNDLE_ID "com.personal.talk2me"
    PLUGIN_MANUFACTURER_CODE Prsn
    PLUGIN_CODE T2Me
    FORMATS ${T2M_FORMATS}
    PRODUCT_NAME "Talk2Me"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

set(T2M_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/Presets.cpp
    Source/Speech/Phonemes.cpp
    Source/Speech/Lexicon.cpp
    Source/Speech/Script.cpp
    Source/DSP/Formant.cpp
    Source/DSP/LpcChip.cpp
    Source/DSP/LpcEncoder.cpp
    Source/DSP/Engine.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/Views.cpp
    Source/UI/About.cpp)

target_sources(Talk2Me PRIVATE ${T2M_SOURCES})
target_include_directories(Talk2Me PRIVATE Source)

set(T2M_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(Talk2Me PUBLIC ${T2M_DEFS})

# Шрифты Medeo (SIL OFL): Sans — интерфейс, Mono — числа, Pixel — логотип
juce_add_binary_data(T2MFonts
    NAMESPACE T2MFonts
    HEADER_NAME BinaryData.h
    SOURCES
        Resources/Fonts/MedeoSans-Regular.ttf
        Resources/Fonts/MedeoSans-SemiBold.ttf
        Resources/Fonts/MedeoMono-Regular.ttf
        Resources/Fonts/MedeoMono-SemiBold.ttf
        Resources/Fonts/MedeoPixel-Regular.ttf)
set_target_properties(T2MFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Словарь произношений CMUdict (BSD), сжат gzip
juce_add_binary_data(T2MLexicon
    NAMESPACE T2MLexicon
    HEADER_NAME LexiconData.h
    SOURCES Resources/Lexicon/cmudict.dict.gz)
set_target_properties(T2MLexicon PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(T2M_LIBS
    T2MFonts
    T2MLexicon
    juce::juce_audio_utils
    juce::juce_dsp)

target_link_libraries(Talk2Me
    PRIVATE ${T2M_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Офлайн-тест: рендер нот в WAV, проверки звука, состояния и скриншоты интерфейса
option(T2M_BUILD_TESTS "Build offline test" OFF)
if(T2M_BUILD_TESTS)
    juce_add_console_app(T2MTest PRODUCT_NAME "T2MTest")
    target_sources(T2MTest PRIVATE Tests/T2MTest.cpp ${T2M_SOURCES})
    target_include_directories(T2MTest PRIVATE Source)
    target_compile_definitions(T2MTest PRIVATE ${T2M_DEFS} T2M_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(T2MTest PRIVATE ${T2M_LIBS} juce::juce_recommended_config_flags)
endif()
