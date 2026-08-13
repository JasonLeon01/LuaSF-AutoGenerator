#pragma once

#include "LuaCallbackCodec.hpp"

inline void bind_CallbackViews(sol::state_view lua) {
  lua_sf::callback::bindCallbackViews(lua);
}
