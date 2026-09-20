#include "bind_Drawable.hpp"

#include <memory>
#include <utility>

lua_sf::LuaDrawable::LuaDrawable(DrawCallback drawCallback)
    : m_drawCallback(std::move(drawCallback)) {}

void lua_sf::LuaDrawable::draw(sf::RenderTarget& target,
                               sf::RenderStates states) const {
    m_drawCallback(target, states);
}

void bind_Drawable(lua_glue::StateView lua) {
    lua_glue::Table sf = lua_sf::sf_table(lua);
    LUASF_STUB_CLASS("sf.Drawable");
    lua_glue::BindClass<sf::Drawable>(sf, "Drawable");
    lua_glue::Table drawableType = sf["Drawable"].get<lua_glue::Table>();
    lua_glue::Table callbacks = lua.create_table();
    callbacks.add("draw");
    drawableType.raw_set("__classCallbacks", callbacks);
    drawableType.set_function(
        "__classFactory", [](const lua_glue::Table& classCallbacks) {
            return lua_sf::makeLuaSharedObject<lua_sf::LuaDrawable>(
                lua_sf::function_from_object<void(sf::RenderTarget&,
                                                  sf::RenderStates)>(
                    classCallbacks["draw"].get<lua_glue::Object>()));
        });
    LUASF_STUB_CLASS("sf.LuaDrawable", "sf.Drawable");
    LUASF_STUB_FUNCTION(
        "sf.LuaDrawable", "new",
        "fun(drawCallback: fun(target: sf.RenderTarget, states: "
        "sf.RenderStates)): sf.LuaDrawable");
    auto luaDrawableType =
        lua_glue::BindClass<lua_sf::LuaDrawable>(sf, "LuaDrawable");
    lua_glue::BindBase<lua_sf::LuaDrawable, sf::Drawable>(luaDrawableType);
    luaDrawableType.set_function(
        "new", [](const lua_glue::Object& drawCallback) {
            return lua_sf::makeLuaSharedObject<lua_sf::LuaDrawable>(
                lua_sf::function_from_object<void(
                    sf::RenderTarget&, sf::RenderStates)>(drawCallback));
        });
    lua_sf::mark_shared_usertype<lua_sf::LuaDrawable>(lua);
    lua_glue::Table luaDrawableBases = lua.create_table();
    luaDrawableBases.add(drawableType);
    sf["LuaDrawable"].get<lua_glue::Table>().raw_set("__nativeBases",
                                                     luaDrawableBases);
}
