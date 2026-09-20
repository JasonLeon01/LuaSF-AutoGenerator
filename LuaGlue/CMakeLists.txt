cmake_minimum_required(VERSION 3.21)
project(LuaGlue VERSION 1.0.0 LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/LuaGlueLua.cmake")

set(_luaglue_shared ON)
if(CMAKE_SYSTEM_NAME MATCHES "^(iOS|Android|OHOS)$")
    set(_luaglue_shared OFF)
endif()
option(LUAGLUE_BUILD_SHARED "Build one shared LuaGlue runtime" ${_luaglue_shared})
if(LUAGLUE_BUILD_SHARED)
    set(_luaglue_library_type SHARED)
else()
    set(_luaglue_library_type STATIC)
endif()

add_library(LuaGlue ${_luaglue_library_type}
    src/Value.cpp
    src/Lifecycle.cpp
    src/Native.cpp
    src/Binding.cpp)
add_library(LuaGlue::LuaGlue ALIAS LuaGlue)
target_compile_features(LuaGlue PUBLIC cxx_std_20)
target_compile_definitions(LuaGlue PRIVATE LUAGLUE_BUILD)
if(NOT LUAGLUE_BUILD_SHARED)
    target_compile_definitions(LuaGlue PUBLIC LUAGLUE_STATIC)
endif()
target_include_directories(LuaGlue PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_link_libraries(LuaGlue PUBLIC "${LUAGLUE_LUA_TARGET}")
set_target_properties(LuaGlue PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
    target_compile_options(LuaGlue PRIVATE /GR- /EHsc)
else()
    target_compile_options(LuaGlue PRIVATE -fno-rtti)
endif()
