# Lua 5.5 uses numeric *_N version macros that older CMake finders cannot read.
# Keep discovery local to LuaGlue; existing host targets bypass this module.
find_path(LUA_INCLUDE_DIR NAMES lua.h
    HINTS ${Lua_ROOT} ${LUA_ROOT} ENV Lua_ROOT ENV LUA_ROOT ENV LUA_DIR
    PATH_SUFFIXES include/lua5.5 include/lua55 include/lua-5.5 include/lua
                  include src lua5.5 lua55 lua-5.5 lua)
find_library(LUA_LIBRARY NAMES lua5.5 lua55 lua-5.5 lua
    HINTS ${Lua_ROOT} ${LUA_ROOT} ENV Lua_ROOT ENV LUA_ROOT ENV LUA_DIR
    PATH_SUFFIXES lib lib64)

unset(LUA_VERSION_STRING)
set(_lua_headers_compatible FALSE)
if(EXISTS "${LUA_INCLUDE_DIR}/lua.h")
    file(STRINGS "${LUA_INCLUDE_DIR}/lua.h" _lua_version_lines
        REGEX "^[ \t]*#[ \t]*define[ \t]+LUA_VERSION_(MAJOR|MINOR|RELEASE)(_N)?[ \t]+")
    foreach(part IN ITEMS MAJOR MINOR RELEASE)
        unset(LUA_VERSION_${part})
        string(REGEX MATCH "LUA_VERSION_${part}_N[ \t]+([0-9]+)" match "${_lua_version_lines}")
        if(match)
            set(LUA_VERSION_${part} "${CMAKE_MATCH_1}")
        else()
            string(REGEX MATCH "LUA_VERSION_${part}[ \t]+\"([0-9]+)\"" match "${_lua_version_lines}")
            if(match)
                set(LUA_VERSION_${part} "${CMAKE_MATCH_1}")
            endif()
        endif()
    endforeach()
    if(DEFINED LUA_VERSION_MAJOR AND DEFINED LUA_VERSION_MINOR AND DEFINED LUA_VERSION_RELEASE)
        set(LUA_VERSION_STRING "${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}.${LUA_VERSION_RELEASE}")
        if(LUA_VERSION_MAJOR STREQUAL "5" AND LUA_VERSION_MINOR STREQUAL "5")
            set(_lua_headers_compatible TRUE)
        endif()
    endif()
endif()
set(_lua_library_exists FALSE)
if(EXISTS "${LUA_LIBRARY}" AND NOT IS_DIRECTORY "${LUA_LIBRARY}")
    set(_lua_library_exists TRUE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Lua
    REQUIRED_VARS LUA_INCLUDE_DIR LUA_LIBRARY _lua_headers_compatible _lua_library_exists
    VERSION_VAR LUA_VERSION_STRING
    REASON_FAILURE_MESSAGE "LuaGlue requires Lua 5.5 headers and a matching library. Set LUA_INCLUDE_DIR and LUA_LIBRARY, or add the Lua installation to CMAKE_PREFIX_PATH.")

if(Lua_FOUND)
    set(LUA_INCLUDE_DIRS "${LUA_INCLUDE_DIR}")
    set(LUA_LIBRARIES "${LUA_LIBRARY}")
    if(UNIX AND NOT APPLE)
        list(APPEND LUA_LIBRARIES m ${CMAKE_DL_LIBS})
    endif()
endif()
mark_as_advanced(LUA_INCLUDE_DIR LUA_LIBRARY)
