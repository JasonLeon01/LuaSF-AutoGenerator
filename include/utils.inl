#pragma once

namespace lua_sf {

template <typename T> struct is_std_vector : std::false_type {};

template <typename T, typename Allocator>
struct is_std_vector<std::vector<T, Allocator>> : std::true_type {};

template <typename T>
inline constexpr bool is_std_vector_v =
    is_std_vector<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template <typename T>
T unwrapLuaNumeric(const LuaNumeric<T> &value) {
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

inline std::unordered_map<const void *, LongLivedMemoryBuffer> &
longLivedMemoryStore() {
  static std::unordered_map<const void *, LongLivedMemoryBuffer> store;
  return store;
}

inline std::mutex &longLivedMemoryStoreMutex() {
  static std::mutex mutex;
  return mutex;
}

inline std::unordered_map<const void *, LongLivedStreamObject> &
longLivedStreamStore() {
  static std::unordered_map<const void *, LongLivedStreamObject> store;
  return store;
}

inline std::mutex &longLivedStreamStoreMutex() {
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
                                    LongLivedStreamObject stream) {
  if (!owner)
    return;

  std::lock_guard<std::mutex> lock(longLivedStreamStoreMutex());
  if (stream.valid())
    longLivedStreamStore().insert_or_assign(owner, std::move(stream));
  else
    longLivedStreamStore().erase(owner);
}

inline void releaseLongLivedStream(const void *owner) {
  if (!owner)
    return;

  std::lock_guard<std::mutex> lock(longLivedStreamStoreMutex());
  longLivedStreamStore().erase(owner);
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
void rememberLongLivedStream(const T &owner, LongLivedStreamObject stream) {
  rememberLongLivedStream(static_cast<const void *>(&owner), std::move(stream));
}

template <typename T> void releaseLongLivedStream(const T &owner) {
  releaseLongLivedStream(static_cast<const void *>(&owner));
}

template <typename T> void releaseLongLivedResources(const T &owner) {
  releaseLongLivedResources(static_cast<const void *>(&owner));
}

template <typename T>
void LongLivedMemoryDeleter<T>::operator()(T *object) const noexcept {
  releaseLongLivedResources(static_cast<const void *>(object));
  delete object;
}

template <typename T, typename... Args>
std::unique_ptr<T, LongLivedMemoryDeleter<T>>
makeLongLivedMemoryObject(Args &&...args) {
  return std::unique_ptr<T, LongLivedMemoryDeleter<T>>(
      new T(std::forward<Args>(args)...));
}

template <typename T> T object_as(const sol::object &object) {
  using U = std::remove_cv_t<std::remove_reference_t<T>>;
  if constexpr (std::is_same_v<U, sf::String>) {
    return to_sf_string(object.as<std::string>());
  } else if constexpr (std::is_same_v<U, std::filesystem::path>) {
    return std::filesystem::path(object.as<std::string>());
  } else if constexpr (is_lua_integral_v<U>) {
    return object.as<LuaIntegral<U>>().value();
  } else if constexpr (std::is_same_v<U, std::byte>) {
    return static_cast<std::byte>(
        object.as<LuaIntegral<unsigned int>>().value());
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
  } else if constexpr (std::is_same_v<U, std::filesystem::path>) {
    return sol::make_object(lua, value.string());
  } else if constexpr (is_std_vector_v<U>) {
    return vector_to_object(lua, value);
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

inline sol::table audioFramesToTable(sol::state_view lua, const float *frames,
                                     unsigned int frameCount,
                                     unsigned int frameChannelCount) {
  sol::table result = lua.create_table(static_cast<int>(frameCount), 0);
  if (!frames || frameCount == 0 || frameChannelCount == 0)
    return result;

  for (unsigned int frame = 0; frame < frameCount; ++frame) {
    sol::table row = lua.create_table(static_cast<int>(frameChannelCount), 0);
    for (unsigned int channel = 0; channel < frameChannelCount; ++channel)
      row[static_cast<int>(channel + 1)] =
          frames[static_cast<std::size_t>(frame) * frameChannelCount + channel];
    result[static_cast<int>(frame + 1)] = row;
  }

  return result;
}

inline void copyAudioFramesFromObject(const sol::object &object, float *frames,
                                      unsigned int frameCount,
                                      unsigned int frameChannelCount) {
  if (!frames || frameCount == 0 || frameChannelCount == 0 ||
      is_nil_object(object))
    return;

  const std::size_t maxSampleCount =
      static_cast<std::size_t>(frameCount) * frameChannelCount;
  if (object.get_type() != sol::type::table) {
    const auto values = object.as<std::vector<float>>();
    const std::size_t copyCount = std::min(values.size(), maxSampleCount);
    std::copy_n(values.begin(), copyCount, frames);
    return;
  }

  const sol::table table = object.as<sol::table>();
  const sol::object firstValue = table[1];
  if (firstValue.get_type() != sol::type::table) {
    const auto values = table.as<std::vector<float>>();
    const std::size_t copyCount = std::min(values.size(), maxSampleCount);
    std::copy_n(values.begin(), copyCount, frames);
    return;
  }

  for (unsigned int frame = 0; frame < frameCount; ++frame) {
    const sol::object rowValue = table[static_cast<int>(frame + 1)];
    if (is_nil_object(rowValue))
      break;

    const sol::table row = rowValue.as<sol::table>();
    for (unsigned int channel = 0; channel < frameChannelCount; ++channel) {
      const sol::object sampleValue = row[static_cast<int>(channel + 1)];
      if (!is_nil_object(sampleValue))
        frames[static_cast<std::size_t>(frame) * frameChannelCount + channel] =
            sampleValue.as<float>();
    }
  }
}

inline void updateAudioFrameCount(const sol::object &object,
                                  unsigned int &frameCount,
                                  unsigned int frameCapacity) {
  if (is_nil_object(object))
    return;

  frameCount = std::min(object_as<unsigned int>(object), frameCapacity);
}

template <typename Signature> struct function_converter;

template <typename R, typename... Args> struct function_converter<R(Args...)> {
  static std::function<R(Args...)> from_object(const sol::object &object) {
    if (is_nil_object(object))
      return {};

    sol::protected_function callback = object.as<sol::protected_function>();
    return [callback = std::move(callback)](Args... args) mutable -> R {
      sol::protected_function_result result =
          callback(std::forward<Args>(args)...);
      throw_on_lua_error(result);
      if constexpr (!std::is_void_v<R>)
        return result.get<R>();
    };
  }
};

template <>
struct function_converter<void(const float *, unsigned int &, float *,
                               unsigned int &, unsigned int)> {
  static std::function<void(const float *, unsigned int &, float *,
                            unsigned int &, unsigned int)>
  from_object(const sol::object &object) {
    if (is_nil_object(object))
      return {};

    sol::protected_function callback = object.as<sol::protected_function>();
    return [callback = std::move(callback)](
               const float *inputFrames, unsigned int &inputFrameCount,
               float *outputFrames, unsigned int &outputFrameCount,
               unsigned int frameChannelCount) mutable {
      sol::state_view lua(callback.lua_state());
      const unsigned int inputFrameCapacity = inputFrameCount;
      const unsigned int outputFrameCapacity = outputFrameCount;
      sol::table input = audioFramesToTable(lua, inputFrames, inputFrameCount,
                                            frameChannelCount);
      sol::table output = audioFramesToTable(
          lua, outputFrames, outputFrameCount, frameChannelCount);

      sol::protected_function_result result = callback(
          input, inputFrameCount, output, outputFrameCount, frameChannelCount);
      throw_on_lua_error(result);

      sol::object outputValue = output;
      const sol::object returned = result;
      if (!is_nil_object(returned)) {
        if (returned.get_type() == sol::type::table) {
          const sol::table table = returned.as<sol::table>();
          const sol::object inputCountValue = table["inputFrameCount"];
          const sol::object outputCountValue = table["outputFrameCount"];
          const sol::object returnedOutputValue = table["output"];
          updateAudioFrameCount(inputCountValue, inputFrameCount,
                                inputFrameCapacity);
          updateAudioFrameCount(outputCountValue, outputFrameCount,
                                outputFrameCapacity);

          const bool hasNamedReturn = !is_nil_object(inputCountValue) ||
                                      !is_nil_object(outputCountValue) ||
                                      !is_nil_object(returnedOutputValue);
          if (!is_nil_object(returnedOutputValue))
            outputValue = returnedOutputValue;
          else if (!hasNamedReturn)
            outputValue = returned;
        } else if (returned.get_type() == sol::type::number) {
          updateAudioFrameCount(returned, outputFrameCount,
                                outputFrameCapacity);
        }
      }

      copyAudioFramesFromObject(outputValue, outputFrames, outputFrameCount,
                                frameChannelCount);
    };
  }
};

template <>
struct function_converter<void(const sf::Text::ShapedGlyph &, std::uint32_t &,
                               sf::Color &, sf::Color &, float &)> {
  static std::function<void(const sf::Text::ShapedGlyph &, std::uint32_t &,
                            sf::Color &, sf::Color &, float &)>
  from_object(const sol::object &object) {
    if (is_nil_object(object))
      return {};

    sol::protected_function callback = object.as<sol::protected_function>();
    return [callback = std::move(callback)](
               const sf::Text::ShapedGlyph &shapedGlyph, std::uint32_t &style,
               sf::Color &fillColor, sf::Color &outlineColor,
               float &outlineThickness) mutable {
      sol::protected_function_result result =
          callback(std::ref(shapedGlyph), style, std::ref(fillColor),
                   std::ref(outlineColor), outlineThickness);
      throw_on_lua_error(result);

      const sol::object returned = result;
      if (is_nil_object(returned) || returned.get_type() != sol::type::table)
        return;

      const sol::table table = returned.as<sol::table>();
      const sol::object styleValue = table["style"];
      const sol::object fillValue = table["fillColor"];
      const sol::object outlineValue = table["outlineColor"];
      const sol::object thicknessValue = table["outlineThickness"];
      if (!is_nil_object(styleValue))
        style = styleValue.as<std::uint32_t>();
      if (!is_nil_object(fillValue))
        fillColor = fillValue.as<sf::Color>();
      if (!is_nil_object(outlineValue))
        outlineColor = outlineValue.as<sf::Color>();
      if (!is_nil_object(thicknessValue))
        outlineThickness = thicknessValue.as<float>();
    };
  }
};

template <> struct function_converter<bool(const void *, std::size_t)> {
  static std::function<bool(const void *, std::size_t)>
  from_object(const sol::object &object) {
    if (is_nil_object(object))
      return {};

    sol::protected_function callback = object.as<sol::protected_function>();
    return [callback = std::move(callback)](const void *data,
                                            std::size_t size) mutable {
      const auto *bytes = static_cast<const char *>(data);
      const std::string buffer =
          bytes && size != 0 ? std::string(bytes, bytes + size) : std::string{};
      sol::protected_function_result result = callback(buffer, size);
      throw_on_lua_error(result);
      return result.get<bool>();
    };
  }
};

template <> struct function_converter<bool(void *, std::size_t &)> {
  static std::function<bool(void *, std::size_t &)>
  from_object(const sol::object &object) {
    if (is_nil_object(object))
      return {};

    sol::protected_function callback = object.as<sol::protected_function>();
    return [callback = std::move(callback)](void *data,
                                            std::size_t &size) mutable {
      sol::protected_function_result result = callback(size);
      throw_on_lua_error(result);

      const sol::object returned = result;
      if (is_nil_object(returned)) {
        size = 0;
        return false;
      }

      bool keepGoing = true;
      sol::object dataValue = returned;
      if (returned.get_type() == sol::type::table) {
        const sol::table table = returned.as<sol::table>();
        const sol::object keepGoingValue = table["keepGoing"];
        if (!is_nil_object(keepGoingValue))
          keepGoing = keepGoingValue.as<bool>();
        dataValue = table["data"];
      } else if (returned.get_type() == sol::type::boolean) {
        keepGoing = returned.as<bool>();
        dataValue = sol::make_object(returned.lua_state(), LUASF_SOL_NIL);
      }

      if (!keepGoing || is_nil_object(dataValue)) {
        size = 0;
        return keepGoing;
      }

      std::vector<std::byte> bytes;
      if (dataValue.get_type() == sol::type::string) {
        const std::string text = dataValue.as<std::string>();
        bytes.reserve(text.size());
        for (unsigned char byte : text)
          bytes.push_back(static_cast<std::byte>(byte));
      } else {
        bytes = array_from_object<std::byte>(dataValue);
      }

      const std::size_t copyCount =
          std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(size));
      std::memcpy(data, bytes.data(), copyCount);
      size = copyCount;
      return true;
    };
  }
};

template <typename Signature>
std::function<Signature> function_from_object(const sol::object &object) {
  return function_converter<Signature>::from_object(object);
}

} // namespace lua_sf
