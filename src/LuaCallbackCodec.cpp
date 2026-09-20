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
    CallbackContext(const lua_glue::Object& object, std::string callbackLabel)
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
    explicit AutomaticGcPause(lua_State* state) noexcept
        : state_(state), wasRunning_(lua_gc(state, LUA_GCISRUNNING) != 0) {
        if (wasRunning_) {
            lua_gc(state_, LUA_GCSTOP);
        }
    }

    ~AutomaticGcPause() {
        if (wasRunning_) {
            lua_gc(state_, LUA_GCRESTART);
        } else {
            lua_gc(state_, LUA_GCSTOP);
        }
    }

    AutomaticGcPause(const AutomaticGcPause&) = delete;
    AutomaticGcPause& operator=(const AutomaticGcPause&) = delete;

private:
    lua_State* state_{};
    bool wasRunning_{};
};

void recordFault(const std::shared_ptr<CallbackContext>& context,
                 std::string_view message) noexcept {
    bool expected = false;
    if (!context->faulted.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return;
    }
    context->reference.deferCallbackError(context->label, message);
}

void fallback(const float* input, unsigned int& inputCount, float* output,
              unsigned int& outputCount, unsigned int channels) noexcept {
    const unsigned int originalInputCount = inputCount;
    const unsigned int outputCapacity = outputCount;
    if (input == nullptr || output == nullptr || channels == 0) {
        inputCount = 0;
        outputCount = 0;
        return;
    }
    const unsigned int frameCount =
        std::min(originalInputCount, outputCapacity);
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
    if (channels != 0 &&
        static_cast<std::size_t>(frames) >
            std::numeric_limits<std::size_t>::max() / channels) {
        throw std::overflow_error("audio callback sample count overflow");
    }
    const std::size_t count = static_cast<std::size_t>(frames) * channels;
    if (count >= static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        count >
            static_cast<std::size_t>(std::numeric_limits<lua_Integer>::max())) {
        throw std::overflow_error("audio callback exceeds Lua array capacity");
    }
    return count;
}

int pushCachedDenseTable(lua_State* state, LuaRegistryReference& reference,
                         std::size_t sampleCount,
                         std::size_t& previousSampleCount,
                         const float* samples) {
    if (!lua_checkstack(state, 8)) {
        throw std::runtime_error("Lua audio sample stack cannot grow");
    }
    if (!reference) {
        lua_pushcfunction(state, [](lua_State* inner) -> int {
            lua_createtable(inner, static_cast<int>(lua_tointeger(inner, 1)),
                            0);
            return 1;
        });
        lua_pushinteger(state, static_cast<lua_Integer>(sampleCount));
        lua_glue::ProtectedStackCall(state, 1, 1);
        reference = LuaRegistryReference(state, -1);
    } else if (!reference.pushUnderExecutionScope()) {
        throw std::runtime_error("Lua audio sample table is unavailable");
    }

    const int tableIndex = lua_absindex(state, -1);
    lua_pushcfunction(state, [](lua_State* inner) -> int {
        const auto* samples =
            static_cast<const float*>(lua_touserdata(inner, 2));
        const lua_Integer count = lua_tointeger(inner, 3);
        const lua_Integer previous = lua_tointeger(inner, 4);
        for (lua_Integer index = 0; index < count; ++index) {
            lua_pushnumber(inner, samples == nullptr ? 0.F : samples[index]);
            lua_rawseti(inner, 1, index + 1);
        }
        for (lua_Integer index = count; index < previous; ++index) {
            lua_pushnil(inner);
            lua_rawseti(inner, 1, index + 1);
        }
        return 0;
    });
    lua_pushvalue(state, tableIndex);
    lua_pushlightuserdata(state, const_cast<float*>(samples));
    lua_pushinteger(state, static_cast<lua_Integer>(sampleCount));
    lua_pushinteger(state, static_cast<lua_Integer>(previousSampleCount));
    lua_glue::ProtectedStackCall(state, 4, 0);
    previousSampleCount = sampleCount;
    return tableIndex;
}

void validateResultFields(lua_State* state, int resultIndex) {
    resultIndex = lua_absindex(state, resultIndex);
    lua_pushnil(state);
    while (lua_next(state, resultIndex) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
            throw std::invalid_argument(
                "audio callback result must contain only named fields");
        }
        std::size_t nameLength = 0;
        const char* nameData = lua_tolstring(state, -2, &nameLength);
        const std::string_view name(nameData, nameLength);
        if (name != "inputFrameCount" && name != "outputFrameCount" &&
            name != "outputFrames") {
            throw std::invalid_argument(
                "unknown audio callback result field: " + std::string(name));
        }
        lua_pop(state, 1);
    }
}

unsigned int readRequiredFrameCount(lua_State* state, int resultIndex,
                                    const char* field, unsigned int capacity) {
    lua_glue::Push(state, std::string_view(field));
    lua_rawget(state, resultIndex);
    if (lua_isnil(state, -1)) {
        throw std::invalid_argument(std::string("audio callback result.") +
                                    field + " is required");
    }
    unsigned int count = 0;
    if (!tryReadLuaIntegral(state, -1, count)) {
        throw std::invalid_argument(std::string("audio callback result.") +
                                    field +
                                    " must be a finite, in-range integer");
    }
    lua_pop(state, 1);
    if (count > capacity) {
        throw std::out_of_range(std::string("audio callback result.") + field +
                                " exceeds its native capacity");
    }
    return count;
}

void copyDenseFloatArray(lua_State* state, int tableIndex,
                         std::size_t minimumSize, std::size_t maximumSize,
                         float* output, std::string_view label) {
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

std::vector<float> readDenseFloatArray(const lua_glue::Object& object,
                                       std::size_t minimumSize,
                                       std::size_t maximumSize,
                                       std::string_view label) {
    if (!object.is<lua_glue::Table>()) {
        throw std::invalid_argument(std::string(label) + " must be an array");
    }
    const lua_glue::Table table = object.as<lua_glue::Table>();
    std::size_t entryCount = 0;
    std::size_t maximumIndex = 0;
    for (const auto& entry : table) {
        const lua_glue::Object key = entry.first;
        if (!key.is<LuaIntegral<std::size_t>>()) {
            throw std::invalid_argument(std::string(label) +
                                        " must contain only array indices");
        }
        const std::size_t index = key.as<LuaIntegral<std::size_t>>().value();
        if (index == 0 || index > maximumSize) {
            throw std::out_of_range(std::string(label) +
                                    " index exceeds the permitted capacity");
        }
        ++entryCount;
        maximumIndex = std::max(maximumIndex, index);
    }
    if (entryCount != maximumIndex) {
        throw std::invalid_argument(std::string(label) +
                                    " must be a dense 1-based array");
    }
    if (maximumIndex < minimumSize) {
        throw std::out_of_range(std::string(label) +
                                " does not cover the produced samples");
    }

    std::vector<float> values;
    values.reserve(maximumIndex);
    for (std::size_t index = 1; index <= maximumIndex; ++index) {
        const lua_glue::Object value = table.raw_get<lua_glue::Object>(index);
        if (!value.is<float>()) {
            throw std::invalid_argument(std::string(label) +
                                        " must contain only numbers");
        }
        values.push_back(value.as<float>());
    }
    return values;
}

template <typename Callback>
decltype(auto) withBlockingCallback(const std::shared_ptr<CallbackContext>& ctx,
                                    Callback&& callback) {
    lua_State* state = ctx->reference.state();
    LuaStateExecutionScope execution(state);
    if (!execution.active()) {
        throw std::runtime_error("Lua state is stopping");
    }
    if (!ctx->reference.pushUnderExecutionScope()) {
        throw std::runtime_error("Lua callback is unavailable");
    }
    auto popper = lua_glue::PopGuard(state, 1);
    lua_glue::Function function = lua_glue::Read<lua_glue::Function>(state, -1);
    return std::forward<Callback>(callback)(function,
                                            lua_glue::StateView(state));
}

}  // namespace

std::shared_ptr<CallbackContext> makeCallbackContext(
    const lua_glue::Object& object, std::string label) {
    if (!object.is<lua_glue::Function>()) {
        throw std::invalid_argument("expected a Lua callback function");
    }
    return std::make_shared<CallbackContext>(object, std::move(label));
}

void invokeInterleavedFloatTransform(
    const std::shared_ptr<CallbackContext>& context, const float* inputFrames,
    unsigned int& inputFrameCount, float* outputFrames,
    unsigned int& outputFrameCount, unsigned int frameChannelCount) noexcept {
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
    lua_State* state = context->reference.state();
    LuaStateTryExecutionScope execution(state);
    if (!execution.active()) {
        fallback(inputFrames, inputFrameCount, outputFrames, outputFrameCount,
                 frameChannelCount);
        return;
    }
    try {
        AutomaticGcPause gcPause(state);
        lua_glue::StackGuard stackRestore(state);
        if (!context->reference.pushUnderExecutionScope()) {
            fallback(inputFrames, inputFrameCount, outputFrames,
                     outputFrameCount, frameChannelCount);
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

        lua_pushvalue(state, functionIndex);
        if (inputFrames == nullptr) {
            lua_pushnil(state);
        } else {
            lua_pushvalue(state, inputTableIndex);
        }
        lua_pushinteger(state, originalInput);
        lua_pushvalue(state, outputTableIndex);
        lua_pushinteger(state, originalOutput);
        lua_pushinteger(state, frameChannelCount);
        lua_glue::ProtectedStackCall(state, 5, 1);
        const int resultIndex = lua_absindex(state, -1);
        if (lua_type(state, resultIndex) != LUA_TTABLE) {
            throw std::invalid_argument(
                "audio callback must return a result table");
        }
        validateResultFields(state, resultIndex);
        const unsigned int consumed = readRequiredFrameCount(
            state, resultIndex, "inputFrameCount", originalInput);
        const unsigned int produced = readRequiredFrameCount(
            state, resultIndex, "outputFrameCount", originalOutput);
        const std::size_t producedSamples =
            sampleCount(produced, frameChannelCount);
        lua_glue::Push(state, std::string_view("outputFrames"));
        lua_rawget(state, resultIndex);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushvalue(state, outputTableIndex);
        }
        copyDenseFloatArray(state, -1, producedSamples, outputSamples,
                            outputFrames, "outputFrames");
        inputFrameCount = consumed;
        outputFrameCount = produced;
    } catch (const std::exception& error) {
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

void invokeGlyphPreProcessor(const std::shared_ptr<CallbackContext>& context,
                             const sf::Text::ShapedGlyph& glyph,
                             std::uint32_t& style, sf::Color& fill,
                             sf::Color& outline, float& thickness) {
    withBlockingCallback(
        context, [&](lua_glue::Function& function, lua_glue::StateView) {
            lua_glue::CallResult result =
                function(std::ref(glyph), style, std::ref(fill),
                         std::ref(outline), thickness);
            throw_on_lua_error(result);
            const lua_glue::Object returned = result;
            if (is_nil_object(returned) || !returned.is<lua_glue::Table>()) {
                return;
            }
            const lua_glue::Table table = returned.as<lua_glue::Table>();
            const lua_glue::Object styleValue = table["style"];
            const lua_glue::Object fillValue = table["fillColor"];
            const lua_glue::Object outlineValue = table["outlineColor"];
            const lua_glue::Object thicknessValue = table["outlineThickness"];
            if (!is_nil_object(styleValue)) {
                style = styleValue.as<std::uint32_t>();
            }
            if (!is_nil_object(fillValue)) {
                fill = fillValue.as<sf::Color>();
            }
            if (!is_nil_object(outlineValue)) {
                outline = outlineValue.as<sf::Color>();
            }
            if (!is_nil_object(thicknessValue)) {
                thickness = thicknessValue.as<float>();
            }
        });
}

bool invokeSftpDownload(const std::shared_ptr<CallbackContext>& context,
                        const void* data, std::size_t size) {
    return withBlockingCallback(
        context, [&](lua_glue::Function& function, lua_glue::StateView) {
            const char* bytes = static_cast<const char*>(data);
            const std::string buffer = bytes != nullptr && size != 0
                                           ? std::string(bytes, bytes + size)
                                           : std::string{};
            lua_glue::CallResult result = function(buffer, size);
            throw_on_lua_error(result);
            return result.get<bool>();
        });
}

bool invokeSftpUpload(const std::shared_ptr<CallbackContext>& context,
                      void* data, std::size_t& size) {
    return withBlockingCallback(context, [&](lua_glue::Function& function,
                                             lua_glue::StateView lua) {
        lua_glue::CallResult result = function(size);
        throw_on_lua_error(result);
        const lua_glue::Object returned = result;
        if (is_nil_object(returned)) {
            size = 0;
            return false;
        }
        bool keepGoing = true;
        lua_glue::Object dataValue = returned;
        if (returned.is<lua_glue::Table>()) {
            const lua_glue::Table table = returned.as<lua_glue::Table>();
            const lua_glue::Object keep = table["keepGoing"];
            if (!is_nil_object(keep)) {
                keepGoing = keep.as<bool>();
            }
            dataValue = table["data"];
        } else if (returned.is<bool>()) {
            keepGoing = returned.as<bool>();
            dataValue = lua_glue::MakeObject(lua, lua_glue::nil);
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

}  // namespace lua_sf::callback::detail

namespace lua_sf::callback {

template <>
lua_glue::Object
to_object<sf::SoundSource::EffectProcessor, InterleavedFloatTransformCodec>(
    lua_glue::StateView lua, const sf::SoundSource::EffectProcessor& callable,
    CallbackOptions options) {
    if (!callable) {
        if (options.allowNil) {
            return lua_glue::MakeObject(lua, lua_glue::nil);
        }
        throw std::invalid_argument(
            options.label.empty() ? "native audio callback does not allow nil"
                                  : options.label + " does not allow nil");
    }
    const std::string label = options.label.empty() ? "native audio callback"
                                                    : std::move(options.label);
    return lua_glue::MakeObject(lua, [callable, label](
                                         lua_glue::Object inputValue,
                                         LuaIntegral<unsigned int> inputCount,
                                         lua_glue::Object outputValue,
                                         LuaIntegral<unsigned int> outputCount,
                                         LuaIntegral<unsigned int> channels) {
        try {
            const unsigned int inputCapacity = inputCount.value();
            const unsigned int outputCapacity = outputCount.value();
            const unsigned int channelCount = channels.value();
            if (channelCount == 0) {
                throw std::invalid_argument(
                    "audio callback channel count must be positive");
            }
            const std::size_t inputSamples =
                detail::sampleCount(inputCapacity, channelCount);
            const std::size_t outputSamples =
                detail::sampleCount(outputCapacity, channelCount);
            const bool endOfStream = is_nil_object(inputValue);
            if (endOfStream && inputCapacity != 0) {
                throw std::invalid_argument(
                    "nil input requires a zero input count");
            }
            std::vector<float> inputValues;
            if (!endOfStream) {
                inputValues = detail::readDenseFloatArray(
                    inputValue, inputSamples, inputSamples, "inputFrames");
            }
            std::vector<float> outputValues = detail::readDenseFloatArray(
                outputValue, outputSamples, outputSamples, "outputFrames");
            unsigned int consumed = inputCapacity;
            unsigned int produced = outputCapacity;
            callable(endOfStream ? nullptr : inputValues.data(), consumed,
                     outputValues.data(), produced, channelCount);
            if (consumed > inputCapacity || produced > outputCapacity) {
                throw std::out_of_range(
                    "native audio callback returned a count beyond capacity");
            }
            const std::size_t producedSamples =
                detail::sampleCount(produced, channelCount);
            outputValues.resize(producedSamples);
            lua_glue::StateView state(inputValue.lua_state());
            lua_glue::Table result = state.create_table(0, 3);
            result.raw_set("inputFrameCount", consumed);
            result.raw_set("outputFrameCount", produced);
            result.raw_set("outputFrames",
                           vector_to_object(state, outputValues));
            return result;
        } catch (const std::exception& error) {
            throw std::runtime_error(label + ": " + error.what());
        }
    });
}

}  // namespace lua_sf::callback
