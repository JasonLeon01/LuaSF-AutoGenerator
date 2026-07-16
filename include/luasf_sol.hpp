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

enum class LuaSFNativeLookup {
  notComposite,
  missing,
  found,
};

template <typename T>
LuaSFNativeLookup push_luasf_native_object(lua_State *state, int index) {
  index = lua_absindex(state, index);
  if (lua_type(state, index) != LUA_TUSERDATA ||
      lua_getmetatable(state, index) == 0)
    return LuaSFNativeLookup::notComposite;
  lua_getfield(state, -1, "__LuaSFNativeComposite");
  const bool isComposite = lua_toboolean(state, -1) != 0;
  lua_pop(state, 2);
  if (!isComposite)
    return LuaSFNativeLookup::notComposite;
  if (lua_getiuservalue(state, index, 1) != LUA_TTABLE) {
    lua_pop(state, 1);
    return LuaSFNativeLookup::missing;
  }
  lua_getfield(state, -1, "__nativeObjects");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 2);
    return LuaSFNativeLookup::missing;
  }
  const std::string &typeName = sol::usertype_traits<T>::qualified_name();
  lua_getfield(state, -1, typeName.c_str());
  lua_remove(state, -2);
  lua_remove(state, -2);
  if (lua_type(state, -1) == LUA_TUSERDATA)
    return LuaSFNativeLookup::found;
  lua_pop(state, 1);
  return LuaSFNativeLookup::missing;
}

template <typename T>
T *get_pushed_luasf_native_object(lua_State *state) {
  void *memory = lua_touserdata(state, -1);
  if (memory == nullptr)
    return nullptr;
  void *rawData = sol::detail::align_usertype_pointer(memory);
  void *nativeObject = *static_cast<void **>(rawData);
  if (nativeObject == nullptr || lua_getmetatable(state, -1) == 0)
    return nullptr;
  lua_getfield(state, -1, &sol::detail::base_class_cast_key()[0]);
  if (lua_type(state, -1) != LUA_TNIL) {
    void *castData = lua_touserdata(state, -1);
    if (castData == nullptr) {
      lua_pop(state, 2);
      return nullptr;
    }
    sol::detail::inheritance_cast_function cast =
        reinterpret_cast<sol::detail::inheritance_cast_function>(castData);
    nativeObject = cast(nativeObject, sol::usertype_traits<T>::qualified_name());
  }
  lua_pop(state, 2);
  return static_cast<T *>(nativeObject);
}

} // namespace detail

} // namespace lua_sf

namespace sol {

template <typename T, typename Handler>
bool sol_lua_interop_check(types<T>, lua_State *state, int index, type,
                           Handler &&, stack::record &tracking) {
  const lua_sf::detail::LuaSFNativeLookup lookup =
      lua_sf::detail::push_luasf_native_object<T>(state, index);
  if (lookup != lua_sf::detail::LuaSFNativeLookup::found)
    return false;
  T *value = lua_sf::detail::get_pushed_luasf_native_object<T>(state);
  lua_pop(state, 1);
  if (value == nullptr)
    return false;
  tracking.use(1);
  return true;
}

template <typename T>
std::pair<bool, T *> sol_lua_interop_get(types<T>, lua_State *state, int index,
                                         void *, stack::record &tracking) {
  const lua_sf::detail::LuaSFNativeLookup lookup =
      lua_sf::detail::push_luasf_native_object<T>(state, index);
  if (lookup == lua_sf::detail::LuaSFNativeLookup::notComposite)
    return {false, nullptr};
  if (lookup == lua_sf::detail::LuaSFNativeLookup::missing) {
    luaL_typeerror(state, index,
                   sol::usertype_traits<T>::qualified_name().c_str());
    return {false, nullptr};
  }
  T *value = lua_sf::detail::get_pushed_luasf_native_object<T>(state);
  lua_pop(state, 1);
  if (value == nullptr) {
    luaL_typeerror(state, index,
                   sol::usertype_traits<T>::qualified_name().c_str());
    return {false, nullptr};
  }
  tracking.use(1);
  return {true, value};
}

} // namespace sol
