#pragma once

#include <LuaGlue/Api.hpp>

#include <cstddef>
#include <memory>
#include <string_view>

struct lua_State;

namespace lua_glue {

using StateEnterHook = int (*)(lua_State*, void*) noexcept;
using StateTryEnterHook = int (*)(lua_State*, void*) noexcept;
using StateLeaveHook = void (*)(lua_State*, void*) noexcept;
// Reports current-thread gate ownership without blocking, allocating,
// accessing Lua, or re-entering LuaGlue. The context is the execution hook's.
using StateOwnsExecutionHook = int (*)(lua_State*, void*) noexcept;

LUAGLUE_API int InitializeState(lua_State* state);
LUAGLUE_API int EnterState(lua_State* state);
LUAGLUE_API int TryEnterState(lua_State* state) noexcept;
LUAGLUE_API void LeaveState(lua_State* state) noexcept;
LUAGLUE_API void QuiesceState(lua_State* state) noexcept;
LUAGLUE_API void ShutdownState(lua_State* state);
LUAGLUE_API int SetStateExecutionHooks(lua_State* state, StateEnterHook enter,
                                       StateTryEnterHook tryEnter,
                                       StateLeaveHook leave, void* context);
// Allows internal value access to reuse a currently owned execution gate.
// Replacing execution hooks clears this opt-in.
LUAGLUE_API int SetStateOwnsExecutionHook(lua_State* state,
                                          StateOwnsExecutionHook owns);
LUAGLUE_API int TakeDeferredCallbackError(lua_State* state, char* buffer,
                                          std::size_t capacity);

struct RegistryReferenceState;
using StateQuiesceCallback = void (*)() noexcept;

class LUAGLUE_API ExecutionScope {
public:
    explicit ExecutionScope(lua_State* state) noexcept;
    ~ExecutionScope();

    ExecutionScope(const ExecutionScope&) = delete;
    ExecutionScope& operator=(const ExecutionScope&) = delete;

    [[nodiscard]] bool active() const noexcept;

private:
    lua_State* state_{};
    bool active_{};
};

class LUAGLUE_API TryExecutionScope {
public:
    explicit TryExecutionScope(lua_State* state) noexcept;
    ~TryExecutionScope();

    TryExecutionScope(const TryExecutionScope&) = delete;
    TryExecutionScope& operator=(const TryExecutionScope&) = delete;

    [[nodiscard]] bool active() const noexcept;

private:
    lua_State* state_{};
    bool active_{};
};

class LUAGLUE_API RegistryReference {
public:
    RegistryReference() noexcept = default;
    RegistryReference(lua_State* state, int stackIndex);

    [[nodiscard]] lua_State* state() const noexcept;
    [[nodiscard]] lua_State* originState() const noexcept;
    [[nodiscard]] bool push() const;
    [[nodiscard]] bool push(lua_State* target) const;
    [[nodiscard]] bool pushUnderExecutionScope() const noexcept;
    void deferCallbackError(std::string_view label,
                            std::string_view message) const noexcept;
    [[nodiscard]] bool equals(const RegistryReference& other) const;
    explicit operator bool() const noexcept;

private:
    std::shared_ptr<RegistryReferenceState> reference_;
};

namespace detail {

// Internal, non-escaping access within an enclosing caller's lifetime.
// Public ExecutionScope always acquires its own entry and never borrows.
class LUAGLUE_API AccessScope {
public:
    explicit AccessScope(lua_State* state) noexcept;
    ~AccessScope();
    AccessScope(const AccessScope&) = delete;
    AccessScope& operator=(const AccessScope&) = delete;
    [[nodiscard]] bool active() const noexcept;

private:
    lua_State* state_{};
    bool active_{};
    bool borrowed_{};
};

LUAGLUE_API void registerLuaThreadForRegistryReference(lua_State* state);
LUAGLUE_API void retainLuaRegistryReference(const void* owner,
                                            const RegistryReference& reference);
LUAGLUE_API void releaseLuaRegistryReference(const void* owner);
LUAGLUE_API void registerStateQuiesceCallback(lua_State* state,
                                              const void* owner,
                                              StateQuiesceCallback callback);
LUAGLUE_API void unregisterStateQuiesceCallback(lua_State* state,
                                                const void* owner) noexcept;

}  // namespace detail

}  // namespace lua_glue
