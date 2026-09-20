#pragma once

#include <LuaGlue/LuaGlue.hpp>

#include <memory>
#include <stdexcept>
#include <utility>

namespace lua_sf {

namespace detail {

template <typename T>
void attach_shared_owner(lua_State* state, int index,
                         const std::shared_ptr<T>& owner) {
    lua_glue::AttachSharedOwner(state, index,
                                std::shared_ptr<void>(owner, nullptr));
}

template <typename T>
T* get_pushed_luasf_native_object(lua_State* state) {
    return static_cast<T*>(
        lua_glue::NativePointer(state, -1, lua_glue::TypeName<T>()));
}

inline bool copy_pushed_luasf_shared_owner(lua_State* state,
                                           std::shared_ptr<void>& owner) {
    owner = lua_glue::NativeSharedOwner(state, -1);
    return owner.use_count() != 0;
}

template <typename T>
bool get_pushed_luasf_shared_object(lua_State* state,
                                    std::shared_ptr<T>& value) {
    auto* pointer = get_pushed_luasf_native_object<T>(state);
    auto owner =
        lua_glue::NativeSharedOwner(state, -1, lua_glue::TypeName<T>());
    if (pointer == nullptr || owner.use_count() == 0) {
        return false;
    }
    value = std::shared_ptr<T>(std::move(owner), pointer);
    return true;
}

}  // namespace detail

template <typename T>
struct LuaSharedObject {
    std::shared_ptr<T> owner;
};

template <typename T>
LuaSharedObject<T> wrapLuaSharedObject(std::shared_ptr<T> owner) {
    return {std::move(owner)};
}

template <typename T, typename... Args>
LuaSharedObject<T> makeLuaSharedObject(Args&&... args) {
    return wrapLuaSharedObject(
        std::make_shared<T>(std::forward<Args>(args)...));
}

template <typename T>
void mark_shared_usertype(lua_glue::StateView lua) {
    if (!lua_glue::NativeTypeTable(lua.lua_state(), lua_glue::TypeName<T>())
             .valid()) {
        throw std::logic_error(
            "Shared LuaSF type must be registered before use");
    }
}

template <typename T, typename... Bases>
void register_external_usertype(lua_glue::StateView lua) {
    auto type = lua_glue::Class<T>(
        lua_glue::NativeTypeTable(lua.lua_state(), lua_glue::TypeName<T>()));
    (lua_glue::BindBase<T, Bases>(type), ...);
}

}  // namespace lua_sf

namespace lua_glue {

template <typename T>
struct Codec<lua_sf::LuaSharedObject<T>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return Codec<std::shared_ptr<T>>::Check(state, index);
    }
    static lua_sf::LuaSharedObject<T> Read(lua_State* state, int index) {
        return {Codec<std::shared_ptr<T>>::Read(state, index)};
    }
    static int Push(lua_State* state, const lua_sf::LuaSharedObject<T>& value) {
        return Codec<std::shared_ptr<T>>::Push(state, value.owner);
    }
};

}  // namespace lua_glue
