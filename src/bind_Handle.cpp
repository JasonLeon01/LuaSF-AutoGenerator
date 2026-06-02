#include "bind_Handle.hpp"

void bind_Handle(sol::state_view lua)
{
    sol::table sf = lua_sf::sf_table(lua);
    LUASF_STUB_CLASS("sf.WindowHandle");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "fromInteger", "fun(value: integer): integer");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "toInteger", "fun(value: integer): integer");
    sol::table handle = sf["WindowHandle"].get_or_create<sol::table>();
    handle.set_function("fromInteger", [](std::uintptr_t value) { return value; });
    handle.set_function("toInteger", [](std::uintptr_t value) { return value; });
}
