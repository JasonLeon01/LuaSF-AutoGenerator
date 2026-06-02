#pragma once

namespace lua_sf {

template <typename T>
struct is_std_vector : std::false_type
{
};

template <typename T, typename Allocator>
struct is_std_vector<std::vector<T, Allocator>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_std_vector_v = is_std_vector<std::remove_cv_t<std::remove_reference_t<T>>>::value;

inline bool is_nil_object(const sol::object& object)
{
    return !object.valid() || object == LUASF_SOL_NIL;
}

inline void throw_on_lua_error(const sol::protected_function_result& result)
{
    if (result.valid())
        return;

    const sol::error error = result;
    throw std::runtime_error(error.what());
}

template <typename T>
T object_as(const sol::object& object)
{
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<U, sf::String>)
    {
        return to_sf_string(object.as<std::string>());
    }
    else if constexpr (std::is_same_v<U, std::filesystem::path>)
    {
        return std::filesystem::path(object.as<std::string>());
    }
    else if constexpr (std::is_same_v<U, std::byte>)
    {
        return static_cast<std::byte>(object.as<unsigned int>());
    }
    else
    {
        return object.as<U>();
    }
}

template <typename T>
std::vector<T> array_from_object(const sol::object& object)
{
    if (object.get_type() == sol::type::table)
    {
        const sol::table table = object.as<sol::table>();
        std::vector<T> values;
        values.reserve(table.size());

        if constexpr (is_byte_like_v<T>)
        {
            for (const auto& entry : table)
                values.push_back(static_cast<T>(entry.second.as<unsigned int>()));
            return values;
        }

        for (const auto& entry : table)
        {
            const sol::object value = entry.second;
            values.push_back(object_as<T>(value));
        }
        return values;
    }

    if constexpr (is_byte_like_v<T>)
    {
        if (object.get_type() == sol::type::string)
        {
            const auto data = object.as<std::string>();
            std::vector<T> values;
            values.reserve(data.size());
            for (unsigned char byte : data)
                values.push_back(static_cast<T>(byte));
            return values;
        }
    }

    throw std::runtime_error("expected Lua table or byte string for array conversion");
}

template <typename T>
std::optional<T> optional_from_object(const sol::object& object)
{
    if (!object.valid() || object == LUASF_SOL_NIL)
        return std::nullopt;
    return object_as<T>(object);
}

template <typename T>
sol::object as_lua_object(sol::state_view lua, const T& value)
{
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<U, sf::String>)
    {
        return sol::make_object(lua, to_utf8_string(value));
    }
    else if constexpr (std::is_same_v<U, std::filesystem::path>)
    {
        return sol::make_object(lua, value.string());
    }
    else if constexpr (is_std_vector_v<U>)
    {
        return vector_to_object(lua, value);
    }
    else
    {
        return sol::make_object(lua, value);
    }
}

template <typename T, typename Allocator>
sol::object vector_to_object(sol::state_view lua, const std::vector<T, Allocator>& values)
{
    sol::table table = lua.create_table(static_cast<int>(values.size()), 0);
    int index = 1;
    for (const auto& value : values)
        table[index++] = as_lua_object(lua, value);
    return sol::make_object(lua, table);
}

template <typename T>
sol::object optional_to_object(sol::state_view lua, const std::optional<T>& value)
{
    if (!value)
        return sol::make_object(lua, LUASF_SOL_NIL);

    return as_lua_object(lua, *value);
}

template <typename T>
sol::object optional_to_object(sol::state_view lua, std::optional<T>&& value)
{
    if (!value)
        return sol::make_object(lua, LUASF_SOL_NIL);

    using U = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<U, sf::String>)
    {
        return sol::make_object(lua, to_utf8_string(*value));
    }
    else if constexpr (std::is_same_v<U, std::filesystem::path>)
    {
        return sol::make_object(lua, value->string());
    }
    else if constexpr (is_std_vector_v<U>)
    {
        return vector_to_object(lua, *value);
    }
    else
    {
        return sol::make_object(lua, std::move(*value));
    }
}

template <typename T, typename Allocator>
sol::object optional_to_object(sol::state_view lua, const std::optional<std::vector<T, Allocator>>& value)
{
    if (!value)
        return sol::make_object(lua, LUASF_SOL_NIL);
    return vector_to_object(lua, *value);
}

template <typename T, typename Allocator>
sol::object optional_to_object(sol::state_view lua, std::optional<std::vector<T, Allocator>>&& value)
{
    if (!value)
        return sol::make_object(lua, LUASF_SOL_NIL);
    return vector_to_object(lua, *value);
}

template <typename Signature>
struct function_converter;

template <typename R, typename... Args>
struct function_converter<R(Args...)>
{
    static std::function<R(Args...)> from_object(const sol::object& object)
    {
        if (is_nil_object(object))
            return {};

        sol::protected_function callback = object.as<sol::protected_function>();
        return [callback = std::move(callback)](Args... args) mutable -> R {
            sol::protected_function_result result = callback(std::forward<Args>(args)...);
            throw_on_lua_error(result);
            if constexpr (!std::is_void_v<R>)
                return result.get<R>();
        };
    }
};

template <>
struct function_converter<void(const float*, unsigned int&, float*, unsigned int&, unsigned int)>
{
    static std::function<void(const float*, unsigned int&, float*, unsigned int&, unsigned int)> from_object(
        const sol::object& object)
    {
        if (is_nil_object(object))
            return {};

        sol::protected_function callback = object.as<sol::protected_function>();
        return [callback = std::move(callback)](const float* inputFrames,
                                                unsigned int& inputFrameCount,
                                                float* outputFrames,
                                                unsigned int& outputFrameCount,
                                                unsigned int frameChannelCount) mutable {
            const std::size_t inputSize = static_cast<std::size_t>(inputFrameCount) * frameChannelCount;
            const std::size_t outputSize = static_cast<std::size_t>(outputFrameCount) * frameChannelCount;
            std::vector<float> input;
            std::vector<float> output;
            if (inputFrames && inputSize != 0)
                input.assign(inputFrames, inputFrames + inputSize);
            if (outputFrames && outputSize != 0)
                output.assign(outputFrames, outputFrames + outputSize);

            sol::protected_function_result result = callback(
                sol::as_table(input),
                inputFrameCount,
                sol::as_table(output),
                outputFrameCount,
                frameChannelCount);
            throw_on_lua_error(result);

            const sol::object returned = result;
            if (is_nil_object(returned))
                return;

            std::vector<float> returnedOutput;
            if (returned.get_type() == sol::type::table)
            {
                const sol::table table = returned.as<sol::table>();
                sol::object outputValue = table["output"];
                if (is_nil_object(outputValue))
                    outputValue = table;
                returnedOutput = outputValue.as<std::vector<float>>();

                const sol::object countValue = table["outputFrameCount"];
                if (!is_nil_object(countValue))
                    outputFrameCount = countValue.as<unsigned int>();
            }
            else
            {
                returnedOutput = returned.as<std::vector<float>>();
            }

            const std::size_t copyCount = std::min<std::size_t>(returnedOutput.size(), outputSize);
            std::copy_n(returnedOutput.begin(), copyCount, outputFrames);
        };
    }
};

template <>
struct function_converter<void(const sf::Text::ShapedGlyph&, std::uint32_t&, sf::Color&, sf::Color&, float&)>
{
    static std::function<void(const sf::Text::ShapedGlyph&, std::uint32_t&, sf::Color&, sf::Color&, float&)> from_object(
        const sol::object& object)
    {
        if (is_nil_object(object))
            return {};

        sol::protected_function callback = object.as<sol::protected_function>();
        return [callback = std::move(callback)](const sf::Text::ShapedGlyph& shapedGlyph,
                                                std::uint32_t& style,
                                                sf::Color& fillColor,
                                                sf::Color& outlineColor,
                                                float& outlineThickness) mutable {
            sol::protected_function_result result = callback(
                std::ref(shapedGlyph),
                style,
                std::ref(fillColor),
                std::ref(outlineColor),
                outlineThickness);
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

template <>
struct function_converter<bool(const void*, std::size_t)>
{
    static std::function<bool(const void*, std::size_t)> from_object(const sol::object& object)
    {
        if (is_nil_object(object))
            return {};

        sol::protected_function callback = object.as<sol::protected_function>();
        return [callback = std::move(callback)](const void* data, std::size_t size) mutable {
            const auto* bytes = static_cast<const char*>(data);
            const std::string buffer = bytes && size != 0 ? std::string(bytes, bytes + size) : std::string{};
            sol::protected_function_result result = callback(buffer, size);
            throw_on_lua_error(result);
            return result.get<bool>();
        };
    }
};

template <>
struct function_converter<bool(void*, std::size_t&)>
{
    static std::function<bool(void*, std::size_t&)> from_object(const sol::object& object)
    {
        if (is_nil_object(object))
            return {};

        sol::protected_function callback = object.as<sol::protected_function>();
        return [callback = std::move(callback)](void* data, std::size_t& size) mutable {
            sol::protected_function_result result = callback(size);
            throw_on_lua_error(result);

            const sol::object returned = result;
            if (is_nil_object(returned))
            {
                size = 0;
                return false;
            }

            bool keepGoing = true;
            sol::object dataValue = returned;
            if (returned.get_type() == sol::type::table)
            {
                const sol::table table = returned.as<sol::table>();
                const sol::object keepGoingValue = table["keepGoing"];
                if (!is_nil_object(keepGoingValue))
                    keepGoing = keepGoingValue.as<bool>();
                dataValue = table["data"];
            }
            else if (returned.get_type() == sol::type::boolean)
            {
                keepGoing = returned.as<bool>();
                dataValue = sol::make_object(returned.lua_state(), LUASF_SOL_NIL);
            }

            if (!keepGoing || is_nil_object(dataValue))
            {
                size = 0;
                return keepGoing;
            }

            std::vector<std::byte> bytes;
            if (dataValue.get_type() == sol::type::string)
            {
                const std::string text = dataValue.as<std::string>();
                bytes.reserve(text.size());
                for (unsigned char byte : text)
                    bytes.push_back(static_cast<std::byte>(byte));
            }
            else
            {
                bytes = array_from_object<std::byte>(dataValue);
            }

            const std::size_t copyCount = std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(size));
            std::memcpy(data, bytes.data(), copyCount);
            size = copyCount;
            return true;
        };
    }
};

template <typename Signature>
std::function<Signature> function_from_object(const sol::object& object)
{
    return function_converter<Signature>::from_object(object);
}

} // namespace lua_sf
