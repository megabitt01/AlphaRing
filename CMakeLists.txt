cmake_minimum_required(VERSION 3.27)
project(alpha_ring)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)

set(VERSION "1.3528.0.0")

set(WRAPPER_NAME "WTSAPI32")
set(WRAPPER_DLL_NAME "${WRAPPER_NAME}.dll")

set(LIBRARY_LIST
        minhook imgui lua spdlog tinyxml2
        utils game
        Version
)

include_directories(${CMAKE_SOURCE_DIR}/src)

# json
include_directories(${CMAKE_SOURCE_DIR}/lib/json/include)

# imgui
add_library(imgui SHARED IMPORTED)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/inc)
set_target_properties(imgui PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/imgui/lib/release/imgui.lib")

# lua
add_library(lua SHARED IMPORTED)
include_directories(${CMAKE_SOURCE_DIR}/lib/lua/inc)
set_target_properties(lua PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/lua/lib/release/lua.lib")

# spdlog
add_library(spdlog SHARED IMPORTED)
include_directories(${CMAKE_SOURCE_DIR}/lib/spdlog/inc)
set_target_properties(spdlog PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/spdlog/lib/release/spdlog.lib")

# tinyxml2
add_library(tinyxml2 SHARED IMPORTED)
include_directories(${CMAKE_SOURCE_DIR}/lib/tinyxml2/inc)
set_target_properties(tinyxml2 PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/tinyxml2/lib/release/tinyxml2.lib")

# minhook
add_library(minhook SHARED IMPORTED)
include_directories(${CMAKE_SOURCE_DIR}/lib/minhook/inc)
# set_target_properties(minhook PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/minhook/lib/${CMAKE_BUILD_TYPE}/libMinHook.x64.lib")
set_target_properties(minhook PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/minhook/lib/release/libMinHook.x64.lib")

# utils
file(GLOB_RECURSE UTILS_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/utils/src/*.cpp)
include_directories(${CMAKE_SOURCE_DIR}/lib/utils/inc)
add_library(utils ${UTILS_SRC} "lib/game/inc/${VERSION}/offset_groundhog.h" "lib/game/inc/${VERSION}/offset_halo1.h" "lib/game/inc/${VERSION}/offset_halo2.h" "lib/game/inc/${VERSION}/offset_halo3.h" "lib/game/inc/${VERSION}/offset_halo3odst.h" "lib/game/inc/${VERSION}/offset_halo4.h" "lib/game/inc/${VERSION}/offset_haloreach.h" "lib/game/inc/${VERSION}/offset_mcc.h")

# game
file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/game/src/*.cpp)
include_directories(${CMAKE_SOURCE_DIR}/lib/game/inc)
include_directories(${CMAKE_SOURCE_DIR}/lib/game/inc/${VERSION})
add_library(game ${GAME_SRC} "lib/game/inc/${VERSION}/offset_groundhog.h" "lib/game/inc/${VERSION}/offset_halo1.h" "lib/game/inc/${VERSION}/offset_halo2.h" "lib/game/inc/${VERSION}/offset_halo3.h" "lib/game/inc/${VERSION}/offset_halo3odst.h" "lib/game/inc/${VERSION}/offset_halo4.h" "lib/game/inc/${VERSION}/offset_haloreach.h" "lib/game/inc/${VERSION}/offset_mcc.h")

# disable warning c4996
add_definitions(-D_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS)
add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)
# MSVC 17.10+ constexpr-constructs std::mutex, which crashes on older msvcp140 runtimes that some
# Proton prefixes still ship (MCC "Fatal error" at launch).
add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)

add_compile_definitions(WRAPPER_DLL_NAME="${WRAPPER_DLL_NAME}")
add_compile_definitions(GAME_VERSION="${VERSION}")

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.c
)

add_library(${WRAPPER_NAME} SHARED ${CORE_SRC} "lib/game/inc/${VERSION}/offset_groundhog.h" "lib/game/inc/${VERSION}/offset_halo1.h" "lib/game/inc/${VERSION}/offset_halo2.h" "lib/game/inc/${VERSION}/offset_halo3.h" "lib/game/inc/${VERSION}/offset_halo3odst.h" "lib/game/inc/${VERSION}/offset_halo4.h" "lib/game/inc/${VERSION}/offset_haloreach.h" "lib/game/inc/${VERSION}/offset_mcc.h")

target_link_libraries(${WRAPPER_NAME} PUBLIC ${LIBRARY_LIST}
    SDL2::SDL2-static
    SDL2_mixer::SDL2_mixer-static
)

if (MSVC)
    target_link_options(${WRAPPER_NAME} PUBLIC /DEBUG)
else ()
    target_link_options(${WRAPPER_NAME} PUBLIC -g)
endif()

install(TARGETS ${WRAPPER_NAME} DESTINATION "${MCC_DIR}/mcc/binaries/win64")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/" DESTINATION "${MCC_DIR}/alpha_ring")