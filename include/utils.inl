#pragma once

namespace lua_sf {

template <typename T> struct is_std_vector : std::false_type {};

template <typename T, typename Allocator>
struct is_std_vector<std::vector<T, Allocator>> : std::true_type {};

template <typename T>
inline constexpr bool is_std_vector_v =
    is_std_vector<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template <typename T> struct is_std_optional : std::false_type {};

template <typename T>
struct is_std_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_std_optional_v =
    is_std_optional<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template <typename T> T unwrapLuaNumeric(const LuaNumeric<T> &value) {
  if constexpr (is_lua_integral_v<T>)
    return value.value();
  else
    return value;
}

template <typename T> bool luaIntegerFits(lua_Integer value) {
  static_assert(is_lua_integral_v<T>);
  if constexpr (std::is_signed_v<T>) {
    if constexpr (std::numeric_limits<T>::digits >=
                  std::numeric_limits<lua_Integer>::digits) {
      return true;
    } else {
      return value >= static_cast<lua_Integer>(std::numeric_limits<T>::min()) &&
             value <= static_cast<lua_Integer>(std::numeric_limits<T>::max());
    }
  } else {
    if (value < 0)
      return false;
    using LuaUnsigned = std::make_unsigned_t<lua_Integer>;
    if constexpr (std::numeric_limits<T>::digits >=
                  std::numeric_limits<LuaUnsigned>::digits) {
      return true;
    } else {
      return static_cast<LuaUnsigned>(value) <=
             static_cast<LuaUnsigned>(std::numeric_limits<T>::max());
    }
  }
}

template <typename T>
bool tryReadLuaIntegral(lua_State *state, int index, T &value) {
  static_assert(is_lua_integral_v<T>);
  if (lua_type(state, index) != LUA_TNUMBER)
    return false;

  if (lua_isinteger(state, index)) {
    const lua_Integer integer = lua_tointeger(state, index);
    if (!luaIntegerFits<T>(integer))
      return false;

    value = static_cast<T>(integer);
    return true;
  }

  const lua_Number number = lua_tonumber(state, index);
  if (!std::isfinite(number) || std::trunc(number) != number)
    return false;

  constexpr int valueBits = std::numeric_limits<T>::digits;
  const lua_Number upperBound = std::ldexp(lua_Number{1}, valueBits);
  if constexpr (std::is_signed_v<T>) {
    if (number < -upperBound || number >= upperBound)
      return false;
  } else if (number < lua_Number{0} || number >= upperBound) {
    return false;
  }

  value = static_cast<T>(number);
  return true;
}

template <typename T, typename Handler>
bool sol_lua_check(sol::types<LuaIntegral<T>>, lua_State *state, int index,
                   Handler &&handler, sol::stack::record &tracking) {
  tracking.use(1);
  T value{};
  const bool success = tryReadLuaIntegral(state, index, value);
  if (!success) {
    handler(state, index, sol::type::number, sol::type_of(state, index),
            "expected a finite, in-range integer value");
  }
  return success;
}

template <typename T>
LuaIntegral<T> sol_lua_get(sol::types<LuaIntegral<T>>, lua_State *state,
                           int index, sol::stack::record &tracking) {
  tracking.use(1);
  T value{};
  if (!tryReadLuaIntegral(state, index, value))
    luaL_error(state, "expected a finite, in-range integer value");
  return LuaIntegral<T>(value);
}

inline bool is_nil_object(const sol::object &object) {
  return !object.valid() || object == LUASF_SOL_NIL;
}

inline void throw_on_lua_error(const sol::protected_function_result &result) {
  if (result.valid())
    return;

  const sol::error error = result;
  throw std::runtime_error(error.what());
}

inline LuaRegistryReference
makeLuaRegistryReference(const sol::object &object) {
  lua_State *state = object.lua_state();
  detail::registerLuaThreadForRegistryReference(state);
  LuaStateExecutionScope execution(state);
  if (!execution.active())
    throw std::runtime_error("Lua state is stopping");
  auto pushed = sol::stack::push_pop(object);
  return LuaRegistryReference(state, pushed.index_of(object));
}

template <typename Callback>
decltype(auto) withLuaRegistryCallback(const LuaRegistryReference &reference,
                                       Callback &&callback) {
  lua_State *state = reference.state();
  LuaStateExecutionScope execution(state);
  if (!execution.active())
    throw std::runtime_error("Lua state is stopping");
  if (!reference.pushUnderExecutionScope())
    throw std::runtime_error("Lua callback is no longer available");
  auto popper = sol::stack::pop_n(state, 1);
  sol::protected_function function =
      sol::stack::get<sol::protected_function>(state, -1);
  return std::forward<Callback>(callback)(function, sol::state_view(state));
}

inline std::unordered_map<const void *, LongLivedMemoryBuffer> &
longLivedMemoryStore() {
  static std::unordered_map<const void *, LongLivedMemoryBuffer> store;
  return store;
}

inline std::mutex &longLivedMemoryStoreMutex() {
  static std::mutex mutex;
  return mutex;
}

inline LongLivedMemoryBuffer
makeLongLivedMemoryBuffer(const sol::object &object) {
  return std::make_shared<std::vector<std::byte>>(
      array_from_object<std::byte>(object));
}

inline void rememberLongLivedMemory(const void *owner,
                                    LongLivedMemoryBuffer buffer) {
  if (!owner)
    return;

  std::lock_guard<std::mutex> lock(longLivedMemoryStoreMutex());
  if (buffer)
    longLivedMemoryStore()[owner] = std::move(buffer);
  else
    longLivedMemoryStore().erase(owner);
}

inline void releaseLongLivedMemory(const void *owner) {
  if (!owner)
    return;

  std::lock_guard<std::mutex> lock(longLivedMemoryStoreMutex());
  longLivedMemoryStore().erase(owner);
}

inline void rememberLongLivedStream(const void *owner,
                                    const sol::object &stream) {
  if (!owner)
    return;
  if (!stream.valid()) {
    detail::releaseLuaRegistryReference(owner);
    return;
  }
  detail::retainLuaRegistryReference(owner, makeLuaRegistryReference(stream));
}

inline void releaseLongLivedStream(const void *owner) {
  if (!owner)
    return;

  detail::releaseLuaRegistryReference(owner);
}

inline void releaseLongLivedResources(const void *owner) {
  releaseLongLivedMemory(owner);
  releaseLongLivedStream(owner);
}

template <typename T>
void rememberLongLivedMemory(const T &owner, LongLivedMemoryBuffer buffer) {
  rememberLongLivedMemory(static_cast<const void *>(&owner), std::move(buffer));
}

template <typename T> void releaseLongLivedMemory(const T &owner) {
  releaseLongLivedMemory(static_cast<const void *>(&owner));
}

template <typename T>
void rememberLongLivedStream(const T &owner, const sol::object &stream) {
  rememberLongLivedStream(static_cast<const void *>(&owner), stream);
}

template <typename T> void releaseLongLivedStream(const T &owner) {
  releaseLongLivedStream(static_cast<const void *>(&owner));
}

template <typename T> void releaseLongLivedResources(const T &owner) {
  releaseLongLivedResources(static_cast<const void *>(&owner));
}

template <typename T, typename... Args>
std::shared_ptr<T> makeLongLivedMemoryObject(Args &&...args) {
  return std::shared_ptr<T>(new T(std::forward<Args>(args)...), [](T *object) {
    releaseLongLivedResources(static_cast<const void *>(object));
    delete object;
  });
}

template <typename T> T object_as(const sol::object &object) {
  using U = std::remove_cv_t<std::remove_reference_t<T>>;
  if constexpr (std::is_same_v<U, sf::String>) {
    return to_sf_string(object.as<std::string>());
  } else if constexpr (std::is_same_v<U, std::wstring>) {
    return to_sf_string(object.as<std::string>()).toWideString();
  } else if constexpr (std::is_same_v<U, std::filesystem::path>) {
    return std::filesystem::path(object.as<std::string>());
#if defined(LUASF_IOS_COMPAT)
  } else if constexpr (std::is_same_v<U, std::filesystem::file_time_type>) {
    using Duration = typename U::duration;
    using Rep = typename U::duration::rep;
    return U(Duration(static_cast<Rep>(object.as<lua_Integer>())));
#endif
  } else if constexpr (is_lua_integral_v<U>) {
    return object.as<LuaIntegral<U>>().value();
  } else if constexpr (std::is_same_v<U, std::byte>) {
    return static_cast<std::byte>(
        object.as<LuaIntegral<unsigned int>>().value());
  } else if constexpr (is_std_vector_v<U>) {
    return array_from_object<typename U::value_type>(object);
  } else if constexpr (is_std_optional_v<U>) {
    return optional_from_object<typename U::value_type>(object);
  } else {
    return object.as<U>();
  }
}

template <typename T>
std::vector<T> array_from_object(const sol::object &object) {
  if (object.get_type() == sol::type::table) {
    const sol::table table = object.as<sol::table>();
    std::vector<T> values;
    values.reserve(table.size());

    if constexpr (is_byte_like_v<T>) {
      for (const auto &entry : table)
        values.push_back(static_cast<T>(
            object_as<unsigned int>(entry.second.as<sol::object>())));
      return values;
    }

    for (const auto &entry : table) {
      const sol::object value = entry.second;
      values.push_back(object_as<T>(value));
    }
    return values;
  }

  if constexpr (is_byte_like_v<T>) {
    if (object.get_type() == sol::type::string) {
      const auto data = object.as<std::string>();
      std::vector<T> values;
      values.reserve(data.size());
      for (unsigned char byte : data)
        values.push_back(static_cast<T>(byte));
      return values;
    }
  }

  throw std::runtime_error(
      "expected Lua table or byte string for array conversion");
}

template <typename T>
std::optional<T> optional_from_object(const sol::object &object) {
  if (!object.valid() || object == LUASF_SOL_NIL)
    return std::nullopt;
  return object_as<T>(object);
}

template <typename T>
sol::object as_lua_object(sol::state_view lua, const T &value) {
  using U = std::remove_cv_t<std::remove_reference_t<T>>;
  if constexpr (std::is_same_v<U, sf::String>) {
    return sol::make_object(lua, to_utf8_string(value));
  } else if constexpr (std::is_same_v<U, std::wstring>) {
    return sol::make_object(lua, to_utf8_string(sf::String(value)));
  } else if constexpr (std::is_same_v<U, std::filesystem::path>) {
    return sol::make_object(lua, value.string());
#if defined(LUASF_IOS_COMPAT)
  } else if constexpr (std::is_same_v<U, std::filesystem::file_time_type>) {
    return sol::make_object(
        lua, static_cast<lua_Integer>(value.time_since_epoch().count()));
#endif
  } else if constexpr (is_std_vector_v<U>) {
    return vector_to_object(lua, value);
  } else if constexpr (is_std_optional_v<U>) {
    return optional_to_object(lua, value);
  } else {
    return sol::make_object(lua, value);
  }
}

template <typename T, typename Allocator>
sol::object vector_to_object(sol::state_view lua,
                             const std::vector<T, Allocator> &values) {
  sol::table table = lua.create_table(static_cast<int>(values.size()), 0);
  int index = 1;
  for (const auto &value : values)
    table[index++] = as_lua_object(lua, value);
  return sol::make_object(lua, table);
}

template <typename T>
sol::object optional_to_object(sol::state_view lua,
                               const std::optional<T> &value) {
  if (!value)
    return sol::make_object(lua, LUASF_SOL_NIL);

  return as_lua_object(lua, *value);
}

template <typename T>
sol::object optional_to_object(sol::state_view lua, std::optional<T> &&value) {
  if (!value)
    return sol::make_object(lua, LUASF_SOL_NIL);

  using U = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<U, sf::String>) {
    return sol::make_object(lua, to_utf8_string(*value));
  } else if constexpr (std::is_same_v<U, std::filesystem::path>) {
    return sol::make_object(lua, value->string());
  } else if constexpr (is_std_vector_v<U>) {
    return vector_to_object(lua, *value);
  } else {
    return sol::make_object(lua, std::move(*value));
  }
}

template <typename T, typename Allocator>
sol::object
optional_to_object(sol::state_view lua,
                   const std::optional<std::vector<T, Allocator>> &value) {
  if (!value)
    return sol::make_object(lua, LUASF_SOL_NIL);
  return vector_to_object(lua, *value);
}

template <typename T, typename Allocator>
sol::object
optional_to_object(sol::state_view lua,
                   std::optional<std::vector<T, Allocator>> &&value) {
  if (!value)
    return sol::make_object(lua, LUASF_SOL_NIL);
  return vector_to_object(lua, *value);
}

namespace detail {

template <typename Element>
inline constexpr bool hasExactShaderUniformArrayOverload = requires {
  static_cast<void (sf::Shader::*)(const std::string &, const Element *,
                                   std::size_t)>(
      &sf::Shader::setUniformArray);
};

template <typename... Elements> struct AreUniqueTypes : std::true_type {};

template <typename First, typename... Rest>
struct AreUniqueTypes<First, Rest...>
    : std::bool_constant<((!std::is_same_v<First, Rest>) && ...) &&
                         AreUniqueTypes<Rest...>::value> {};

template <typename... Elements>
inline constexpr bool areUniqueTypes = AreUniqueTypes<Elements...>::value;

template <typename Element>
bool shaderUniformArrayElementMatches(const sol::object &first) {
  if constexpr (std::is_floating_point_v<Element>)
    return first.get_type() == sol::type::number;
  else
    return first.is<Element>();
}

template <typename Element>
std::vector<Element>
shaderUniformArrayFromObject(const sol::object &values,
                             const std::string &errorLabel) {
  if (values.get_type() != sol::type::table)
    throw std::runtime_error(errorLabel + " expects a Lua array");

  const sol::table table = values.as<sol::table>();
  const std::size_t length = table.size();
  std::size_t entryCount = 0;
  for ([[maybe_unused]] const auto &entry : table)
    ++entryCount;
  if (entryCount != length)
    throw std::runtime_error(errorLabel +
                             " expects a dense 1-based Lua array");

  std::vector<Element> buffer;
  buffer.reserve(length);
  for (std::size_t index = 1; index <= length; ++index) {
    const sol::object value = table[index];
    if (is_nil_object(value))
      throw std::runtime_error(errorLabel +
                               " expects a dense 1-based Lua array");
    if (!shaderUniformArrayElementMatches<Element>(value))
      throw std::runtime_error(errorLabel + " element " +
                               std::to_string(index) +
                               " has an incompatible type");
    buffer.push_back(object_as<Element>(value));
  }
  return buffer;
}

template <typename Element>
auto shaderUniformArraySetter(std::string errorLabel) {
  static_assert(
      hasExactShaderUniformArrayOverload<Element>,
      "ShaderUniformArrayVariant element must exactly match an "
      "sf::Shader::setUniformArray pointer-element overload");

  return [errorLabel = std::move(errorLabel)](
             sf::Shader &self, std::string name, sol::object values) {
    auto buffer = shaderUniformArrayFromObject<Element>(values, errorLabel);
    if (buffer.empty())
      return;

    self.setUniformArray(name, buffer.data(), buffer.size());
  };
}

template <typename Element>
bool trySetInferredShaderUniformArray(
    const ShaderUniformArrayVariant<Element> &, sf::Shader &self,
    const std::string &name, const sol::object &values,
    const sol::object &first, const std::string &errorLabel) {
  if (!shaderUniformArrayElementMatches<Element>(first))
    return false;

  shaderUniformArraySetter<Element>(errorLabel)(self, name, values);
  return true;
}

inline std::string shaderUniformArrayStubSignature(std::string_view luaOwner,
                                                   std::string_view luaType) {
  std::string signature = "fun(self: ";
  signature.append(luaOwner);
  signature.append(", name: string, values: ");
  signature.append(luaType);
  signature.push_back(')');
  return signature;
}

template <typename... Elements>
std::string shaderUniformArrayTypedMethodHint(
    const ShaderUniformArrayVariant<Elements> &...variants) {
  const std::array<std::string_view, sizeof...(Elements)> methodNames = {
      variants.methodName...};
  std::string hint;
  for (std::size_t index = 0; index < methodNames.size(); ++index) {
    if (index > 0) {
      if (index + 1 == methodNames.size())
        hint.append(methodNames.size() == 2 ? " or " : ", or ");
      else
        hint.append(", ");
    }
    hint.append(methodNames[index]);
  }
  return hint;
}

template <typename... Elements>
void validateShaderUniformArrayVariants(
    std::string_view luaOwner, std::string_view inferredMethod,
    const ShaderUniformArrayVariant<Elements> &...variants) {
  static_assert(sizeof...(Elements) > 0,
                "bindShaderUniformArrays requires at least one variant");
  static_assert(areUniqueTypes<Elements...>,
                "bindShaderUniformArrays requires unique element types");
  static_assert((hasExactShaderUniformArrayOverload<Elements> && ...),
                "ShaderUniformArrayVariant element does not exactly match an "
                "sf::Shader::setUniformArray pointer-element overload");

  if (luaOwner.empty())
    throw std::invalid_argument("Shader uniform-array Lua owner is empty");
  if (inferredMethod.empty())
    throw std::invalid_argument(
        "Shader uniform-array inferred method name is empty");

  const std::array<std::string_view, sizeof...(Elements)> methodNames = {
      variants.methodName...};
  const std::array<std::string_view, sizeof...(Elements)> luaArrayTypes = {
      variants.luaArrayType...};

  for (std::size_t index = 0; index < methodNames.size(); ++index) {
    if (methodNames[index].empty())
      throw std::invalid_argument(
          "Shader uniform-array typed method name is empty");
    if (luaArrayTypes[index].empty())
      throw std::invalid_argument(
          "Shader uniform-array Lua array type is empty");
    if (methodNames[index] == inferredMethod)
      throw std::invalid_argument(
          "Shader uniform-array typed and inferred method names overlap");
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (methodNames[index] == methodNames[previous])
        throw std::invalid_argument(
            "Shader uniform-array typed method names must be unique");
    }
  }
}

template <typename Element>
ShaderUniformArrayVariant<Element>
shaderUniformArrayVariant(std::string methodName, std::string luaArrayType) {
  static_assert(
      hasExactShaderUniformArrayOverload<Element>,
      "ShaderUniformArrayVariant element must exactly match an "
      "sf::Shader::setUniformArray pointer-element overload");
  static_assert(std::is_same_v<Element, std::remove_cv_t<Element>>,
                "ShaderUniformArrayVariant element must be unqualified");

  return {std::move(methodName), std::move(luaArrayType)};
}

template <typename Usertype, typename... Elements>
void bindShaderUniformArrays(
    Usertype &usertype, std::string_view luaOwner,
    std::string_view inferredMethod,
    ShaderUniformArrayVariant<Elements>... variants) {
  validateShaderUniformArrayVariants(luaOwner, inferredMethod, variants...);

  const std::string owner(luaOwner);
  const std::string inferredName(inferredMethod);
  bool firstStub = true;
  const auto addInferredStub = [&](const auto &variant) {
    const std::string signature =
        shaderUniformArrayStubSignature(owner, variant.luaArrayType);
    if (firstStub) {
      stub::function(owner.c_str(), inferredName.c_str(), signature.c_str());
      firstStub = false;
    } else {
      stub::overload(owner.c_str(), inferredName.c_str(), signature.c_str());
    }
  };
  (addInferredStub(variants), ...);

  const auto addTypedStub = [&](const auto &variant) {
    const std::string signature =
        shaderUniformArrayStubSignature(owner, variant.luaArrayType);
    stub::function(owner.c_str(), variant.methodName.c_str(),
                   signature.c_str());
  };
  (addTypedStub(variants), ...);

  (usertype.set_function(
       variants.methodName,
       shaderUniformArraySetter<Elements>(owner + "." + variants.methodName)),
   ...);

  const std::string errorLabel = owner + "." + inferredName;
  const std::string typedMethodHint =
      shaderUniformArrayTypedMethodHint(variants...);
  auto variantTuple = std::make_tuple(std::move(variants)...);
  usertype.set_function(
      inferredName,
      [variants = std::move(variantTuple), errorLabel,
       typedMethodHint](sf::Shader &self, std::string name,
                        sol::object values) {
        if (values.get_type() != sol::type::table)
          throw std::runtime_error(errorLabel + " expects a Lua array");

        const sol::table table = values.as<sol::table>();
        const sol::object first = table[1];
        if (is_nil_object(first))
          throw std::runtime_error(errorLabel +
                                   " cannot infer an empty array; use " +
                                   typedMethodHint);

        const bool handled = std::apply(
            [&](const auto &...variant) {
              return (trySetInferredShaderUniformArray(
                          variant, self, name, values, first, errorLabel) ||
                      ...);
            },
            variants);
        if (!handled)
          throw std::runtime_error(errorLabel +
                                   " received an unsupported array element "
                                   "type");
      });
}

} // namespace detail

template <typename T>
sol::object callback_argument_to_object(sol::state_view lua, T &&value) {
  using Argument = T;
  using U = std::remove_cv_t<std::remove_reference_t<Argument>>;
  static_assert(!std::is_rvalue_reference_v<Argument>,
                "generic Lua callbacks do not support rvalue references");
  static_assert(!std::is_pointer_v<U>,
                "generic Lua callbacks require a codec for pointer arguments");
  static_assert(!(std::is_lvalue_reference_v<Argument> &&
                  !std::is_const_v<std::remove_reference_t<Argument>> &&
                  !std::is_class_v<U>),
                "generic Lua callbacks require a codec for mutable scalar "
                "references");

  if constexpr (std::is_lvalue_reference_v<Argument> && std::is_class_v<U> &&
                !std::is_copy_constructible_v<U>) {
    if constexpr (std::is_const_v<std::remove_reference_t<Argument>>)
      return sol::make_object(lua, std::cref(value));
    else
      return sol::make_object(lua, std::ref(value));
  } else {
    return as_lua_object(lua, value);
  }
}

template <typename Signature> struct function_converter;

template <typename R, typename... Args> struct function_converter<R(Args...)> {
  static std::function<R(Args...)> from_object(const sol::object &object,
                                               std::string label) {
    static_assert((!std::is_rvalue_reference_v<Args> && ...),
                  "generic Lua callbacks require a codec for rvalue-reference "
                  "arguments");
    static_assert(
        (!std::is_pointer_v<std::remove_reference_t<Args>> && ...),
        "generic Lua callbacks require a codec for pointer arguments");
    static_assert(
        std::is_void_v<R> || (!std::is_pointer_v<std::remove_reference_t<R>> &&
                              !std::is_reference_v<R>),
        "generic Lua callbacks require a codec for pointer or reference "
        "returns");

    const LuaRegistryReference callback = makeLuaRegistryReference(object);
    return [callback, label = std::move(label)](Args... args) -> R {
      try {
        return withLuaRegistryCallback(
            callback,
            [&](sol::protected_function &function, sol::state_view lua) -> R {
              sol::protected_function_result result =
                  function(callback_argument_to_object(
                      lua, std::forward<Args>(args))...);
              throw_on_lua_error(result);
              if constexpr (!std::is_void_v<R>) {
                const sol::object returned = result;
                return object_as<R>(returned);
              }
            });
      } catch (const std::exception &error) {
        if (label.empty())
          throw;
        throw std::runtime_error(label + ": " + error.what());
      }
    };
  }
};

template <typename Signature> struct native_thread_function_converter;

template <typename... Args>
struct native_thread_function_converter<void(Args...)> {
  static std::function<void(Args...)> from_object(const sol::object &object) {
    if (is_nil_object(object))
      return {};

    const LuaRegistryReference callback = makeLuaRegistryReference(object);
    return [callback](Args... args) noexcept {
      try {
        withLuaRegistryCallback(
            callback, [&](sol::protected_function &function, sol::state_view) {
              sol::protected_function_result result =
                  function(std::forward<Args>(args)...);
              throw_on_lua_error(result);
            });
      } catch (...) {
        return;
      }
    };
  }
};

template <typename Signature>
std::function<Signature>
function_from_object_at_native_thread_boundary(const sol::object &object) {
  return native_thread_function_converter<Signature>::from_object(object);
}

namespace callback::detail {

template <typename R, typename... Args>
struct FromObject<std::function<R(Args...)>, GenericCallbackCodec> {
  static std::function<R(Args...)> read(const sol::object &object,
                                        CallbackOptions options) {
    return function_converter<R(Args...)>::from_object(
        object, std::move(options.label));
  }
};

} // namespace callback::detail

template <typename Signature>
std::function<Signature> function_from_object(const sol::object &object) {
  return callback::from_object<std::function<Signature>,
                               callback::GenericCallbackCodec>(
      object, callback::CallbackOptions{"Lua callback", false});
}

} // namespace lua_sf
