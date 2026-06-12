#include "bind_Handle.hpp"

void bind_Handle(sol::state_view lua)
{
    sol::table sf = lua_sf::sf_table(lua);
    LUASF_STUB_CLASS("sf.WindowHandle");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "new", "fun(value: integer): sf.WindowHandle");
    LUASF_STUB_OVERLOAD("sf.WindowHandle", "new", "fun(): sf.WindowHandle");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "fromInteger", "fun(value: integer): sf.WindowHandle");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "toInteger", "fun(self: sf.WindowHandle): integer");

    auto type_sf__WindowHandle = sf.new_usertype<lua_sf::WindowHandle>("WindowHandle",
        sol::constructors<lua_sf::WindowHandle(std::uintptr_t), lua_sf::WindowHandle()>()
    );
    type_sf__WindowHandle.set_function("fromInteger",
        [](std::uintptr_t value) {
            return lua_sf::WindowHandle(value);
        }
    );
    type_sf__WindowHandle.set_function("toInteger",
        [](const lua_sf::WindowHandle& self) -> std::uintptr_t {
            return self.toInteger();
        }
    );
}
