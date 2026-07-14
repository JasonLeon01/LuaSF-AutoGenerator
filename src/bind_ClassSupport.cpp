#include "bind_ClassSupport.hpp"

#include <memory>

lua_sf::ClassSprite::ClassSprite() : sf::Sprite(texture) {}

void bind_ClassSupport(sol::state_view lua) {
  sol::table sf = lua_sf::sf_table(lua);
  sf.new_usertype<lua_sf::ClassSprite>(
      "__ClassSprite", sol::no_constructor, sol::base_classes,
      sol::bases<sf::Sprite, sf::Drawable, sf::Transformable>());
  sf["Sprite"].get<sol::table>().set_function(
      "__classFactory", [](const sol::table &) {
        return std::make_unique<lua_sf::ClassSprite>();
      });
}
