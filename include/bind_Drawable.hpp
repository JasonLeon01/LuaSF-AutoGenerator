#pragma once

#include "utils.hpp"

#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>

namespace lua_sf {

class LuaDrawable : public sf::Drawable {
public:
  explicit LuaDrawable(sol::protected_function drawCallback);

private:
  void draw(sf::RenderTarget &target, sf::RenderStates states) const override;

  sol::protected_function m_drawCallback;
};

} // namespace lua_sf

void bind_Drawable(sol::state_view lua);
