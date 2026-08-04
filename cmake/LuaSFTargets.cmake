include_guard(GLOBAL)

get_filename_component(LUASF_RESULT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(LUASF_INCLUDE_DIR "${LUASF_RESULT_ROOT}/include" CACHE PATH "LuaSF include directory" FORCE)
set(LUASF_RUNTIME_DIR "${LUASF_RESULT_ROOT}/bin" CACHE PATH "LuaSF runtime library directory" FORCE)
set(LUASF_LIBRARY_DIR "${LUASF_RESULT_ROOT}/lib" CACHE PATH "LuaSF import/static library directory" FORCE)
set(LUASF_STUB_FILE "${LUASF_RESULT_ROOT}/stub/LuaSF.d.lua" CACHE FILEPATH "LuaSF Lua language-server stub" FORCE)

if(WIN32)
    set(LUASF_MODULE_FILE "${LUASF_RUNTIME_DIR}/LuaSF.dll")
    set(LUASF_LUA_FILE "${LUASF_RUNTIME_DIR}/lua.dll")
    set(LUASF_MODULE_IMPLIB "${LUASF_LIBRARY_DIR}/LuaSF.lib")
    set(LUASF_LUA_IMPLIB "${LUASF_LIBRARY_DIR}/lua.lib")
elseif(APPLE)
    set(LUASF_MODULE_FILE "${LUASF_RUNTIME_DIR}/LuaSF.dylib")
    set(LUASF_LUA_FILE "${LUASF_RUNTIME_DIR}/liblua.dylib")
else()
    set(LUASF_MODULE_FILE "${LUASF_RUNTIME_DIR}/LuaSF.so")
    set(LUASF_LUA_FILE "${LUASF_RUNTIME_DIR}/liblua.so")
endif()

if(NOT EXISTS "${LUASF_MODULE_FILE}")
    message(FATAL_ERROR "LuaSF runtime library was not found: ${LUASF_MODULE_FILE}")
endif()

if(NOT EXISTS "${LUASF_LUA_FILE}")
    message(FATAL_ERROR "Lua runtime library was not found: ${LUASF_LUA_FILE}")
endif()

if(WIN32)
    if(NOT EXISTS "${LUASF_MODULE_IMPLIB}")
        message(FATAL_ERROR "LuaSF import library was not found: ${LUASF_MODULE_IMPLIB}")
    endif()

    if(NOT EXISTS "${LUASF_LUA_IMPLIB}")
        message(FATAL_ERROR "Lua import library was not found: ${LUASF_LUA_IMPLIB}")
    endif()
endif()

add_library(LuaSF::Lua SHARED IMPORTED GLOBAL)
set_target_properties(LuaSF::Lua PROPERTIES
    IMPORTED_LOCATION "${LUASF_LUA_FILE}"
    INTERFACE_INCLUDE_DIRECTORIES "${LUASF_INCLUDE_DIR}"
)
if(WIN32)
    set_target_properties(LuaSF::Lua PROPERTIES
        IMPORTED_IMPLIB "${LUASF_LUA_IMPLIB}"
    )
endif()

add_library(LuaSF::LuaSF SHARED IMPORTED GLOBAL)
set_target_properties(LuaSF::LuaSF PROPERTIES
    IMPORTED_LOCATION "${LUASF_MODULE_FILE}"
    INTERFACE_INCLUDE_DIRECTORIES "${LUASF_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES LuaSF::Lua
    INTERFACE_COMPILE_FEATURES cxx_std_20
)
if(WIN32)
    set_target_properties(LuaSF::LuaSF PROPERTIES
        IMPORTED_IMPLIB "${LUASF_MODULE_IMPLIB}"
    )
endif()

if(WIN32)
    file(GLOB _LUASF_RUNTIME_FILES "${LUASF_RUNTIME_DIR}/*.dll")
elseif(APPLE)
    file(GLOB _LUASF_RUNTIME_FILES "${LUASF_RUNTIME_DIR}/*.dylib")
else()
    file(GLOB _LUASF_RUNTIME_FILES "${LUASF_RUNTIME_DIR}/*.so" "${LUASF_RUNTIME_DIR}/*.so.*")
endif()
set(LUASF_RUNTIME_FILES "${_LUASF_RUNTIME_FILES}" CACHE STRING "LuaSF bundled runtime files" FORCE)
set(LUASF_RUNTIME_DLLS "${LUASF_RUNTIME_FILES}" CACHE STRING "LuaSF runtime files kept for compatibility" FORCE)

function(luasf_copy_runtime_files target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "luasf_copy_runtime_files target does not exist: ${target_name}")
    endif()

    add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${LUASF_RUNTIME_FILES}
            "$<TARGET_FILE_DIR:${target_name}>"
        VERBATIM
    )
endfunction()

function(luasf_copy_runtime_dlls target_name)
    luasf_copy_runtime_files("${target_name}")
endfunction()
