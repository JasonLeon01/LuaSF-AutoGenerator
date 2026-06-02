#pragma once

#include <SFML/Audio/PlaybackDevice.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Network/SocketSelector.hpp>
#include <SFML/System/String.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/WindowHandle.hpp>
#include <sol2/sol.hpp>

#include "lua_stub.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lua_sf {

inline sf::String to_sf_string(std::string_view value)
{
    return sf::String::fromUtf8(value.begin(), value.end());
}

inline std::string to_utf8_string(const sf::String& value)
{
    const auto utf8 = value.toUtf8();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

inline sol::table sf_table(sol::state_view lua)
{
    return lua["sf"].get_or_create<sol::table>();
}

inline sf::WindowHandle window_handle_from_integer(std::uintptr_t value)
{
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) || defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID)
    return reinterpret_cast<sf::WindowHandle>(value);
#else
    return static_cast<sf::WindowHandle>(value);
#endif
}

inline std::uintptr_t window_handle_to_integer(sf::WindowHandle handle)
{
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) || defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID)
    return reinterpret_cast<std::uintptr_t>(handle);
#else
    return static_cast<std::uintptr_t>(handle);
#endif
}

template <typename T>
inline constexpr bool is_byte_like_v =
    std::is_same_v<std::remove_cv_t<T>, std::byte> ||
    std::is_same_v<std::remove_cv_t<T>, std::uint8_t> ||
    std::is_same_v<std::remove_cv_t<T>, unsigned char>;

template <typename T>
T object_as(const sol::object& object);

template <typename T>
std::vector<T> array_from_object(const sol::object& object);

template <typename T>
std::optional<T> optional_from_object(const sol::object& object);

template <typename T>
sol::object as_lua_object(sol::state_view lua, const T& value);

template <typename T, typename Allocator>
sol::object vector_to_object(sol::state_view lua, const std::vector<T, Allocator>& values);

template <typename T>
sol::object optional_to_object(sol::state_view lua, const std::optional<T>& value);

template <typename T>
sol::object optional_to_object(sol::state_view lua, std::optional<T>&& value);

template <typename Signature>
std::function<Signature> function_from_object(const sol::object& object);

} // namespace lua_sf

#include "utils.inl"
