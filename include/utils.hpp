#pragma once

#include <SFML/Audio/PlaybackDevice.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Network/SocketSelector.hpp>
#include <SFML/System/String.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/WindowHandle.hpp>
#include "luasf_sol.hpp"

#include "lua_stub.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
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

class WindowHandle
{
public:
    WindowHandle() = default;

    explicit WindowHandle(std::uintptr_t value)
        : handle_(nativeFromInteger(value))
    {
    }

    [[nodiscard]] static WindowHandle fromNative(sf::WindowHandle handle)
    {
        WindowHandle result;
        result.handle_ = handle;
        return result;
    }

    [[nodiscard]] sf::WindowHandle native() const
    {
        return handle_;
    }

    [[nodiscard]] std::uintptr_t toInteger() const
    {
        return integerFromNative(handle_);
    }

private:
    [[nodiscard]] static sf::WindowHandle nativeFromInteger(std::uintptr_t value)
    {
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) || defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID)
        return reinterpret_cast<sf::WindowHandle>(value);
#else
        return static_cast<sf::WindowHandle>(value);
#endif
    }

    [[nodiscard]] static std::uintptr_t integerFromNative(sf::WindowHandle handle)
    {
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) || defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID)
        return reinterpret_cast<std::uintptr_t>(handle);
#else
        return static_cast<std::uintptr_t>(handle);
#endif
    }

    sf::WindowHandle handle_{};
};

inline sf::WindowHandle window_handle_from_integer(std::uintptr_t value)
{
    return WindowHandle(value).native();
}

inline std::uintptr_t window_handle_to_integer(sf::WindowHandle handle)
{
    return WindowHandle::fromNative(handle).toInteger();
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

sol::table audioFramesToTable(sol::state_view lua,
                              const float* frames,
                              unsigned int frameCount,
                              unsigned int frameChannelCount);

void copyAudioFramesFromObject(const sol::object& object,
                               float* frames,
                               unsigned int frameCount,
                               unsigned int frameChannelCount);

void updateAudioFrameCount(const sol::object& object, unsigned int& frameCount, unsigned int frameCapacity);

template <typename Signature>
std::function<Signature> function_from_object(const sol::object& object);

using LongLivedMemoryBuffer = std::shared_ptr<std::vector<std::byte>>;
using LongLivedStreamObject = sol::object;

LongLivedMemoryBuffer makeLongLivedMemoryBuffer(const sol::object& object);

void rememberLongLivedMemory(const void* owner, LongLivedMemoryBuffer buffer);

void releaseLongLivedMemory(const void* owner);

void rememberLongLivedStream(const void* owner, LongLivedStreamObject stream);

void releaseLongLivedStream(const void* owner);

void releaseLongLivedResources(const void* owner);

template <typename T>
void rememberLongLivedMemory(const T& owner, LongLivedMemoryBuffer buffer);

template <typename T>
void releaseLongLivedMemory(const T& owner);

template <typename T>
void rememberLongLivedStream(const T& owner, LongLivedStreamObject stream);

template <typename T>
void releaseLongLivedStream(const T& owner);

template <typename T>
void releaseLongLivedResources(const T& owner);

template <typename T>
struct LongLivedMemoryDeleter
{
    void operator()(T* object) const noexcept;
};

template <typename T, typename... Args>
std::unique_ptr<T, LongLivedMemoryDeleter<T>> makeLongLivedMemoryObject(Args&&... args);

} // namespace lua_sf

#include "utils.inl"
