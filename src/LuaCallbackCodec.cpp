#include "LuaCallbackCodec.hpp"

#include "utils.hpp"

extern "C" {
#include <lauxlib.h>
}

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lua_sf::callback::detail {

class CallbackContext final {
public:
  CallbackContext(const sol::object &object, std::string callbackLabel)
      : reference(makeLuaRegistryReference(object)),
        label(std::move(callbackLabel)) {}

  LuaRegistryReference reference;
  LuaRegistryReference inputFrames;
  LuaRegistryReference outputFrames;
  std::size_t inputSampleCount{};
  std::size_t outputSampleCount{};
  std::string label;
  std::atomic<bool> faulted{};
};

namespace {

class AutomaticGcPause final {
public:
  explicit AutomaticGcPause(lua_State *state) noexcept
      : state_(state), wasRunning_(lua_gc(state, LUA_GCISRUNNING) != 0) {
    if (wasRunning_)
      lua_gc(state_, LUA_GCSTOP);
  }

  ~AutomaticGcPause() {
    if (wasRunning_)
      lua_gc(state_, LUA_GCRESTART);
    else
      lua_gc(state_, LUA_GCSTOP);
  }

  AutomaticGcPause(const AutomaticGcPause &) = delete;
  AutomaticGcPause &operator=(const AutomaticGcPause &) = delete;

private:
  lua_State *state_{};
  bool wasRunning_{};
};

void recordFault(const std::shared_ptr<CallbackContext> &context,
                 std::string_view message) noexcept {
  bool expected = false;
  if (!context->faulted.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel))
    return;
  context->reference.deferCallbackError(context->label, message);
}

void fallback(const float *input, unsigned int &inputCount, float *output,
              unsigned int &outputCount, unsigned int channels) noexcept {
  const unsigned int originalInputCount = inputCount;
  const unsigned int outputCapacity = outputCount;
  if (input == nullptr || output == nullptr || channels == 0) {
    inputCount = 0;
    outputCount = 0;
    return;
  }
  const unsigned int frameCount = std::min(originalInputCount, outputCapacity);
  if (frameCount != 0 &&
      static_cast<std::size_t>(frameCount) >
          std::numeric_limits<std::size_t>::max() / channels) {
    inputCount = 0;
    outputCount = 0;
    return;
  }
  const std::size_t samples = static_cast<std::size_t>(frameCount) * channels;
  if (samples > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
    inputCount = 0;
    outputCount = 0;
    return;
  }
  std::memmove(output, input, samples * sizeof(float));
  inputCount = frameCount;
  outputCount = frameCount;
}

std::size_t sampleCount(unsigned int frames, unsigned int channels) {
  if (channels != 0 && static_cast<std::size_t>(frames) >
                           std::numeric_limits<std::size_t>::max() / channels)
    throw std::overflow_error("audio callback sample count overflow");
  const std::size_t count = static_cast<std::size_t>(frames) * channels;
  if (count >= static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      count > static_cast<std::size_t>(std::numeric_limits<lua_Integer>::max()))
    throw std::overflow_error("audio callback exceeds Lua array capacity");
  return count;
}

class LuaStackRestore final {
public:
  explicit LuaStackRestore(lua_State *state) noexcept
      : state_(state), top_(lua_gettop(state)) {}

  ~LuaStackRestore() { lua_settop(state_, top_); }

  LuaStackRestore(const LuaStackRestore &) = delete;
  LuaStackRestore &operator=(const LuaStackRestore &) = delete;

private:
  lua_State *state_{};
  int top_{};
};

int pushCachedDenseTable(lua_State *state, LuaRegistryReference &reference,
                         std::size_t sampleCount,
                         std::size_t &previousSampleCount,
                         const float *samples) {
  if (!reference) {
    lua_createtable(state, static_cast<int>(sampleCount), 0);
    reference = LuaRegistryReference(state, -1);
  } else if (!reference.pushUnderExecutionScope()) {
    throw std::runtime_error("Lua audio sample table is unavailable");
  }

  const int tableIndex = lua_absindex(state, -1);
  for (std::size_t index = 0; index < sampleCount; ++index) {
    lua_pushnumber(state, samples == nullptr ? 0.F : samples[index]);
    lua_rawseti(state, tableIndex, static_cast<lua_Integer>(index + 1));
  }
  for (std::size_t index = sampleCount; index < previousSampleCount; ++index) {
    lua_pushnil(state);
    lua_rawseti(state, tableIndex, static_cast<lua_Integer>(index + 1));
  }
  previousSampleCount = sampleCount;
  return tableIndex;
}

void validateResultFields(lua_State *state, int resultIndex) {
  resultIndex = lua_absindex(state, resultIndex);
  lua_pushnil(state);
  while (lua_next(state, resultIndex) != 0) {
    if (lua_type(state, -2) != LUA_TSTRING) {
      throw std::invalid_argument(
          "audio callback result must contain only named fields");
    }
    std::size_t nameLength = 0;
    const char *nameData = lua_tolstring(state, -2, &nameLength);
    const std::string_view name(nameData, nameLength);
    if (name != "inputFrameCount" && name != "outputFrameCount" &&
        name != "outputFrames") {
      throw std::invalid_argument("unknown audio callback result field: " +
                                  std::string(name));
    }
    lua_pop(state, 1);
  }
}

unsigned int readRequiredFrameCount(lua_State *state, int resultIndex,
                                    const char *field, unsigned int capacity) {
  lua_getfield(state, resultIndex, field);
  if (lua_isnil(state, -1)) {
    throw std::invalid_argument(std::string("audio callback result.") + field +
                                " is required");
  }
  unsigned int count = 0;
  if (!tryReadLuaIntegral(state, -1, count)) {
    throw std::invalid_argument(std::string("audio callback result.") + field +
                                " must be a finite, in-range integer");
  }
  lua_pop(state, 1);
  if (count > capacity) {
    throw std::out_of_range(std::string("audio callback result.") + field +
                            " exceeds its native capacity");
  }
  return count;
}

void copyDenseFloatArray(lua_State *state, int tableIndex,
                         std::size_t minimumSize, std::size_t maximumSize,
                         float *output, std::string_view label) {
  if (lua_type(state, tableIndex) != LUA_TTABLE) {
    throw std::invalid_argument(std::string(label) + " must be an array");
  }
  tableIndex = lua_absindex(state, tableIndex);
  std::size_t entryCount = 0;
  std::size_t maximumIndex = 0;
  lua_pushnil(state);
  while (lua_next(state, tableIndex) != 0) {
    std::size_t index = 0;
    if (!tryReadLuaIntegral(state, -2, index)) {
      throw std::invalid_argument(std::string(label) +
                                  " must contain only array indices");
    }
    if (index == 0 || index > maximumSize) {
      throw std::out_of_range(std::string(label) +
                              " index exceeds the permitted capacity");
    }
    if (lua_type(state, -1) != LUA_TNUMBER) {
      throw std::invalid_argument(std::string(label) +
                                  " must contain only numbers");
    }
    ++entryCount;
    maximumIndex = std::max(maximumIndex, index);
    lua_pop(state, 1);
  }
  if (entryCount != maximumIndex) {
    throw std::invalid_argument(std::string(label) +
                                " must be a dense 1-based array");
  }
  if (maximumIndex < minimumSize) {
    throw std::out_of_range(std::string(label) +
                            " does not cover the produced samples");
  }

  for (std::size_t index = 0; index < minimumSize; ++index) {
    lua_rawgeti(state, tableIndex, static_cast<lua_Integer>(index + 1));
    output[index] = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);
  }
}

std::vector<float> readDenseFloatArray(const sol::object &object,
                                       std::size_t minimumSize,
                                       std::size_t maximumSize,
                                       std::string_view label) {
  if (!object.is<sol::table>())
    throw std::invalid_argument(std::string(label) + " must be an array");
  const sol::table table = object.as<sol::table>();
  std::size_t entryCount = 0;
  std::size_t maximumIndex = 0;
  for (const auto &entry : table) {
    const sol::object key = entry.first;
    if (!key.is<LuaIntegral<std::size_t>>())
      throw std::invalid_argument(std::string(label) +
                                  " must contain only array indices");
    const std::size_t index = key.as<LuaIntegral<std::size_t>>().value();
    if (index == 0 || index > maximumSize)
      throw std::out_of_range(std::string(label) +
                              " index exceeds the permitted capacity");
    ++entryCount;
    maximumIndex = std::max(maximumIndex, index);
  }
  if (entryCount != maximumIndex)
    throw std::invalid_argument(std::string(label) +
                                " must be a dense 1-based array");
  if (maximumIndex < minimumSize)
    throw std::out_of_range(std::string(label) +
                            " does not cover the produced samples");

  std::vector<float> values;
  values.reserve(maximumIndex);
  for (std::size_t index = 1; index <= maximumIndex; ++index) {
    const sol::object value = table.raw_get<sol::object>(index);
    if (!value.is<float>())
      throw std::invalid_argument(std::string(label) +
                                  " must contain only numbers");
    values.push_back(value.as<float>());
  }
  return values;
}

template <typename Callback>
decltype(auto) withBlockingCallback(const std::shared_ptr<CallbackContext> &ctx,
                                    Callback &&callback) {
  lua_State *state = ctx->reference.state();
  LuaStateExecutionScope execution(state);
  if (!execution.active())
    throw std::runtime_error("Lua state is stopping");
  if (!ctx->reference.pushUnderExecutionScope())
    throw std::runtime_error("Lua callback is unavailable");
  auto popper = sol::stack::pop_n(state, 1);
  sol::protected_function function =
      sol::stack::get<sol::protected_function>(state, -1);
  return std::forward<Callback>(callback)(function, sol::state_view(state));
}

} // namespace

std::shared_ptr<CallbackContext> makeCallbackContext(const sol::object &object,
                                                     std::string label) {
  if (!object.is<sol::protected_function>())
    throw std::invalid_argument("expected a Lua callback function");
  return std::make_shared<CallbackContext>(object, std::move(label));
}

void invokeInterleavedFloatTransform(
    const std::shared_ptr<CallbackContext> &context, const float *inputFrames,
    unsigned int &inputFrameCount, float *outputFrames,
    unsigned int &outputFrameCount, unsigned int frameChannelCount) noexcept {
  const unsigned int originalInput = inputFrameCount;
  const unsigned int originalOutput = outputFrameCount;
  if (context->faulted.load(std::memory_order_acquire)) {
    fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
             frameChannelCount);
    return;
  }
  if ((inputFrames == nullptr && originalInput != 0) ||
      (outputFrames == nullptr && originalOutput != 0) ||
      frameChannelCount == 0) {
    recordFault(context, "invalid native audio callback buffer capacity");
    fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
             frameChannelCount);
    return;
  }
  lua_State *state = context->reference.state();
  LuaStateTryExecutionScope execution(state);
  if (!execution.active()) {
    fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
             frameChannelCount);
    return;
  }
  try {
    AutomaticGcPause gcPause(state);
    LuaStackRestore stackRestore(state);
    if (!context->reference.pushUnderExecutionScope()) {
      fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
               frameChannelCount);
      return;
    }
    const int functionIndex = lua_absindex(state, -1);
    const std::size_t inputSamples = sampleCount(
        inputFrames == nullptr ? 0 : originalInput, frameChannelCount);
    const std::size_t outputSamples =
        sampleCount(originalOutput, frameChannelCount);
    const int inputTableIndex =
        pushCachedDenseTable(state, context->inputFrames, inputSamples,
                             context->inputSampleCount, inputFrames);
    const int outputTableIndex =
        pushCachedDenseTable(state, context->outputFrames, outputSamples,
                             context->outputSampleCount, nullptr);

    const sol::stack_protected_function function(state, functionIndex);
    const sol::stack_object outputObject(state, outputTableIndex);
    sol::protected_function_result result =
        inputFrames == nullptr
            ? function(lua_sf::LUASF_SOL_NIL, originalInput, outputObject,
                       originalOutput, frameChannelCount)
            : function(sol::stack_object(state, inputTableIndex), originalInput,
                       outputObject, originalOutput, frameChannelCount);
    throw_on_lua_error(result);
    const int resultIndex = result.stack_index();
    if (lua_type(state, resultIndex) != LUA_TTABLE) {
      throw std::invalid_argument("audio callback must return a result table");
    }
    validateResultFields(state, resultIndex);
    const unsigned int consumed = readRequiredFrameCount(
        state, resultIndex, "inputFrameCount", originalInput);
    const unsigned int produced = readRequiredFrameCount(
        state, resultIndex, "outputFrameCount", originalOutput);
    const std::size_t producedSamples =
        sampleCount(produced, frameChannelCount);
    lua_getfield(state, resultIndex, "outputFrames");
    if (lua_isnil(state, -1)) {
      lua_pop(state, 1);
      lua_pushvalue(state, outputTableIndex);
    }
    copyDenseFloatArray(state, -1, producedSamples, outputSamples, outputFrames,
                        "outputFrames");
    inputFrameCount = consumed;
    outputFrameCount = produced;
  } catch (const std::exception &error) {
    recordFault(context, error.what());
    inputFrameCount = originalInput;
    outputFrameCount = originalOutput;
    fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
             frameChannelCount);
  } catch (...) {
    recordFault(context, "unknown Lua audio callback failure");
    inputFrameCount = originalInput;
    outputFrameCount = originalOutput;
    fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
             frameChannelCount);
  }
}

void invokeGlyphPreProcessor(const std::shared_ptr<CallbackContext> &context,
                             const sf::Text::ShapedGlyph &glyph,
                             std::uint32_t &style, sf::Color &fill,
                             sf::Color &outline, float &thickness) {
  withBlockingCallback(context, [&](sol::protected_function &function,
                                    sol::state_view) {
    sol::protected_function_result result = function(
        std::ref(glyph), style, std::ref(fill), std::ref(outline), thickness);
    throw_on_lua_error(result);
    const sol::object returned = result;
    if (is_nil_object(returned) || !returned.is<sol::table>())
      return;
    const sol::table table = returned.as<sol::table>();
    const sol::object styleValue = table["style"];
    const sol::object fillValue = table["fillColor"];
    const sol::object outlineValue = table["outlineColor"];
    const sol::object thicknessValue = table["outlineThickness"];
    if (!is_nil_object(styleValue))
      style = styleValue.as<std::uint32_t>();
    if (!is_nil_object(fillValue))
      fill = fillValue.as<sf::Color>();
    if (!is_nil_object(outlineValue))
      outline = outlineValue.as<sf::Color>();
    if (!is_nil_object(thicknessValue))
      thickness = thicknessValue.as<float>();
  });
}

bool invokeSftpDownload(const std::shared_ptr<CallbackContext> &context,
                        const void *data, std::size_t size) {
  return withBlockingCallback(
      context, [&](sol::protected_function &function, sol::state_view) {
        const char *bytes = static_cast<const char *>(data);
        const std::string buffer = bytes != nullptr && size != 0
                                       ? std::string(bytes, bytes + size)
                                       : std::string{};
        sol::protected_function_result result = function(buffer, size);
        throw_on_lua_error(result);
        return result.get<bool>();
      });
}

bool invokeSftpUpload(const std::shared_ptr<CallbackContext> &context,
                      void *data, std::size_t &size) {
  return withBlockingCallback(
      context, [&](sol::protected_function &function, sol::state_view lua) {
        sol::protected_function_result result = function(size);
        throw_on_lua_error(result);
        const sol::object returned = result;
        if (is_nil_object(returned)) {
          size = 0;
          return false;
        }
        bool keepGoing = true;
        sol::object dataValue = returned;
        if (returned.is<sol::table>()) {
          const sol::table table = returned.as<sol::table>();
          const sol::object keep = table["keepGoing"];
          if (!is_nil_object(keep))
            keepGoing = keep.as<bool>();
          dataValue = table["data"];
        } else if (returned.is<bool>()) {
          keepGoing = returned.as<bool>();
          dataValue = sol::make_object(lua, lua_sf::LUASF_SOL_NIL);
        }
        if (!keepGoing || is_nil_object(dataValue)) {
          size = 0;
          return keepGoing;
        }
        std::vector<std::byte> bytes = array_from_object<std::byte>(dataValue);
        const std::size_t count = std::min(bytes.size(), size);
        std::memcpy(data, bytes.data(), count);
        size = count;
        return true;
      });
}

} // namespace lua_sf::callback::detail

namespace lua_sf::callback {

template <>
sol::object
to_object<sf::SoundSource::EffectProcessor, InterleavedFloatTransformCodec>(
    sol::state_view lua, const sf::SoundSource::EffectProcessor &callable,
    CallbackOptions options) {
  if (!callable) {
    if (options.allowNil)
      return sol::make_object(lua, lua_sf::LUASF_SOL_NIL);
    throw std::invalid_argument(options.label.empty()
                                    ? "native audio callback does not allow nil"
                                    : options.label + " does not allow nil");
  }
  const std::string label = options.label.empty() ? "native audio callback"
                                                  : std::move(options.label);
  return sol::make_object(
      lua,
      sol::as_function([callable, label](sol::object inputValue,
                                         LuaIntegral<unsigned int> inputCount,
                                         sol::object outputValue,
                                         LuaIntegral<unsigned int> outputCount,
                                         LuaIntegral<unsigned int> channels) {
        try {
          const unsigned int inputCapacity = inputCount.value();
          const unsigned int outputCapacity = outputCount.value();
          const unsigned int channelCount = channels.value();
          if (channelCount == 0)
            throw std::invalid_argument(
                "audio callback channel count must be positive");
          const std::size_t inputSamples =
              detail::sampleCount(inputCapacity, channelCount);
          const std::size_t outputSamples =
              detail::sampleCount(outputCapacity, channelCount);
          const bool endOfStream = is_nil_object(inputValue);
          if (endOfStream && inputCapacity != 0)
            throw std::invalid_argument(
                "nil input requires a zero input count");
          std::vector<float> inputValues;
          if (!endOfStream)
            inputValues = detail::readDenseFloatArray(
                inputValue, inputSamples, inputSamples, "inputFrames");
          std::vector<float> outputValues = detail::readDenseFloatArray(
              outputValue, outputSamples, outputSamples, "outputFrames");
          unsigned int consumed = inputCapacity;
          unsigned int produced = outputCapacity;
          callable(endOfStream ? nullptr : inputValues.data(), consumed,
                   outputValues.data(), produced, channelCount);
          if (consumed > inputCapacity || produced > outputCapacity)
            throw std::out_of_range(
                "native audio callback returned a count beyond capacity");
          const std::size_t producedSamples =
              detail::sampleCount(produced, channelCount);
          outputValues.resize(producedSamples);
          sol::state_view state(inputValue.lua_state());
          sol::table result = state.create_table(0, 3);
          result.raw_set("inputFrameCount", consumed);
          result.raw_set("outputFrameCount", produced);
          result.raw_set("outputFrames", vector_to_object(state, outputValues));
          return result;
        } catch (const std::exception &error) {
          throw std::runtime_error(label + ": " + error.what());
        }
      }));
}

} // namespace lua_sf::callback
