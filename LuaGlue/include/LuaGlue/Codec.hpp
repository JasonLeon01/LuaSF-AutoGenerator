#pragma once

#include "Native.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <variant>

namespace lua_glue {

namespace detail {
template <typename F>
int PushCallable(lua_State*, F&&);
LUAGLUE_API int PushString(lua_State*, std::string_view);
template <typename T>
struct IsReference : std::false_type {};
template <typename T>
struct IsReference<std::reference_wrapper<T>> : std::true_type {};
template <typename T>
struct IsShared : std::false_type {};
template <typename T>
struct IsShared<std::shared_ptr<T>> : std::true_type {};
template <typename T>
struct IsUnique : std::false_type {};
template <typename T, typename D>
struct IsUnique<std::unique_ptr<T, D>> : std::true_type {};
template <typename T>
struct IsTuple : std::false_type {};
template <typename... T>
struct IsTuple<std::tuple<T...>> : std::true_type {};
template <typename A, typename B>
struct IsTuple<std::pair<A, B>> : std::true_type {};
template <typename T>
inline constexpr bool IsCallable = requires {
    &T::operator();
} || (std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>);
}  // namespace detail

template <typename T, typename Enable = void>
struct Codec {
    static constexpr bool native = true;
    static bool Check(lua_State* state, int index) {
        return NativePointer(state, index, TypeName<T>()) != nullptr;
    }
    static T Read(lua_State* state, int index) {
        auto* pointer =
            static_cast<T*>(NativePointer(state, index, TypeName<T>()));
        if (!pointer) {
            throw std::runtime_error("expected " + std::string(TypeName<T>()));
        }
        return T(*pointer);
    }
    template <typename V>
    static int Push(lua_State* state, V&& value) {
        auto owner = std::make_shared<T>(std::forward<V>(value));
        T* pointer = owner.get();
        return detail::PushNative(state, TypeName<T>(), pointer,
                                  std::move(owner), false);
    }
};

template <typename T>
struct Codec<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        if (lua_type(state, index) != LUA_TNUMBER) {
            return false;
        }
        if (lua_isinteger(state, index)) {
            const lua_Integer value = lua_tointeger(state, index);
            if constexpr (std::is_signed_v<T>) {
                if constexpr (std::numeric_limits<T>::digits >=
                              std::numeric_limits<lua_Integer>::digits) {
                    return true;
                }
                return value >= static_cast<lua_Integer>(
                                    std::numeric_limits<T>::min()) &&
                       value <= static_cast<lua_Integer>(
                                    std::numeric_limits<T>::max());
            } else {
                if (value < 0) {
                    return false;
                }
                return static_cast<lua_Unsigned>(value) <=
                       std::numeric_limits<T>::max();
            }
        }
        const lua_Number value = lua_tonumber(state, index);
        if (!std::isfinite(value) || std::trunc(value) != value) {
            return false;
        }
        const lua_Number limit =
            std::ldexp(lua_Number{1}, std::numeric_limits<T>::digits);
        if constexpr (std::is_signed_v<T>) {
            return value >= -limit && value < limit;
        } else {
            return value >= 0 && value < limit;
        }
    }
    static T Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected a finite, in-range integer");
        }
        return lua_isinteger(state, index)
                   ? static_cast<T>(lua_tointeger(state, index))
                   : static_cast<T>(lua_tonumber(state, index));
    }
    static int Push(lua_State* state, T value) {
        if constexpr (std::is_unsigned_v<T> &&
                      sizeof(T) >= sizeof(lua_Integer)) {
            if (value >
                static_cast<T>(std::numeric_limits<lua_Integer>::max())) {
                lua_pushnumber(state, static_cast<lua_Number>(value));
                return 1;
            }
        }
        lua_pushinteger(state, static_cast<lua_Integer>(value));
        return 1;
    }
};

template <>
struct Codec<bool> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_type(state, index) == LUA_TBOOLEAN;
    }
    static bool Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected boolean");
        }
        return lua_toboolean(state, index) != 0;
    }
    static int Push(lua_State* state, bool value) {
        lua_pushboolean(state, value);
        return 1;
    }
};

template <typename T>
struct Codec<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_type(state, index) == LUA_TNUMBER;
    }
    static T Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected number");
        }
        return static_cast<T>(lua_tonumber(state, index));
    }
    static int Push(lua_State* state, T value) {
        lua_pushnumber(state, static_cast<lua_Number>(value));
        return 1;
    }
};

template <typename T>
struct Codec<T, std::enable_if_t<std::is_enum_v<T>>> {
    static constexpr bool native = false;
    using Base = Codec<std::underlying_type_t<T>>;
    static bool Check(lua_State* state, int index) {
        return Base::Check(state, index);
    }
    static T Read(lua_State* state, int index) {
        return static_cast<T>(Base::Read(state, index));
    }
    static int Push(lua_State* state, T value) {
        return Base::Push(state, static_cast<std::underlying_type_t<T>>(value));
    }
};

template <>
struct Codec<std::string> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_type(state, index) == LUA_TSTRING;
    }
    static std::string Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected string");
        }
        std::size_t size = 0;
        const char* value = lua_tolstring(state, index, &size);
        return {value, size};
    }
    static int Push(lua_State* state, std::string_view value) {
        return detail::PushString(state, value);
    }
};

template <>
struct Codec<std::string_view> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return Codec<std::string>::Check(state, index);
    }
    static std::string_view Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected string");
        }
        std::size_t size = 0;
        const char* value = lua_tolstring(state, index, &size);
        return {value, size};
    }
    static int Push(lua_State* state, std::string_view value) {
        return detail::PushString(state, value);
    }
};

template <>
struct Codec<Nil> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_isnoneornil(state, index);
    }
    static Nil Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected nil");
        }
        return {};
    }
    static int Push(lua_State* state, Nil) {
        lua_pushnil(state);
        return 1;
    }
};
template <>
struct Codec<std::nullptr_t> : Codec<Nil> {};
template <>
struct Codec<std::monostate> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return Codec<Nil>::Check(state, index);
    }
    static std::monostate Read(lua_State* state, int index) {
        Codec<Nil>::Read(state, index);
        return {};
    }
    static int Push(lua_State* state, std::monostate) {
        lua_pushnil(state);
        return 1;
    }
};

template <typename T>
struct Codec<T, std::enable_if_t<std::is_base_of_v<Object, T>>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        if constexpr (std::is_base_of_v<Table, T>) {
            return lua_istable(state, index);
        }
        if constexpr (std::is_base_of_v<Function, T>) {
            return lua_isfunction(state, index);
        }
        return lua_type(state, index) != LUA_TNONE;
    }
    static T Read(lua_State* state, int index) {
        return T(state, index);
    }
    static int Push(lua_State* state, const T& value) {
        return value.push(state);
    }
};

template <>
struct Codec<LightUserdata> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_islightuserdata(state, index);
    }
    static LightUserdata Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected light userdata");
        }
        return LightUserdata(lua_touserdata(state, index));
    }
    static int Push(lua_State* state, LightUserdata value) {
        lua_pushlightuserdata(state, value.value);
        return 1;
    }
};

template <typename T>
struct Codec<T*> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        if (lua_isnil(state, index)) {
            return true;
        }
        if constexpr (std::is_void_v<std::remove_const_t<T>>) {
            return lua_isuserdata(state, index);
        } else {
            return NativePointer(state, index, TypeName<T>()) &&
                   (std::is_const_v<T> || !NativeIsConst(state, index));
        }
    }
    static T* Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected " + std::string(TypeName<T>()) +
                                     " pointer");
        }
        if (lua_isnil(state, index)) {
            return nullptr;
        }
        if constexpr (std::is_void_v<std::remove_const_t<T>>) {
            return static_cast<T*>(lua_touserdata(state, index));
        } else {
            return static_cast<T*>(NativePointer(state, index, TypeName<T>()));
        }
    }
    static int Push(lua_State* state, T* value) {
        if constexpr (std::is_void_v<std::remove_const_t<T>>) {
            if (value) {
                lua_pushlightuserdata(
                    state, const_cast<void*>(static_cast<const void*>(value)));
            } else {
                lua_pushnil(state);
            }
            return 1;
        } else {
            return detail::PushNative(
                state, TypeName<T>(),
                const_cast<std::remove_const_t<T>*>(value), {},
                std::is_const_v<T>);
        }
    }
};

template <>
struct Codec<const char*> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_isnil(state, index) || lua_type(state, index) == LUA_TSTRING;
    }
    static const char* Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected string or nil");
        }
        return lua_tostring(state, index);
    }
    static int Push(lua_State* state, const char* value) {
        if (!value) {
            lua_pushnil(state);
            return 1;
        }
        return detail::PushString(state, value);
    }
};

template <typename T>
struct Codec<std::shared_ptr<T>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_isnil(state, index) ||
               (Codec<T*>::Check(state, index) &&
                NativeSharedOwner(state, index, TypeName<T>()).use_count() !=
                    0);
    }
    static std::shared_ptr<T> Read(lua_State* state, int index) {
        if (lua_isnil(state, index)) {
            return {};
        }
        auto owner = NativeSharedOwner(state, index, TypeName<T>());
        T* pointer = Codec<T*>::Read(state, index);
        if (!owner.use_count()) {
            throw std::runtime_error("native value has no shared ownership");
        }
        return {std::move(owner), pointer};
    }
    static int Push(lua_State* state, const std::shared_ptr<T>& value) {
        std::shared_ptr<void> owner(
            value, const_cast<std::remove_const_t<T>*>(value.get()));
        return detail::PushNative(
            state, TypeName<T>(),
            const_cast<std::remove_const_t<T>*>(value.get()), std::move(owner),
            std::is_const_v<T>);
    }
};

template <typename T>
struct Codec<std::optional<T>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_isnil(state, index) || lua_glue::Check<T>(state, index);
    }
    static std::optional<T> Read(lua_State* state, int index) {
        if (lua_isnil(state, index)) {
            return {};
        }
        return lua_glue::Read<T>(state, index);
    }
    static int Push(lua_State* state, const std::optional<T>& value) {
        if (!value) {
            lua_pushnil(state);
            return 1;
        }
        return lua_glue::Push(state, *value);
    }
};

template <typename T, typename A>
struct Codec<std::vector<T, A>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        if (!lua_istable(state, index)) {
            return false;
        }
        Table table(state, index);
        const auto size = table.size();
        for (std::size_t i = 1; i <= size; ++i) {
            if (!table.raw_get<Object>(i).template is<T>()) {
                return false;
            }
        }
        return true;
    }
    static std::vector<T, A> Read(lua_State* state, int index) {
        Table table(state, index);
        std::vector<T, A> result;
        const auto size = table.size();
        result.reserve(size);
        for (std::size_t i = 1; i <= size; ++i) {
            result.push_back(table.raw_get<T>(i));
        }
        return result;
    }
    static int Push(lua_State* state, const std::vector<T, A>& value) {
        Table table =
            StateView(state).create_table(static_cast<int>(value.size()), 0);
        for (std::size_t i = 0; i < value.size(); ++i) {
            table.raw_set(i + 1, static_cast<const T&>(value[i]));
        }
        return table.push(state);
    }
};

template <typename T, std::size_t N>
struct Codec<std::array<T, N>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_istable(state, index) && lua_rawlen(state, index) == N &&
               Codec<std::vector<T>>::Check(state, index);
    }
    static std::array<T, N> Read(lua_State* state, int index) {
        if (!Check(state, index)) {
            throw std::runtime_error("expected fixed-size array");
        }
        Table table(state, index);
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<T, N>{table.raw_get<T>(I + 1)...};
        }(std::make_index_sequence<N>{});
    }
    static int Push(lua_State* state, const std::array<T, N>& value) {
        Table table = StateView(state).create_table(static_cast<int>(N), 0);
        for (std::size_t i = 0; i < N; ++i) {
            table.raw_set(i + 1, value[i]);
        }
        return table.push(state);
    }
};

namespace detail {
template <typename Map>
struct MapCodec {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        if (!lua_istable(state, index)) {
            return false;
        }
        for (const auto& [key, value] : Table(state, index)) {
            if (!key.template is<typename Map::key_type>() ||
                !value.template is<typename Map::mapped_type>()) {
                return false;
            }
        }
        return true;
    }
    static Map Read(lua_State* state, int index) {
        Map result;
        for (const auto& [key, value] : Table(state, index)) {
            result.emplace(key.template as<typename Map::key_type>(),
                           value.template as<typename Map::mapped_type>());
        }
        return result;
    }
    static int Push(lua_State* state, const Map& value) {
        Table table =
            StateView(state).create_table(0, static_cast<int>(value.size()));
        for (const auto& [key, entry] : value) {
            table.raw_set(key, entry);
        }
        return table.push(state);
    }
};
}  // namespace detail
template <typename K, typename V, typename C, typename A>
struct Codec<std::map<K, V, C, A>> : detail::MapCodec<std::map<K, V, C, A>> {};
template <typename K, typename V, typename H, typename E, typename A>
struct Codec<std::unordered_map<K, V, H, E, A>>
    : detail::MapCodec<std::unordered_map<K, V, H, E, A>> {};

template <typename... T>
struct Codec<std::variant<T...>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return (lua_glue::Check<T>(state, index) || ...);
    }
    template <std::size_t I = 0>
    static std::variant<T...> Read(lua_State* state, int index) {
        if constexpr (I == sizeof...(T)) {
            throw std::runtime_error("no matching variant alternative");
        } else {
            using U = std::variant_alternative_t<I, std::variant<T...>>;
            if (lua_glue::Check<U>(state, index)) {
                return std::variant<T...>(std::in_place_index<I>,
                                          lua_glue::Read<U>(state, index));
            }
            return Read<I + 1>(state, index);
        }
    }
    static int Push(lua_State* state, const std::variant<T...>& value) {
        return std::visit(
            [state](const auto& entry) {
                return lua_glue::Push(state, entry);
            },
            value);
    }
};

template <typename T>
struct Codec<T, std::enable_if_t<detail::IsTuple<T>::value>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        if (!lua_istable(state, index)) {
            return false;
        }
        Table table(state, index);
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (table.raw_get<Object>(I + 1)
                        .template is<std::tuple_element_t<I, T>>() &&
                    ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }
    static T Read(lua_State* state, int index) {
        Table table(state, index);
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return T{table.raw_get<std::tuple_element_t<I, T>>(I + 1)...};
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }
    static int Push(lua_State* state, const T& value) {
        Table table = StateView(state).create_table(std::tuple_size_v<T>, 1);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (table.raw_set(I + 1, std::get<I>(value)), ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
        table.raw_set("n", std::tuple_size_v<T>);
        return table.push(state);
    }
};

template <typename R, typename... A>
struct Codec<std::function<R(A...)>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        return lua_isfunction(state, index);
    }
    static std::function<R(A...)> Read(lua_State* state, int index) {
        Function function(state, index);
        return [function = std::move(function)](A... args) -> R {
            CallResult result = function(std::forward<A>(args)...);
            if (!result.valid()) {
                throw std::runtime_error(result.error());
            }
            if constexpr (!std::is_void_v<R>) {
                return result.template get<R>();
            }
        };
    }
    static int Push(lua_State* state, const std::function<R(A...)>& value) {
        return detail::PushCallable(state, value);
    }
};

template <typename T>
bool Check(lua_State* state, int index) {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_reference_v<T> && Codec<U>::native) {
        return Codec<U>::Check(state, index) &&
               (std::is_const_v<std::remove_reference_t<T>> ||
                !NativeIsConst(state, index));
    } else {
        return Codec<U>::Check(state, index);
    }
}

template <typename T>
T Read(lua_State* state, int index) {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_reference_v<T> && Codec<U>::native) {
        if (!Check<T>(state, index)) {
            throw std::runtime_error("expected " + std::string(TypeName<T>()));
        }
        return *static_cast<std::remove_reference_t<T>*>(
            NativePointer(state, index, TypeName<T>()));
    } else {
        return Codec<U>::Read(state, index);
    }
}

template <typename T>
int Push(lua_State* state, T&& value) {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<std::decay_t<T>, lua_CFunction>) {
        lua_pushcfunction(state, static_cast<lua_CFunction>(value));
        return 1;
    } else if constexpr (std::is_function_v<U>) {
        return detail::PushCallable(state, &value);
    } else if constexpr (std::is_same_v<U, FieldRef>) {
        return value.object().push(state);
    } else if constexpr (std::is_same_v<U, Arguments> ||
                         std::is_same_v<U, StackValue> ||
                         std::is_same_v<U, MultipleResults>) {
        return value.push(state);
    } else if constexpr (std::is_same_v<U, std::nullptr_t>) {
        lua_pushnil(state);
        return 1;
    } else if constexpr (std::is_array_v<U> &&
                         std::is_same_v<
                             std::remove_cv_t<std::remove_extent_t<U>>, char>) {
        return detail::PushString(state, std::string_view(value));
    } else if constexpr (std::is_same_v<U, char*>) {
        return Codec<const char*>::Push(state, value);
    } else if constexpr (detail::IsReference<U>::value) {
        using V = std::remove_cvref_t<decltype(value.get())>;
        if constexpr (Codec<V>::native) {
            return Codec<decltype(&value.get())>::Push(state, &value.get());
        } else {
            return lua_glue::Push(state, value.get());
        }
    } else if constexpr (detail::IsUnique<U>::value) {
        using E = typename U::element_type;
        std::shared_ptr<E> owner(std::forward<T>(value));
        return Codec<std::shared_ptr<E>>::Push(state, owner);
    } else if constexpr (detail::IsCallable<U>) {
        return detail::PushCallable(state, std::forward<T>(value));
    } else {
        return Codec<U>::Push(state, std::forward<T>(value));
    }
}

template <typename Container>
const Container& AsTable(const Container& value) {
    return value;
}
template <typename Container>
Container AsTable(Container&& value) {
    return std::forward<Container>(value);
}

}  // namespace lua_glue
