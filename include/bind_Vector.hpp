#pragma once

#include "utils.hpp"

#include <SFML/Graphics/Glsl.hpp>
#include <SFML/System/Angle.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>

#include <string>

namespace lua_sf {

template <typename T>
void bind_Vector2T(sol::table sfTable, const std::string &name);

template <typename T>
void bind_Vector3T(sol::table sfTable, const std::string &name);

template <typename T>
void bind_Vector4T(sol::table sfTable, const std::string &name);

} // namespace lua_sf

void bind_Vector(sol::state_view lua);

#include "bind_Vector.inl"
