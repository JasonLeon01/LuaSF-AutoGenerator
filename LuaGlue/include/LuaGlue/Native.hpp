#pragma once

#include "Api.hpp"
#include "Value.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lua_glue {

template <typename T>
constexpr std::string_view TypeName() noexcept {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (!std::is_same_v<T, U>) {
        return TypeName<U>();
    } else {
#if defined(_MSC_VER)
        constexpr std::string_view signature = __FUNCSIG__;
        constexpr auto start = signature.find("TypeName<") + 9;
        constexpr auto stop = signature.rfind(">(void)");
        constexpr auto name = signature.substr(start, stop - start);
        if constexpr (name.starts_with("class ")) {
            return name.substr(6);
        } else if constexpr (name.starts_with("struct ")) {
            return name.substr(7);
        } else if constexpr (name.starts_with("enum ")) {
            return name.substr(5);
        } else {
            return name;
        }
#else
        constexpr std::string_view signature = __PRETTY_FUNCTION__;
        constexpr auto start = signature.find("T = ") + 4;
        constexpr auto stop = signature.find_first_of(";]", start);
        return signature.substr(start, stop - start);
#endif
    }
}

using NativeResolver = void* (*)(lua_State*, int, std::string_view);
using SharedOwnerResolver = std::shared_ptr<void> (*)(lua_State*, int,
                                                      std::string_view);

LUAGLUE_API void* NativePointer(lua_State*, int,
                                std::string_view requestedType = {});
LUAGLUE_API std::shared_ptr<void> NativeSharedOwner(
    lua_State*, int, std::string_view requestedType = {});
LUAGLUE_API bool NativeIsConst(lua_State*, int);
LUAGLUE_API Table NativeTypeTable(lua_State*, int);
LUAGLUE_API Table NativeTypeTable(lua_State*, std::string_view);
LUAGLUE_API void RegisterExternalResolver(lua_State*, NativeResolver,
                                          SharedOwnerResolver);
LUAGLUE_API void RetainDependency(lua_State*, int dependent, int owner);
LUAGLUE_API void AttachSharedOwner(lua_State*, int,
                                   const std::shared_ptr<void>&);

namespace detail {
struct TypeRecord;
using NativeCast = void* (*)(void*) noexcept;
LUAGLUE_API Table RegisterClass(const Table&, std::string_view luaName,
                                std::string_view cppName, bool valueType);
LUAGLUE_API void RegisterBase(const Table&, std::string_view derived,
                              std::string_view base, NativeCast);
LUAGLUE_API void RegisterCast(const Table&, std::string_view derived,
                              std::string_view base, NativeCast);
LUAGLUE_API int PushNative(lua_State*, std::string_view cppName, void*,
                           std::shared_ptr<void>, bool isConst);
LUAGLUE_API TypeRecord* GetTypeRecord(lua_State*, int typeTable);
}  // namespace detail

template <typename T>
class Class : public Table {
public:
    using value_type = T;
    using Table::operator=;
    explicit Class(const Table& table) : Table(table) {}
};

template <typename T>
Class<T> BindClass(const Table& module, std::string_view name) {
    return Class<T>(detail::RegisterClass(module, name, TypeName<T>(), false));
}

template <typename Derived, typename Base>
void BindBase(const Class<Derived>& type) {
    static_assert(std::is_base_of_v<Base, Derived>);
    detail::RegisterBase(
        type, TypeName<Derived>(), TypeName<Base>(),
        [](void* object) noexcept -> void* {
            return static_cast<Base*>(static_cast<Derived*>(object));
        });
}

template <typename Derived, typename Base>
void BindCast(const Class<Derived>& type) {
    static_assert(std::is_base_of_v<Base, Derived>);
    detail::RegisterCast(
        type, TypeName<Derived>(), TypeName<Base>(),
        [](void* object) noexcept -> void* {
            return static_cast<Base*>(static_cast<Derived*>(object));
        });
}

template <typename T>
struct StructTraits {
    static constexpr bool enabled = false;
};

// Opt in only after the generator or binding author has verified that every
// stored member has independent C++ value semantics, including private state.
template <typename T>
struct IndependentValue {
    static constexpr bool enabled = std::is_copy_constructible_v<T>;
    static T DeepCopy(const T& value) {
        return T(value);
    }
};

}  // namespace lua_glue
