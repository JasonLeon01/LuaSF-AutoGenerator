#pragma once

#include "Codec.hpp"

#include <initializer_list>
#include <span>

namespace lua_glue {

enum class ReturnPolicy {
    Automatic,
    Copy,
    Move,
    Reference,
    ReferenceInternal
};
struct BindingOptions {
    ReturnPolicy return_policy = ReturnPolicy::Automatic;
    std::vector<std::pair<int, int>> keep_alive;
};

template <typename F>
struct DefaultFactory {
    F function;
};
template <typename F>
DefaultFactory(F) -> DefaultFactory<F>;
template <typename... T>
struct Defaults {
    std::tuple<T...> values;
    explicit Defaults(T... entries) : values(std::move(entries)...) {}
};
template <typename... T>
Defaults(T...) -> Defaults<T...>;

namespace detail {

struct ParameterRecord {
    std::string typeName;
    bool (*check)(lua_State*, int) = nullptr;
    int (*rank)(lua_State*, int) = nullptr;
};
struct DefaultRecord {
    std::shared_ptr<void> storage;
    int (*push)(lua_State*, const void*) = nullptr;
};
struct FunctionRecord {
    std::string name;
    std::string signature;
    std::string doc;
    std::shared_ptr<void> storage;
    int (*invoke)(lua_State*, void*, ReturnPolicy) = nullptr;
    std::vector<ParameterRecord> parameters;
    std::vector<DefaultRecord> defaults;
    BindingOptions options;
    bool variadic = false;
    int cachedReference = LUA_NOREF;
};

LUAGLUE_API void RegisterFunction(const Table&, std::string_view,
                                  std::shared_ptr<FunctionRecord>);
LUAGLUE_API void RegisterProperty(const Table&, std::string_view,
                                  std::shared_ptr<FunctionRecord>,
                                  std::shared_ptr<FunctionRecord>,
                                  bool isStatic);
LUAGLUE_API void RegisterMetamethod(const Table&, std::string_view,
                                    std::shared_ptr<FunctionRecord>);
LUAGLUE_API int PushFunctionValue(lua_State*, std::shared_ptr<FunctionRecord>);

template <typename T>
inline constexpr bool Injected =
    std::is_same_v<std::remove_cvref_t<T>, ThisState> ||
    std::is_same_v<std::remove_cvref_t<T>, StateView> ||
    std::is_same_v<std::remove_cvref_t<T>, Arguments>;

template <typename T>
struct CallableSignature : CallableSignature<decltype(&T::operator())> {};
template <typename R, typename... A>
struct CallableSignature<R (*)(A...)> {
    using Return = R;
    using ArgumentsTuple = std::tuple<A...>;
};
template <typename R, typename... A>
struct CallableSignature<R (*)(A...) noexcept>
    : CallableSignature<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct CallableSignature<R (C::*)(A...)> : CallableSignature<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct CallableSignature<R (C::*)(A...) const>
    : CallableSignature<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct CallableSignature<R (C::*)(A...) noexcept>
    : CallableSignature<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct CallableSignature<R (C::*)(A...) const noexcept>
    : CallableSignature<R (*)(A...)> {};

template <typename A>
struct ArgumentStorage {
    using U = std::remove_cvref_t<A>;
    static constexpr bool reference =
        std::is_lvalue_reference_v<A> && Codec<U>::native;
    using Value = std::conditional_t<
        reference, std::reference_wrapper<std::remove_reference_t<A>>, U>;
    Value value;
    ArgumentStorage(lua_State* state, int index) : value(load(state, index)) {}
    static Value load(lua_State* state, int index) {
        if constexpr (reference) {
            return std::ref(lua_glue::Read<A>(state, index));
        } else {
            return lua_glue::Read<U>(state, index);
        }
    }
    decltype(auto) get() {
        if constexpr (reference) {
            return value.get();
        } else if constexpr (std::is_lvalue_reference_v<A>) {
            return (value);
        } else {
            return std::move(value);
        }
    }
};
template <>
struct ArgumentStorage<ThisState> {
    ThisState value;
    ArgumentStorage(lua_State* state, int) : value(state) {}
    ThisState get() const {
        return value;
    }
};
template <>
struct ArgumentStorage<StateView> {
    StateView value;
    ArgumentStorage(lua_State* state, int) : value(state) {}
    StateView get() const {
        return value;
    }
};
template <>
struct ArgumentStorage<Arguments> {
    Arguments value;
    ArgumentStorage(lua_State* state, int first) : value(state, first) {}
    Arguments get() const {
        return value;
    }
};
template <>
struct ArgumentStorage<const Arguments&> : ArgumentStorage<Arguments> {
    using ArgumentStorage<Arguments>::ArgumentStorage;
};
template <>
struct ArgumentStorage<const StateView&> : ArgumentStorage<StateView> {
    using ArgumentStorage<StateView>::ArgumentStorage;
};

template <typename Tuple, std::size_t I>
constexpr int ArgumentIndex() {
    return []<std::size_t... J>(std::index_sequence<J...>) {
        return 1 +
               (0 + ... + (Injected<std::tuple_element_t<J, Tuple>> ? 0 : 1));
    }(std::make_index_sequence<I>{});
}

template <typename R>
int PushReturn(lua_State* state, R&& value, ReturnPolicy policy) {
    using U = std::remove_cvref_t<R>;
    if constexpr (IsTuple<U>::value) {
        int count = 0;
        std::apply(
            [&](auto&&... entries) {
                ((count += lua_glue::Push(
                      state, std::forward<decltype(entries)>(entries))),
                 ...);
            },
            std::forward<R>(value));
        return count;
    } else if constexpr (IsReference<U>::value) {
        return PushReturn<decltype(value.get())>(state, value.get(), policy);
    } else if constexpr (std::is_reference_v<R> && Codec<U>::native) {
        if (policy == ReturnPolicy::Copy) {
            if constexpr (std::is_copy_constructible_v<U>) {
                return Codec<U>::Push(state, value);
            } else {
                throw std::runtime_error(
                    "copy return policy requires a copyable type");
            }
        }
        if (policy == ReturnPolicy::Move) {
            if constexpr (std::is_constructible_v<U,
                                                  decltype(std::move(value))>) {
                return Codec<U>::Push(state, std::move(value));
            } else {
                throw std::runtime_error(
                    "move return policy requires a movable type");
            }
        }
        return lua_glue::Push(state, std::ref(value));
    } else if constexpr (std::is_pointer_v<U> &&
                         !std::is_function_v<std::remove_pointer_t<U>> &&
                         !std::is_void_v<std::remove_pointer_t<U>> &&
                         !std::is_same_v<
                             std::remove_cv_t<std::remove_pointer_t<U>>,
                             char>) {
        using Element = std::remove_cv_t<std::remove_pointer_t<U>>;
        if (value && policy == ReturnPolicy::Copy) {
            if constexpr (std::is_copy_constructible_v<Element>) {
                return Codec<Element>::Push(state, *value);
            } else {
                throw std::runtime_error(
                    "copy return policy requires a copyable type");
            }
        }
        if (value && policy == ReturnPolicy::Move) {
            if constexpr (std::is_constructible_v<Element, decltype(std::move(
                                                               *value))>) {
                return Codec<Element>::Push(state, std::move(*value));
            } else {
                throw std::runtime_error(
                    "move return policy requires a movable type");
            }
        }
        return lua_glue::Push(state, value);
    } else {
        return lua_glue::Push(state, std::forward<R>(value));
    }
}

template <typename F, typename R, typename Tuple, std::size_t... I>
int InvokeCallable(lua_State* state, F& function, ReturnPolicy policy,
                   std::index_sequence<I...>) {
    std::tuple<ArgumentStorage<std::tuple_element_t<I, Tuple>>...> arguments{
        ArgumentStorage<std::tuple_element_t<I, Tuple>>(
            state, ArgumentIndex<Tuple, I>())...};
    if constexpr (std::is_void_v<R>) {
        std::invoke(function, std::get<I>(arguments).get()...);
        return 0;
    } else {
        decltype(auto) result =
            std::invoke(function, std::get<I>(arguments).get()...);
        return PushReturn<R>(state, std::forward<R>(result), policy);
    }
}

template <typename A>
void AddParameter(FunctionRecord& record) {
    using U = std::remove_cvref_t<A>;
    if constexpr (std::is_same_v<U, Arguments>) {
        record.variadic = true;
    } else if constexpr (!Injected<A>) {
        std::string name;
        if constexpr (std::is_const_v<std::remove_reference_t<A>>) {
            name += "const ";
        }
        name += TypeName<A>();
        if constexpr (std::is_lvalue_reference_v<A>) {
            name += '&';
        }
        if constexpr (std::is_rvalue_reference_v<A>) {
            name += "&&";
        }
        record.parameters.push_back(
            {std::move(name),
             [](lua_State* state, int index) {
                 return lua_glue::Check<A>(state, index);
             },
             [](lua_State* state, int index) -> int {
                 if constexpr (std::is_same_v<U, Object>) {
                     return 100;
                 } else if constexpr (std::is_same_v<U, Table>) {
                     return 20;
                 } else if constexpr (std::is_integral_v<U> ||
                                      std::is_enum_v<U>) {
                     return lua_isinteger(state, index) ? 0 : 2;
                 } else if constexpr (std::is_floating_point_v<U>) {
                     return lua_isinteger(state, index) ? 1 : 0;
                 } else {
                     return 0;
                 }
             }});
    }
}

inline void AddOption(FunctionRecord& record, ReturnPolicy policy) {
    record.options.return_policy = policy;
}
inline void AddOption(FunctionRecord& record, BindingOptions options) {
    record.options = std::move(options);
}
inline void AddOption(FunctionRecord& record, std::string_view doc) {
    record.doc = doc;
}
inline void AddOption(FunctionRecord& record, const char* doc) {
    if (doc) {
        record.doc = doc;
    }
}
template <typename T>
struct IsDefaultFactory : std::false_type {};
template <typename F>
struct IsDefaultFactory<DefaultFactory<F>> : std::true_type {};
template <typename V>
void AddDefault(FunctionRecord& record, V value) {
    auto storage = std::make_shared<V>(std::move(value));
    record.defaults.push_back(
        {std::move(storage), [](lua_State* state, const void* raw) {
             const auto& value = *static_cast<const V*>(raw);
             if constexpr (IsDefaultFactory<V>::value) {
                 return lua_glue::Push(state, value.function());
             } else {
                 return lua_glue::Push(state, value);
             }
         }});
}
template <typename... V>
void AddOption(FunctionRecord& record, Defaults<V...> values) {
    std::apply(
        [&](auto&&... entries) {
            (AddDefault(record, std::move(entries)), ...);
        },
        std::move(values.values));
    if (record.defaults.size() > record.parameters.size()) {
        throw std::invalid_argument(
            "too many default values for binding signature");
    }
}

template <typename F, typename... Options>
std::shared_ptr<FunctionRecord> MakeFunction(F&& function,
                                             Options&&... options) {
    using Callable = std::decay_t<F>;
    using Signature = CallableSignature<Callable>;
    using R = typename Signature::Return;
    using Tuple = typename Signature::ArgumentsTuple;
    auto record = std::make_shared<FunctionRecord>();
    record->storage = std::make_shared<Callable>(std::forward<F>(function));
    record->invoke = [](lua_State* state, void* raw, ReturnPolicy policy) {
        return InvokeCallable<Callable, R, Tuple>(
            state, *static_cast<Callable*>(raw), policy,
            std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    };
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (AddParameter<std::tuple_element_t<I, Tuple>>(*record), ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    record->signature = "(";
    for (const auto& parameter : record->parameters) {
        if (record->signature.size() > 1) {
            record->signature += ", ";
        }
        record->signature += parameter.typeName;
    }
    if (record->variadic) {
        record->signature += ", ...";
    }
    record->signature += ") -> ";
    record->signature += TypeName<R>();
    (AddOption(*record, std::forward<Options>(options)), ...);
    return record;
}

template <typename F>
int PushCallable(lua_State* state, F&& function) {
    return PushFunctionValue(state, MakeFunction(std::forward<F>(function)));
}

}  // namespace detail

template <typename F, typename... Options>
void BindCallable(const Table& table, std::string_view name, F&& function,
                  Options&&... options) {
    detail::RegisterFunction(
        table, name,
        detail::MakeFunction(std::forward<F>(function),
                             std::forward<Options>(options)...));
}

template <typename R, typename... A, typename F, typename... Options>
void BindFunction(const Table& table, std::string_view name, F&& function,
                  Options&&... options) {
    BindCallable(
        table, name,
        [callable = std::forward<F>(function)](A... args) mutable -> R {
            return std::invoke(callable, std::forward<A>(args)...);
        },
        std::forward<Options>(options)...);
}

template <typename R, typename... A, typename C, typename... Options>
void BindMethod(const Class<C>& type, std::string_view name,
                R (C::*method)(A...), Options&&... options) {
    BindCallable(
        type, name,
        [method](C& self, A... args) -> R {
            return std::invoke(method, self, std::forward<A>(args)...);
        },
        std::forward<Options>(options)...);
}
template <typename R, typename... A, typename C, typename... Options>
void BindMethod(const Class<C>& type, std::string_view name,
                R (C::*method)(A...) const, Options&&... options) {
    BindCallable(
        type, name,
        [method](const C& self, A... args) -> R {
            return std::invoke(method, self, std::forward<A>(args)...);
        },
        std::forward<Options>(options)...);
}
template <typename R, typename... A, typename C, typename F,
          typename... Options>
    requires(!std::is_member_function_pointer_v<std::decay_t<F>>)
void BindMethod(const Class<C>& type, std::string_view name, F&& function,
                Options&&... options) {
    BindCallable(type, name, std::forward<F>(function),
                 std::forward<Options>(options)...);
}

template <typename T, typename... A, typename... Options>
void BindConstructor(const Class<T>& type, Options&&... options) {
    BindCallable(
        type, "new",
        [](A... args) {
            return std::make_shared<T>(std::forward<A>(args)...);
        },
        std::forward<Options>(options)...);
}
template <typename T, typename... A, typename F, typename... Options>
void BindFactory(const Class<T>& type, F&& factory, Options&&... options) {
    BindCallable(type, "new", std::forward<F>(factory),
                 std::forward<Options>(options)...);
}

template <typename Getter>
void BindProperty(const Table& type, std::string_view name, Getter&& getter) {
    detail::RegisterProperty(
        type, name,
        detail::MakeFunction(std::forward<Getter>(getter),
                             ReturnPolicy::ReferenceInternal),
        {}, false);
}
template <typename Getter, typename Setter>
void BindProperty(const Table& type, std::string_view name, Getter&& getter,
                  Setter&& setter) {
    detail::RegisterProperty(
        type, name,
        detail::MakeFunction(std::forward<Getter>(getter),
                             ReturnPolicy::ReferenceInternal),
        detail::MakeFunction(std::forward<Setter>(setter)), false);
}

template <typename Getter>
void BindStaticProperty(const Table& type, std::string_view name,
                        Getter&& getter) {
    detail::RegisterProperty(type, name,
                             detail::MakeFunction(std::forward<Getter>(getter)),
                             {}, true);
}
template <typename Getter, typename Setter>
void BindStaticProperty(const Table& type, std::string_view name,
                        Getter&& getter, Setter&& setter) {
    detail::RegisterProperty(
        type, name, detail::MakeFunction(std::forward<Getter>(getter)),
        detail::MakeFunction(std::forward<Setter>(setter)), true);
}

template <typename Field, typename C>
void BindAttr(const Class<C>& type, std::string_view name, Field C::* member) {
    auto getter = [member](ThisState state, Object object) -> Object {
        StackGuard stack(state.value);
        object.push(state.value);
        auto* self = static_cast<C*>(NativePointer(state, -1, TypeName<C>()));
        if (!self) {
            throw std::runtime_error("invalid property receiver");
        }
        using Value = std::remove_const_t<Field>;
        if constexpr (Codec<Value>::native) {
            if (NativeIsConst(state, -1)) {
                return MakeObject(state.value, std::cref(self->*member));
            }
            return MakeObject(state.value, std::ref(self->*member));
        } else {
            return MakeObject(state.value, self->*member);
        }
    };
    if constexpr (std::is_const_v<Field>) {
        BindProperty(type, name, std::move(getter));
    } else {
        BindProperty(type, name, std::move(getter),
                     [member](C& self, Field value) {
                         self.*member = std::move(value);
                     });
    }
}

template <typename Field>
void BindStaticAttr(const Table& type, std::string_view name, Field* member) {
    using Value = std::remove_const_t<Field>;
    auto getter = detail::MakeFunction([member]() -> decltype(auto) {
        if constexpr (Codec<Value>::native) {
            return std::ref(*member);
        } else {
            return Value(*member);
        }
    });
    std::shared_ptr<detail::FunctionRecord> setter;
    if constexpr (!std::is_const_v<Field>) {
        setter = detail::MakeFunction([member](Value value) {
            *member = std::move(value);
        });
    }
    detail::RegisterProperty(type, name, std::move(getter), std::move(setter),
                             true);
}

template <typename F, typename... Options>
void BindMetamethod(const Table& type, std::string_view name, F&& function,
                    Options&&... options) {
    detail::RegisterMetamethod(
        type, name,
        detail::MakeFunction(std::forward<F>(function),
                             std::forward<Options>(options)...));
}

template <typename T>
Class<T> BindStruct(const Table& module, std::string_view name) {
    static_assert(StructTraits<T>::enabled,
                  "BindStruct requires a verified complete independent-value "
                  "or custom deep-copy policy");
    auto type =
        Class<T>(detail::RegisterClass(module, name, TypeName<T>(), true));
    BindCallable(type, "copy", [](const T& value) {
        return T(value);
    });
    BindCallable(type, "__copy", [](const T& value) {
        return T(value);
    });
    BindCallable(type, "deepcopy", [](const T& value) {
        return StructTraits<T>::DeepCopy(value);
    });
    BindCallable(type, "__deepcopy", [](const T& value) {
        return StructTraits<T>::DeepCopy(value);
    });
    return type;
}

template <typename T>
Table BindEnum(const Table& module, std::string_view name,
               std::initializer_list<std::pair<std::string_view, T>> values) {
    Table result = module.get_or_create<Table>(name);
    for (const auto& [key, value] : values) {
        result.raw_set(key, value);
    }
    return result;
}

}  // namespace lua_glue
