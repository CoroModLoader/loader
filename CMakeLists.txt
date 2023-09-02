cmake_minimum_required(VERSION 3.16)
project(loader LANGUAGES CXX VERSION 2.4)

# --------------------------------------------------------------------------------------------------------
# Setup Cross-Compilation on Linux
# --------------------------------------------------------------------------------------------------------

if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    message(FATAL_ERROR "[${PROJECT_NAME}] Make sure to instruct CMake to use the toolchain file!")
endif()

# --------------------------------------------------------------------------------------------------------
# Create library
# --------------------------------------------------------------------------------------------------------

file(GLOB src "src/*.cpp")
file(GLOB exports "export/*")

add_library(${PROJECT_NAME} SHARED ${src} ${exports})

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "iphlpapi")

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS OFF CXX_STANDARD_REQUIRED ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-missing-field-initializers -Wno-cast-function-type)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror -pedantic -pedantic-errors -Wfatal-errors)
endif()

if (MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-Wl,--enable-stdcall-fixup" -static -static-libgcc -static-libstdc++)
endif()

# --------------------------------------------------------------------------------------------------------
# Generate "constants" file
# --------------------------------------------------------------------------------------------------------

configure_file("constants.hpp.in" "${CMAKE_CURRENT_SOURCE_DIR}/private/constants.hpp")

# --------------------------------------------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------------------------------------------

target_include_directories(${PROJECT_NAME} PUBLIC  "include")
target_include_directories(${PROJECT_NAME} PRIVATE "private")

# --------------------------------------------------------------------------------------------------------
# Link Dependencies and Add Sources
# --------------------------------------------------------------------------------------------------------

include("cmake/cpm.cmake")

CPMFindPackage(
  NAME           sol
  VERSION        3.3.0
  GIT_REPOSITORY "https://github.com/ThePhD/sol2"
)

CPMFindPackage(
  NAME           lime
  VERSION        2.2
  GIT_REPOSITORY "https://github.com/Curve/lime"
)

CPMFindPackage(
  NAME           spdlog
  VERSION        1.12.0
  GIT_REPOSITORY "https://github.com/gabime/spdlog"
)

CPMFindPackage(
  NAME           nlohmann_json
  VERSION        3.11.2
  GIT_REPOSITORY "https://github.com/nlohmann/json"
)

CPMFindPackage(
  NAME           semver
  VERSION        0.3.0
  GIT_REPOSITORY "https://github.com/Neargye/semver"
)

CPMFindPackage(
  NAME           fmt
  GIT_TAG        10.1.0
  GIT_REPOSITORY "https://github.com/fmtlib/fmt"
)

find_package(Lua 5.1 EXACT REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog sol2 lime nlohmann_json fmt semver imagehlp ${LUA_LIBRARIES})