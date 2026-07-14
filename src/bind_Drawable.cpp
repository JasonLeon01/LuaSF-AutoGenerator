#include "bind_Drawable.hpp"

#include <memory>
#include <utility>

lua_sf::LuaDrawable::LuaDrawable(sol::protected_function drawCallback)
    : m_drawCallback(std::move(drawCallback)) {}

void lua_sf::LuaDrawable::draw(sf::RenderTarget &target,
                               sf::RenderStates states) const {
  sol::protected_function_result result =
      m_drawCallback(std::ref(target), states);
  throw_on_lua_error(result);
}

void bind_Drawable(sol::state_view lua) {
  sol::table sf = lua_sf::sf_table(lua);
  LUASF_STUB_CLASS("sf.Drawable");
  sf.new_usertype<sf::Drawable>("Drawable", sol::no_constructor);
  sol::table drawableType = sf["Drawable"].get<sol::table>();
  sol::table callbacks = lua.create_table();
  callbacks.add("draw");
  drawableType.raw_set("__classCallbacks", callbacks);
  drawableType.set_function(
      "__classFactory", [](const sol::table &classCallbacks) {
        return std::make_unique<lua_sf::LuaDrawable>(
            classCallbacks["draw"].get<sol::protected_function>());
      });
  LUASF_STUB_CLASS("sf.LuaDrawable", "sf.Drawable");
  LUASF_STUB_FUNCTION("sf.LuaDrawable", "new",
                      "fun(drawCallback: fun(target: sf.RenderTarget, states: "
                      "sf.RenderStates)): sf.LuaDrawable");
  sf.new_usertype<lua_sf::LuaDrawable>(
      "LuaDrawable",
      sol::constructors<lua_sf::LuaDrawable(sol::protected_function)>(),
      sol::base_classes, sol::bases<sf::Drawable>());
  sol::table luaDrawableBases = lua.create_table();
  luaDrawableBases.add(drawableType);
  sf["LuaDrawable"].get<sol::table>().raw_set("__nativeBases",
                                              luaDrawableBases);
}
