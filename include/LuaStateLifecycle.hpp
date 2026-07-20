#pragma once

#include "LuaSF.hpp"

#include <memory>

struct lua_State;

namespace lua_sf {

struct LuaRegistryReferenceState;
using LuaStateQuiesceCallback = void (*)() noexcept;

class LUASF_API LuaStateExecutionScope {
public:
  explicit LuaStateExecutionScope(lua_State *state) noexcept;
  ~LuaStateExecutionScope();

  LuaStateExecutionScope(const LuaStateExecutionScope &) = delete;
  LuaStateExecutionScope &operator=(const LuaStateExecutionScope &) = delete;

  [[nodiscard]] bool active() const noexcept;

private:
  lua_State *state_{};
  bool active_{};
};

class LUASF_API LuaRegistryReference {
public:
  LuaRegistryReference() noexcept = default;
  LuaRegistryReference(lua_State *state, int stackIndex);

  [[nodiscard]] lua_State *state() const noexcept;
  [[nodiscard]] bool push() const;
  [[nodiscard]] bool equals(const LuaRegistryReference &other) const;
  explicit operator bool() const noexcept;

private:
  std::shared_ptr<LuaRegistryReferenceState> reference_;
};

namespace detail {

LUASF_API void retainLuaRegistryReference(
    const void *owner, const LuaRegistryReference &reference);
LUASF_API void releaseLuaRegistryReference(const void *owner);
LUASF_API void registerStateQuiesceCallback(
    lua_State *state, const void *owner, LuaStateQuiesceCallback callback);
LUASF_API void unregisterStateQuiesceCallback(lua_State *state,
                                              const void *owner) noexcept;

}

}
