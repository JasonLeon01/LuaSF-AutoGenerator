#pragma once

#include "utils.hpp"

#include <SFML/Graphics/Rect.hpp>

#include <string>

namespace lua_sf {

template <typename T>
void bind_RectT(sol::state_view lua, sol::table sfTable,
                const std::string &name);

} // namespace lua_sf

void bind_Rect(sol::state_view lua);

#include "bind_Rect.inl"
