#include "bind_Drawable.hpp"

#include <memory>
#include <utility>

lua_sf::LuaDrawable::LuaDrawable(DrawCallback drawCallback)
    : m_drawCallback(std::move(drawCallback)) {}

void lua_sf::LuaDrawable::draw(sf::RenderTarget &target,
                               sf::RenderStates states) const {
  m_drawCallback(target, states);
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
        return lua_sf::makeLuaSharedObject<lua_sf::LuaDrawable>(
            lua_sf::function_from_object<void(sf::RenderTarget &,
                                              sf::RenderStates)>(
                classCallbacks["draw"].get<sol::object>()));
      });
  LUASF_STUB_CLASS("sf.LuaDrawable", "sf.Drawable");
  LUASF_STUB_FUNCTION("sf.LuaDrawable", "new",
                      "fun(drawCallback: fun(target: sf.RenderTarget, states: "
                      "sf.RenderStates)): sf.LuaDrawable");
  auto luaDrawableType = sf.new_usertype<lua_sf::LuaDrawable>(
      "LuaDrawable", sol::no_constructor, sol::base_classes,
      sol::bases<sf::Drawable>());
  luaDrawableType.set_function(
      "new", sol::factories([](const sol::object &drawCallback) {
        return lua_sf::makeLuaSharedObject<lua_sf::LuaDrawable>(
            lua_sf::function_from_object<void(sf::RenderTarget &,
                                              sf::RenderStates)>(drawCallback));
      }));
  lua_sf::mark_shared_usertype<lua_sf::LuaDrawable>(lua);
  sol::table luaDrawableBases = lua.create_table();
  luaDrawableBases.add(drawableType);
  sf["LuaDrawable"].get<sol::table>().raw_set("__nativeBases",
                                              luaDrawableBases);
}
