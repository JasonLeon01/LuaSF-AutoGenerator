#include <cstdio>

#include <LuaSF.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: lua_stub_dump <output.d.lua>\n");
        return 2;
    }

    lua_State* state = luaL_newstate();
    if (state == nullptr)
    {
        std::fprintf(stderr, "failed to create the Lua state used to dump the LuaSF stub\n");
        return 1;
    }

    luaL_openlibs(state);

    const int result = LuaSF_write_stub(state, argv[1]);
    if (result != 0)
    {
        std::fprintf(stderr, "failed to write LuaSF stub: %s\n", argv[1]);
    }

    LuaSF_quiesce_state(state);
    LuaSF_shutdown_state(state);
    lua_close(state);
    return result;
}
