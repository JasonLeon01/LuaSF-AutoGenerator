#pragma once

// LuaSF entry point for sol2. Binding code must include this instead of
// <sol2/sol.hpp>.

#include <sol/config.hpp>
#include <sol2/forward.hpp>
#include <sol2/sol.hpp>

#include <string>
#include <utility>

namespace lua_sf {

// Use on all platforms so generated binding code stays identical (sol::nil is
// disabled on macOS).
inline constexpr decltype(sol::lua_nil) LUASF_SOL_NIL = sol::lua_nil;

namespace detail {

template <typename T>
bool push_ludork_native_object(lua_State *state, int index) {
  index = lua_absindex(state, index);
  if (lua_type(state, index) != LUA_TUSERDATA ||
      lua_getmetatable(state, index) == 0)
    return false;
  lua_getfield(state, -1, "__LuaSFNativeComposite");
  const bool isComposite = lua_toboolean(state, -1) != 0;
  lua_pop(state, 2);
  if (!isComposite)
    return false;
  if (lua_getiuservalue(state, index, 1) != LUA_TTABLE) {
    lua_pop(state, 1);
    return false;
  }
  lua_getfield(state, -1, "__nativeObjects");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 2);
    return false;
  }
  const std::string &typeName = sol::usertype_traits<T>::qualified_name();
  lua_getfield(state, -1, typeName.c_str());
  lua_remove(state, -2);
  lua_remove(state, -2);
  if (lua_type(state, -1) == LUA_TUSERDATA)
    return true;
  lua_pop(state, 1);
  return false;
}

} // namespace detail

} // namespace lua_sf

namespace sol {

template <typename T, typename Handler>
bool sol_lua_interop_check(types<T>, lua_State *state, int index, type,
                           Handler &&, stack::record &tracking) {
  if (!lua_sf::detail::push_ludork_native_object<T>(state, index))
    return false;
  lua_pop(state, 1);
  tracking.use(1);
  return true;
}

template <typename T>
std::pair<bool, T *> sol_lua_interop_get(types<T>, lua_State *state, int index,
                                         void *, stack::record &tracking) {
  if (!lua_sf::detail::push_ludork_native_object<T>(state, index))
    return {false, nullptr};
  stack::record nativeTracking{};
  T *value = stack::get<T *>(state, -1, nativeTracking);
  lua_pop(state, 1);
  tracking.use(1);
  return {true, value};
}

} // namespace sol
