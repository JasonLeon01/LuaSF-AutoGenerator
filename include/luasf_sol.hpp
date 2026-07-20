#pragma once

// LuaSF entry point for sol2. Binding code must include this instead of
// <sol2/sol.hpp>.

#include <sol/config.hpp>
#include <sol2/forward.hpp>
#include <sol2/sol.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>

namespace lua_sf {

// Use on all platforms so generated binding code stays identical (sol::nil is
// disabled on macOS).
inline constexpr decltype(sol::lua_nil) LUASF_SOL_NIL = sol::lua_nil;

namespace detail {

enum class LuaSFNativeLookup {
  notInterop,
  missing,
  found,
  external,
};

inline constexpr const char *LUASF_EXTERNAL_CAST_KEY = "__LuaSFExternalCast";
inline constexpr const char *LUASF_SHARED_OWNER_KEY = "__LuaSFSharedOwner";
inline constexpr const char *LUASF_SHARED_OWNER_METATABLE =
    "LuaSF.SharedOwner";

struct LuaSFSharedOwner {
  bool constructed = false;
  alignas(std::shared_ptr<void>)
      std::byte ownerStorage[sizeof(std::shared_ptr<void>)];

  std::shared_ptr<void> *owner() noexcept {
    return std::launder(
        reinterpret_cast<std::shared_ptr<void> *>(ownerStorage));
  }
};

inline int destroy_shared_owner(lua_State *state) noexcept {
  LuaSFSharedOwner *token = static_cast<LuaSFSharedOwner *>(
      luaL_testudata(state, 1, LUASF_SHARED_OWNER_METATABLE));
  if (token != nullptr && token->constructed) {
    token->constructed = false;
    std::destroy_at(token->owner());
  }
  return 0;
}

inline void push_shared_owner(lua_State *state,
                              const std::shared_ptr<void> &owner) {
  void *memory = lua_newuserdatauv(state, sizeof(LuaSFSharedOwner), 0);
  LuaSFSharedOwner *token =
      std::construct_at(static_cast<LuaSFSharedOwner *>(memory));
  if (luaL_newmetatable(state, LUASF_SHARED_OWNER_METATABLE) != 0) {
    lua_pushcfunction(state, &destroy_shared_owner);
    lua_setfield(state, -2, "__gc");
  }
  lua_setmetatable(state, -2);
  std::construct_at(
      reinterpret_cast<std::shared_ptr<void> *>(token->ownerStorage), owner);
  token->constructed = true;
}

template <typename T>
void attach_shared_owner(lua_State *state, int index,
                         const std::shared_ptr<T> &owner) {
  if (owner.use_count() == 0 || lua_type(state, index) != LUA_TUSERDATA)
    return;
  luaL_checkstack(state, 5, "not enough stack space for shared owner");
  index = lua_absindex(state, index);
  if (lua_getiuservalue(state, index, 1) != LUA_TTABLE) {
    lua_pop(state, 1);
    lua_newtable(state);
  }
  const std::shared_ptr<void> ownerControlBlock(owner, nullptr);
  push_shared_owner(state, ownerControlBlock);
  lua_setfield(state, -2, LUASF_SHARED_OWNER_KEY);
  lua_setiuservalue(state, index, 1);
}

inline void register_external_metatable(lua_State *state,
                                        const std::string &metatableName) {
  lua_getfield(state, LUA_REGISTRYINDEX, metatableName.c_str());
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_getfield(state, -1, &sol::detail::base_class_cast_key()[0]);
  const int castType = lua_type(state, -1);
  if (castType == LUA_TUSERDATA || castType == LUA_TLIGHTUSERDATA)
    lua_setfield(state, -2, LUASF_EXTERNAL_CAST_KEY);
  else
    lua_pop(state, 1);
  lua_pop(state, 1);
}

template <typename T>
T *get_pushed_luasf_native_object(lua_State *state);

template <typename T>
LuaSFNativeLookup push_luasf_native_object(lua_State *state, int index) {
  index = lua_absindex(state, index);
  const int originalTop = lua_gettop(state);
  if (lua_checkstack(state, 6) == 0)
    return LuaSFNativeLookup::missing;
  if (lua_type(state, index) != LUA_TUSERDATA ||
      lua_getmetatable(state, index) == 0)
    return LuaSFNativeLookup::notInterop;
  lua_getfield(state, -1, "__LuaSFNativeComposite");
  const bool isComposite = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  if (!isComposite) {
    lua_getfield(state, -1, LUASF_EXTERNAL_CAST_KEY);
    const int castType = lua_type(state, -1);
    const bool isExternalUsertype = castType == LUA_TUSERDATA ||
                                    castType == LUA_TLIGHTUSERDATA;
    lua_settop(state, originalTop);
    if (!isExternalUsertype)
      return LuaSFNativeLookup::notInterop;
    lua_pushvalue(state, index);
    return LuaSFNativeLookup::external;
  }
  lua_settop(state, originalTop);
  if (lua_getiuservalue(state, index, 1) != LUA_TTABLE) {
    lua_settop(state, originalTop);
    return LuaSFNativeLookup::missing;
  }
  lua_getfield(state, -1, "__nativeObjects");
  if (!lua_istable(state, -1)) {
    lua_settop(state, originalTop);
    return LuaSFNativeLookup::missing;
  }
  const int nativeObjectsIndex = lua_absindex(state, -1);
  const std::string &typeName = sol::usertype_traits<T>::qualified_name();
  lua_getfield(state, -1, typeName.c_str());
  if (lua_type(state, -1) == LUA_TUSERDATA &&
      get_pushed_luasf_native_object<T>(state) != nullptr) {
    lua_pushvalue(state, -1);
    lua_replace(state, originalTop + 1);
    lua_settop(state, originalTop + 1);
    return LuaSFNativeLookup::found;
  }
  lua_pop(state, 1);
  lua_pushnil(state);
  while (lua_next(state, nativeObjectsIndex) != 0) {
    if (lua_type(state, -1) == LUA_TUSERDATA &&
        get_pushed_luasf_native_object<T>(state) != nullptr) {
      lua_pushvalue(state, -1);
      lua_replace(state, originalTop + 1);
      lua_settop(state, originalTop + 1);
      return LuaSFNativeLookup::external;
    }
    lua_pop(state, 1);
  }
  lua_settop(state, originalTop);
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
  lua_getfield(state, -1, LUASF_EXTERNAL_CAST_KEY);
  if (lua_type(state, -1) == LUA_TNIL) {
    lua_pop(state, 1);
    lua_getfield(state, -1, &sol::detail::base_class_cast_key()[0]);
  }
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

inline bool copy_pushed_shared_owner(lua_State *state,
                                     std::shared_ptr<void> &result,
                                     bool &hasOwnerToken) {
  const int originalTop = lua_gettop(state);
  hasOwnerToken = false;
  if (lua_getiuservalue(state, -1, 1) != LUA_TTABLE) {
    lua_settop(state, originalTop);
    return false;
  }
  lua_getfield(state, -1, LUASF_SHARED_OWNER_KEY);
  hasOwnerToken = lua_type(state, -1) != LUA_TNIL;
  LuaSFSharedOwner *token = static_cast<LuaSFSharedOwner *>(
      luaL_testudata(state, -1, LUASF_SHARED_OWNER_METATABLE));
  const bool success = token != nullptr && token->constructed &&
                       token->owner()->use_count() != 0;
  if (success)
    result = *token->owner();
  lua_settop(state, originalTop);
  return success;
}

inline bool push_single_shared_owner_dependency(lua_State *state) {
  const int originalTop = lua_gettop(state);
  if (lua_getiuservalue(state, -1, 1) != LUA_TTABLE) {
    lua_settop(state, originalTop);
    return false;
  }
  const int dependenciesIndex = lua_absindex(state, -1);
  bool valid = true;
  bool foundFreeList = false;
  bool foundDependency = false;
  lua_pushnil(state);
  while (lua_next(state, dependenciesIndex) != 0) {
    const bool integerKey = lua_isinteger(state, -2) != 0;
    const lua_Integer key = integerKey ? lua_tointeger(state, -2) : 0;
    const bool freeListEntry =
        key == 1 && !foundFreeList && lua_isinteger(state, -1) != 0 &&
        lua_tointeger(state, -1) == 0;
    const bool dependencyEntry = key == 2 && !foundDependency &&
                                 lua_type(state, -1) == LUA_TUSERDATA;
    if (!integerKey || (!freeListEntry && !dependencyEntry))
      valid = false;
    else if (freeListEntry)
      foundFreeList = true;
    else if (dependencyEntry)
      foundDependency = true;
    lua_pop(state, 1);
  }
  if (!valid || !foundFreeList || !foundDependency) {
    lua_settop(state, originalTop);
    return false;
  }
  lua_rawgeti(state, dependenciesIndex, 2);
  if (lua_type(state, -1) != LUA_TUSERDATA) {
    lua_settop(state, originalTop);
    return false;
  }
  lua_remove(state, dependenciesIndex);
  return true;
}

inline bool copy_pushed_luasf_shared_owner(lua_State *state,
                                           std::shared_ptr<void> &result) {
  if (lua_type(state, -1) != LUA_TUSERDATA ||
      lua_checkstack(state, 4) == 0)
    return false;
  const int originalTop = lua_gettop(state);
  lua_pushvalue(state, -1);
  std::unordered_set<const void *> visited;
  constexpr std::size_t maximumOwnerChainLength = 64;
  bool success = false;
  for (std::size_t count = 0; count < maximumOwnerChainLength; ++count) {
    const void *identity = lua_topointer(state, -1);
    if (identity == nullptr || !visited.insert(identity).second)
      break;
    bool hasOwnerToken = false;
    if (copy_pushed_shared_owner(state, result, hasOwnerToken)) {
      success = true;
      break;
    }
    if (hasOwnerToken || !push_single_shared_owner_dependency(state))
      break;
    lua_replace(state, -2);
  }
  lua_settop(state, originalTop);
  return success;
}

template <typename T>
bool get_pushed_luasf_shared_object(lua_State *state,
                                    std::shared_ptr<T> &result) {
  T *nativeObject = get_pushed_luasf_native_object<T>(state);
  if (nativeObject == nullptr)
    return false;
  std::shared_ptr<void> owner;
  if (!copy_pushed_luasf_shared_owner(state, owner))
    return false;
  result = std::shared_ptr<T>(std::move(owner), nativeObject);
  return true;
}

} // namespace detail

template <typename T> struct LuaSharedObject {
  std::shared_ptr<T> owner;
};

template <typename T>
LuaSharedObject<T> wrapLuaSharedObject(std::shared_ptr<T> owner) {
  return {std::move(owner)};
}

template <typename T, typename... Args>
LuaSharedObject<T> makeLuaSharedObject(Args &&...args) {
  return wrapLuaSharedObject(
      std::make_shared<T>(std::forward<Args>(args)...));
}

template <typename T>
int sol_lua_push(lua_State *state, const LuaSharedObject<T> &value) {
  const int pushed = sol::stack::push(state, value.owner);
  if (pushed == 1)
    detail::attach_shared_owner(state, -1, value.owner);
  return pushed;
}

template <typename T> void mark_shared_usertype(sol::state_view lua) {
  lua_State *state = lua.lua_state();
  detail::register_external_metatable(
      state, sol::usertype_traits<T>::metatable());
  detail::register_external_metatable(
      state, sol::usertype_traits<const T>::metatable());
  detail::register_external_metatable(
      state, sol::usertype_traits<T *>::metatable());
  detail::register_external_metatable(
      state, sol::usertype_traits<const T *>::metatable());
  detail::register_external_metatable(
      state, sol::usertype_traits<sol::d::u<T>>::metatable());
}

template <typename T, typename... Bases>
void register_external_usertype(sol::state_view lua) {
  (void)sizeof...(Bases);
  mark_shared_usertype<T>(lua);
}

} // namespace lua_sf

namespace sol {

template <typename T, typename Handler>
bool sol_lua_interop_check(types<T>, lua_State *state, int index, type,
                           Handler &&, stack::record &tracking) {
  const lua_sf::detail::LuaSFNativeLookup lookup =
      lua_sf::detail::push_luasf_native_object<T>(state, index);
  if (lookup != lua_sf::detail::LuaSFNativeLookup::found &&
      lookup != lua_sf::detail::LuaSFNativeLookup::external)
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
  if (lookup == lua_sf::detail::LuaSFNativeLookup::notInterop)
    return {false, nullptr};
  if (lookup == lua_sf::detail::LuaSFNativeLookup::missing) {
    luaL_typeerror(state, index,
                   sol::usertype_traits<T>::qualified_name().c_str());
    return {false, nullptr};
  }
  T *value = lua_sf::detail::get_pushed_luasf_native_object<T>(state);
  lua_pop(state, 1);
  if (value == nullptr) {
    if (lookup == lua_sf::detail::LuaSFNativeLookup::external)
      return {false, nullptr};
    luaL_typeerror(state, index,
                   sol::usertype_traits<T>::qualified_name().c_str());
    return {false, nullptr};
  }
  tracking.use(1);
  return {true, value};
}

} // namespace sol
