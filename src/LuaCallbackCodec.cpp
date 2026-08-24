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
  std::string label;
  std::atomic<bool> faulted{};
};

namespace {

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

unsigned int readRequiredFrameCount(const sol::table &result, const char *field,
                                    unsigned int capacity) {
  const sol::object value = result.raw_get<sol::object>(field);
  if (is_nil_object(value))
    throw std::invalid_argument(std::string("audio callback result.") + field +
                                " is required");
  const unsigned int count = object_as<unsigned int>(value);
  if (count > capacity)
    throw std::out_of_range(std::string("audio callback result.") + field +
                            " exceeds its native capacity");
  return count;
}

void validateResultFields(const sol::table &result) {
  for (const auto &entry : result) {
    const sol::object key = entry.first;
    if (!key.is<std::string>())
      throw std::invalid_argument(
          "audio callback result must contain only named fields");
    const std::string name = key.as<std::string>();
    if (name != "inputFrameCount" && name != "outputFrameCount" &&
        name != "outputFrames")
      throw std::invalid_argument("unknown audio callback result field: " +
                                  name);
  }
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
    if (!context->reference.pushUnderExecutionScope()) {
      fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
               frameChannelCount);
      return;
    }
    auto popper = sol::stack::pop_n(state, 1);
    sol::protected_function function =
        sol::stack::get<sol::protected_function>(state, -1);
    const std::size_t inputSamples = sampleCount(
        inputFrames == nullptr ? 0 : originalInput, frameChannelCount);
    const std::size_t outputSamples =
        sampleCount(originalOutput, frameChannelCount);
    sol::state_view lua(state);
    std::vector<float> inputValues;
    if (inputFrames != nullptr)
      inputValues.assign(inputFrames, inputFrames + inputSamples);
    std::vector<float> outputValues(outputSamples, 0.F);
    const sol::object inputObject =
        inputFrames == nullptr ? sol::make_object(lua, lua_sf::LUASF_SOL_NIL)
                               : vector_to_object(lua, inputValues);
    const sol::object outputObject = vector_to_object(lua, outputValues);
    sol::protected_function_result result =
        function(inputObject, originalInput, outputObject, originalOutput,
                 frameChannelCount);
    throw_on_lua_error(result);
    const sol::object returned = result;
    if (!returned.is<sol::table>())
      throw std::invalid_argument("audio callback must return a result table");
    const sol::table resultTable = returned.as<sol::table>();
    validateResultFields(resultTable);
    const unsigned int consumed =
        readRequiredFrameCount(resultTable, "inputFrameCount", originalInput);
    const unsigned int produced =
        readRequiredFrameCount(resultTable, "outputFrameCount", originalOutput);
    const std::size_t producedSamples =
        sampleCount(produced, frameChannelCount);
    const sol::object replacement =
        resultTable.raw_get<sol::object>("outputFrames");
    const sol::object selectedOutput =
        is_nil_object(replacement) ? outputObject : replacement;
    outputValues = readDenseFloatArray(selectedOutput, producedSamples,
                                       outputSamples, "outputFrames");
    if (producedSamples != 0)
      std::copy_n(outputValues.begin(), producedSamples, outputFrames);
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
