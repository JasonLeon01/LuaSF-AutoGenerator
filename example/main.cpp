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

    if (luaL_dofile(L, SCRIPTS_DIR "/Entry.lua") != LUA_OK)
    {
        fprintf(stderr, "Lua error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    lua_close(L);
    return 0;
}
