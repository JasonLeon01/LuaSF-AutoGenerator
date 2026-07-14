#pragma once

#include "utils.hpp"

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>

namespace lua_sf {

class ClassSpriteTexture {
protected:
  sf::Texture texture;
};

class ClassSprite final : private ClassSpriteTexture, public sf::Sprite {
public:
  ClassSprite();
};

} // namespace lua_sf

void bind_ClassSupport(sol::state_view lua);
