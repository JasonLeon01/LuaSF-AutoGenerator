#pragma once

// LuaSF entry point for sol2. Binding code must include this instead of <sol2/sol.hpp>.

#include <sol/config.hpp>
#include <sol2/forward.hpp>
#include <sol2/sol.hpp>

namespace lua_sf {

// Use on all platforms so generated binding code stays identical (sol::nil is disabled on macOS).
inline constexpr decltype(sol::lua_nil) LUASF_SOL_NIL = sol::lua_nil;

} // namespace lua_sf
