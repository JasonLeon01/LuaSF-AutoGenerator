#pragma once

#include "utils.hpp"

#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/Transform.hpp>

#include <cstddef>
#include <string>

namespace lua_sf {

template <std::size_t N>
void bind_MatrixT(sol::state_view lua, sol::table sfTable, const std::string& name);

} // namespace lua_sf

void bind_Matrix(sol::state_view lua);

#include "bind_Matrix.inl"
