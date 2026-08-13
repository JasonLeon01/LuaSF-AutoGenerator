#include "bind_CallbackViews.hpp"

#include "utils.hpp"

extern "C" {
#include <lauxlib.h>
}

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
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

class LeaseGuard final {
public:
  explicit LeaseGuard(std::shared_ptr<BorrowLease> value)
      : lease(std::move(value)) {}
  ~LeaseGuard() { lease->invalidate(); }

private:
  std::shared_ptr<BorrowLease> lease;
};

std::size_t sampleCount(unsigned int frames, unsigned int channels) {
  if (channels != 0 && static_cast<std::size_t>(frames) >
                           std::numeric_limits<std::size_t>::max() / channels)
    throw std::overflow_error("audio callback sample count overflow");
  return static_cast<std::size_t>(frames) * channels;
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

std::shared_ptr<CallbackContext>
makeCallbackContext(const sol::object &object, std::string label) {
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
  if (outputFrames == nullptr || frameChannelCount == 0) {
    if ((outputFrames == nullptr && originalOutput != 0) ||
        (frameChannelCount == 0 &&
         (originalInput != 0 || originalOutput != 0)))
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
    const auto lease = std::make_shared<BorrowLease>();
    LeaseGuard guard(lease);
    const std::size_t inputSamples =
        sampleCount(inputFrames == nullptr ? 0 : originalInput,
                    frameChannelCount);
    const std::size_t outputSamples =
        sampleCount(originalOutput, frameChannelCount);
    ReadOnlyFloatBufferView inputView(
        lease, const_cast<float *>(inputFrames), inputSamples);
    WriteOnlyFloatBufferView outputView(lease, outputFrames, outputSamples);
    UIntRef inputRef(lease, inputFrames == nullptr ? 0 : originalInput,
                     inputFrames == nullptr ? 0 : originalInput);
    UIntRef outputRef(lease, originalOutput, originalOutput);
    sol::state_view lua(state);
    const sol::object inputObject =
        inputFrames == nullptr
            ? sol::make_object(lua, lua_sf::LUASF_SOL_NIL)
            : sol::make_object_userdata(lua, inputView);
    const sol::object inputRefObject = sol::make_object_userdata(lua, inputRef);
    const sol::object outputObject = sol::make_object_userdata(lua, outputView);
    const sol::object outputRefObject =
        sol::make_object_userdata(lua, outputRef);
    sol::protected_function_result result =
        function(inputObject, inputRefObject, outputObject, outputRefObject,
                 frameChannelCount);
    throw_on_lua_error(result);
    inputFrameCount = inputRef.value();
    outputFrameCount = outputRef.value();
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
    sol::protected_function_result result =
        function(std::ref(glyph), style, std::ref(fill), std::ref(outline),
                 thickness);
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
        const std::string buffer =
            bytes != nullptr && size != 0 ? std::string(bytes, bytes + size)
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

void bindCallbackViews(sol::state_view lua) {
  sol::table sf = lua_sf::sf_table(lua);
  sf.new_usertype<ReadOnlyFloatBufferView>(
      "ReadOnlyFloatBufferView", sol::no_constructor, sol::meta_function::length,
      &ReadOnlyFloatBufferView::size, sol::meta_function::index,
      [](const ReadOnlyFloatBufferView &view, LuaIntegral<std::size_t> index) {
        return view.get(index.value());
      },
      sol::meta_function::new_index,
      [](ReadOnlyFloatBufferView &, sol::object, sol::object) {
        throw std::logic_error("read-only audio buffer cannot be written");
      },
      "size", &ReadOnlyFloatBufferView::size);
  sf.new_usertype<WriteOnlyFloatBufferView>(
      "WriteOnlyFloatBufferView", sol::no_constructor, sol::meta_function::length,
      &WriteOnlyFloatBufferView::size, sol::meta_function::index,
      [](const WriteOnlyFloatBufferView &view, sol::object key) -> sol::object {
        if (key.is<std::string>() && key.as<std::string>() == "size")
          return sol::make_object(key.lua_state(), &WriteOnlyFloatBufferView::size);
        throw std::logic_error("write-only audio buffer cannot be read");
      },
      sol::meta_function::new_index,
      [](WriteOnlyFloatBufferView &view, LuaIntegral<std::size_t> index,
         float value) { view.set(index.value(), value); },
      "size", &WriteOnlyFloatBufferView::size);
  sf.new_usertype<UIntRef>(
      "UIntRef", sol::no_constructor, "value",
      sol::property(
          &UIntRef::value,
          [](UIntRef &value, LuaIntegral<unsigned int> next) {
            value.setValue(next.value());
          }),
      "capacity",
      sol::readonly_property(&UIntRef::capacity));
  lua_sf::stub::doc(
      "Borrowed 1-based flat interleaved input samples. The view is read-only "
      "and expires when the effect callback returns.");
  lua_sf::stub::class_("sf.ReadOnlyFloatBufferView");
  lua_sf::stub::field("[integer]", "number");
  lua_sf::stub::doc("Return the borrowed sample capacity.");
  lua_sf::stub::field("size", "fun(self: sf.ReadOnlyFloatBufferView): integer");
  lua_sf::stub::doc(
      "Borrowed 1-based flat interleaved output samples. Indexed assignment "
      "writes samples; indexed reads are runtime errors. The view expires when "
      "the effect callback returns.");
  lua_sf::stub::class_("sf.WriteOnlyFloatBufferView");
  lua_sf::stub::doc(
      "Write-only indexed sample slot. This field annotation describes "
      "assignment only; reading it is a runtime error.");
  lua_sf::stub::field("[integer]", "number");
  lua_sf::stub::doc("Return the borrowed sample capacity.");
  lua_sf::stub::field("size", "fun(self: sf.WriteOnlyFloatBufferView): integer");
  lua_sf::stub::doc(
      "Borrowed mutable frame count. The ref expires when the effect callback "
      "returns.");
  lua_sf::stub::class_("sf.UIntRef");
  lua_sf::stub::doc("Current frame count; writable from 0 through capacity.");
  lua_sf::stub::field("value", "integer");
  lua_sf::stub::doc("Read-only frame capacity.");
  lua_sf::stub::field("capacity", "integer");
}

template <>
sol::object to_object<sf::SoundSource::EffectProcessor,
                      InterleavedFloatTransformCodec>(
    sol::state_view lua, const sf::SoundSource::EffectProcessor &callable,
    CallbackOptions) {
  if (!callable)
    return sol::make_object(lua, lua_sf::LUASF_SOL_NIL);
  return sol::make_object(
      lua, sol::as_function([callable](sol::object inputValue,
                                      UIntRef &inputCount,
                                      WriteOnlyFloatBufferView &output,
                                      UIntRef &outputCount,
                                      LuaIntegral<unsigned int> channels) {
        if (!inputValue.is<ReadOnlyFloatBufferView>() &&
            !is_nil_object(inputValue))
          throw std::invalid_argument("expected a borrowed input buffer or nil");
        const ReadOnlyFloatBufferView *input =
            inputValue.is<ReadOnlyFloatBufferView>()
                ? &inputValue.as<ReadOnlyFloatBufferView &>()
                : nullptr;
        const unsigned int channelCount = channels.value();
        if (channelCount == 0)
          throw std::invalid_argument(
              "audio callback channel count must be positive");
        const BorrowLease *lease = output.leaseIdentity();
        if (inputCount.leaseIdentity() != lease ||
            outputCount.leaseIdentity() != lease ||
            (input != nullptr && input->leaseIdentity() != lease))
          throw std::invalid_argument(
              "audio callback views and refs must share one active lease");
        if (input == nullptr &&
            (inputCount.value() != 0 || inputCount.capacity() != 0))
          throw std::invalid_argument("nil input requires a zero input count");
        const auto requireCapacity = [channelCount](unsigned int capacity,
                                                    std::size_t samples) {
          if (channelCount != 0 &&
              static_cast<std::size_t>(capacity) >
                  std::numeric_limits<std::size_t>::max() / channelCount)
            throw std::overflow_error("audio callback sample count overflow");
          if (static_cast<std::size_t>(capacity) * channelCount != samples)
            throw std::invalid_argument(
                "audio callback count capacity does not match its buffer view");
        };
        if (input != nullptr)
          requireCapacity(inputCount.capacity(), input->size());
        requireCapacity(outputCount.capacity(), output.size());
        unsigned int consumed = inputCount.value();
        unsigned int produced = outputCount.value();
        callable(input == nullptr ? nullptr : input->nativeData(), consumed,
                 output.nativeData(), produced, channelCount);
        inputCount.setValue(consumed);
        outputCount.setValue(produced);
      }));
}

} // namespace lua_sf::callback
