cmake_minimum_required(VERSION 3.22)

project(levimusic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(LL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../LeviLaunchroid" ABSOLUTE)
set(PRELOADER_ANDROID_ROOT
    "${LL_REPO_ROOT}/app/src/main/cpp/preloader"
    CACHE PATH "Path to LiteLDev/preloader-android")

if(NOT EXISTS "${PRELOADER_ANDROID_ROOT}/include/pl/Config.hpp")
    message(FATAL_ERROR
        "PRELOADER_ANDROID_ROOT must point to a preloader-android checkout. "
        "Missing: ${PRELOADER_ANDROID_ROOT}/include/pl/Config.hpp")
endif()

include(FetchContent)

set(LEVIMUSIC_DEPS)
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3)
    list(APPEND LEVIMUSIC_DEPS nlohmann_json)
endif()

if(NOT TARGET Boost::pfr)
    FetchContent_Declare(
        boost_pfr
        GIT_REPOSITORY https://github.com/boostorg/pfr.git
        GIT_TAG boost-1.88.0)
    list(APPEND LEVIMUSIC_DEPS boost_pfr)
endif()

if(NOT TARGET magic_enum::magic_enum)
    FetchContent_Declare(
        magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
        GIT_TAG v0.9.7)
    list(APPEND LEVIMUSIC_DEPS magic_enum)
endif()

if(NOT TARGET fmt::fmt)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.2.0)
    set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
    set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules" FORCE)
    list(APPEND LEVIMUSIC_DEPS fmt)
endif()

if(LEVIMUSIC_DEPS)
    FetchContent_MakeAvailable(${LEVIMUSIC_DEPS})
endif()

function(configure_levi_music_common target_name)
    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${PRELOADER_ANDROID_ROOT}/include")
    target_link_libraries("${target_name}" PRIVATE
        nlohmann_json::nlohmann_json
        Boost::pfr
        magic_enum::magic_enum
        fmt::fmt)
    target_compile_options("${target_name}" PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic>)
endfunction()

if(NOT ANDROID)
    add_executable(levi_music_config_gen
        src/GenerateConfig.cpp)
    configure_levi_music_common(levi_music_config_gen)
    target_compile_definitions(levi_music_config_gen PRIVATE
        PL_CONFIG_NO_RUNTIME)
endif()

if(ANDROID)
    option(LEVIMUSIC_LINK_PRELOADER
           "Build and link the local preloader target for this example"
           ON)

    if(LEVIMUSIC_LINK_PRELOADER AND NOT TARGET preloader)
        add_subdirectory("${PRELOADER_ANDROID_ROOT}"
                         "${CMAKE_BINARY_DIR}/_preloader_android")
    endif()

    if(ANDROID_ABI)
        set(LEVIMUSIC_OUTPUT_ABI "${ANDROID_ABI}")
    else()
        set(LEVIMUSIC_OUTPUT_ABI "android")
    endif()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/out/${LEVIMUSIC_OUTPUT_ABI}")

    find_library(LL_LOG_LIB log)

    add_library(levi_music SHARED
        src/LeviMusic.cpp)
    set_target_properties(levi_music PROPERTIES OUTPUT_NAME "levi_music")
    configure_levi_music_common(levi_music)
    target_compile_options(levi_music PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fvisibility=hidden -fvisibility-inlines-hidden>)

    if(LL_LOG_LIB)
        target_link_libraries(levi_music PRIVATE "${LL_LOG_LIB}")
    endif()

    if(TARGET preloader)
        target_link_libraries(levi_music PRIVATE preloader)
    else()
        target_link_options(levi_music PRIVATE
            -Wl,--unresolved-symbols=ignore-all)
    endif()
endif()
