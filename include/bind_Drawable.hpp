#pragma once

#include "utils.hpp"

#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>

#include <functional>

namespace lua_sf {

class LuaDrawable : public sf::Drawable {
public:
  using DrawCallback =
      std::function<void(sf::RenderTarget &, sf::RenderStates)>;

  explicit LuaDrawable(DrawCallback drawCallback);

private:
  void draw(sf::RenderTarget &target, sf::RenderStates states) const override;

  DrawCallback m_drawCallback;
};

} // namespace lua_sf

void bind_Drawable(sol::state_view lua);
