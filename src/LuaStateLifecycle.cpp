#include "LuaStateLifecycle.hpp"

extern "C" LUASF_API int LuaSF_initialize_state(lua_State* state) {
    return lua_glue::InitializeState(state);
}

extern "C" LUASF_API int LuaSF_set_state_execution_hooks(
    lua_State* state, LuaSFStateEnterHook enter,
    LuaSFStateTryEnterHook tryEnter, LuaSFStateLeaveHook leave, void* context) {
    return lua_glue::SetStateExecutionHooks(state, enter, tryEnter, leave,
                                            context);
}

extern "C" LUASF_API void LuaSF_quiesce_state(lua_State* state) noexcept {
    lua_glue::QuiesceState(state);
}

extern "C" LUASF_API void LuaSF_shutdown_state(lua_State* state) {
    lua_glue::ShutdownState(state);
}

extern "C" LUASF_API int LuaSF_enter_state(lua_State* state) {
    return lua_glue::EnterState(state);
}

extern "C" LUASF_API int LuaSF_try_enter_state(lua_State* state) noexcept {
    return lua_glue::TryEnterState(state);
}

extern "C" LUASF_API void LuaSF_leave_state(lua_State* state) noexcept {
    lua_glue::LeaveState(state);
}

extern "C" LUASF_API int LuaSF_take_deferred_callback_error(
    lua_State* state, char* buffer, std::size_t capacity) {
    return lua_glue::TakeDeferredCallbackError(state, buffer, capacity);
}
