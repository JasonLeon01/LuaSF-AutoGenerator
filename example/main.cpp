#include <cstdio>

#include <LuaSF.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#ifndef SCRIPTS_DIR
#define SCRIPTS_DIR "."
#endif

int main()
{
    lua_State* L = luaL_newstate();
    if (L == nullptr)
    {
        std::fprintf(stderr, "Lua error: failed to create the Lua state\n");
        return 1;
    }

    luaL_openlibs(L);

    int exitCode = 0;
    if (LuaSF_initialize_state(L) != 0 || LuaSF_register(L) != 0)
    {
        std::fprintf(stderr, "Lua error: failed to register the LuaSF bindings\n");
        exitCode = 1;
    }
    else if (LuaSF_enter_state(L) == 0)
    {
        std::fprintf(stderr, "Lua error: failed to enter Lua state\n");
        exitCode = 1;
    }
    else
    {
        if (luaL_dofile(L, SCRIPTS_DIR "/Entry.lua") != LUA_OK)
        {
            std::fprintf(stderr, "Lua error: %s\n", lua_tostring(L, -1));
            exitCode = 1;
        }
        LuaSF_leave_state(L);
    }

    LuaSF_quiesce_state(L);
    LuaSF_shutdown_state(L);
    lua_close(L);
    return exitCode;
}
