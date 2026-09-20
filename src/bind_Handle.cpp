#include "bind_Handle.hpp"

void bind_Handle(lua_glue::StateView lua) {
    lua_glue::Table sf = lua_sf::sf_table(lua);
    LUASF_STUB_CLASS("sf.WindowHandle");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "new",
                        "fun(value: integer): sf.WindowHandle");
    LUASF_STUB_OVERLOAD("sf.WindowHandle", "new", "fun(): sf.WindowHandle");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "fromInteger",
                        "fun(value: integer): sf.WindowHandle");
    LUASF_STUB_FUNCTION("sf.WindowHandle", "toInteger",
                        "fun(self: sf.WindowHandle): integer");

    auto type_sf__WindowHandle =
        lua_glue::BindClass<lua_sf::WindowHandle>(sf, "WindowHandle");
    lua_glue::BindCallable(type_sf__WindowHandle, "new", [] {
        return lua_sf::WindowHandle();
    });
    lua_glue::BindCallable(type_sf__WindowHandle, "new",
                           [](lua_sf::LuaIntegral<std::uintptr_t> value) {
                               return lua_sf::WindowHandle(value.value());
                           });
    type_sf__WindowHandle.set_function(
        "fromInteger", [](lua_sf::LuaIntegral<std::uintptr_t> value) {
            return lua_sf::WindowHandle(value.value());
        });
    type_sf__WindowHandle.set_function(
        "toInteger", [](const lua_sf::WindowHandle& self) -> std::uintptr_t {
            return self.toInteger();
        });
}
