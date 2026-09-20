#pragma once

#include "LuaSF.hpp"
#include <LuaGlue/Lifecycle.hpp>

namespace lua_sf {

using LuaStateExecutionScope = lua_glue::ExecutionScope;
using LuaStateTryExecutionScope = lua_glue::TryExecutionScope;
using LuaRegistryReference = lua_glue::RegistryReference;
using LuaStateQuiesceCallback = lua_glue::StateQuiesceCallback;

namespace detail {
using lua_glue::detail::registerLuaThreadForRegistryReference;
using lua_glue::detail::registerStateQuiesceCallback;
using lua_glue::detail::releaseLuaRegistryReference;
using lua_glue::detail::retainLuaRegistryReference;
using lua_glue::detail::unregisterStateQuiesceCallback;
}  // namespace detail

}  // namespace lua_sf
