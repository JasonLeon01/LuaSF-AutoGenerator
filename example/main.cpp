#include <LuaSF.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#ifndef SCRIPTS_DIR
#define SCRIPTS_DIR "."
#endif

int main()
{
    lua_State* L = LuaSF_create_state();
    if (L == nullptr)
        return 1;

    int exitCode = 0;
    if (LuaSF_enter_state(L) == 0)
    {
        fprintf(stderr, "Lua error: failed to enter Lua state\n");
        exitCode = 1;
    }
    else
    {
        if (luaL_dofile(L, SCRIPTS_DIR "/Entry.lua") != LUA_OK)
        {
            fprintf(stderr, "Lua error: %s\n", lua_tostring(L, -1));
            exitCode = 1;
        }
        LuaSF_leave_state(L);
    }

    LuaSF_quiesce_state(L);
    LuaSF_shutdown_state(L);
    lua_close(L);
    return exitCode;
}
