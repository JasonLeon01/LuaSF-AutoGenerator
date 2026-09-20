#include "bind_ClassSupport.hpp"

#include <memory>

lua_sf::ClassSprite::ClassSprite() : sf::Sprite(texture) {}

void bind_ClassSupport(lua_glue::StateView lua) {
    lua_glue::Table sf = lua_sf::sf_table(lua);
    auto type = lua_glue::BindClass<lua_sf::ClassSprite>(sf, "__ClassSprite");
    lua_glue::BindBase<lua_sf::ClassSprite, sf::Sprite>(type);
    lua_glue::BindBase<lua_sf::ClassSprite, sf::Drawable>(type);
    lua_glue::BindBase<lua_sf::ClassSprite, sf::Transformable>(type);
    lua_sf::mark_shared_usertype<lua_sf::ClassSprite>(lua);
    sf["Sprite"].get<lua_glue::Table>().set_function(
        "__classFactory", [](const lua_glue::Table&) {
            return lua_sf::makeLuaSharedObject<lua_sf::ClassSprite>();
        });
}
