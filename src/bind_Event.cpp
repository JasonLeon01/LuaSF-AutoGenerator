#include "bind_Event.hpp"

#include <functional>

namespace {

template <typename T>
sol::object event_get_if(sol::state_view lua, sf::Event& event)
{
    if (auto* value = event.getIf<T>())
        return sol::make_object(lua, std::ref(*value));
    return sol::make_object(lua, sol::nil);
}

template <typename T>
bool event_is(const sf::Event& event)
{
    return event.is<T>();
}

template <typename T>
const char* event_type_name()
{
    if constexpr (std::is_same_v<T, sf::Event::Closed>)
        return "Closed";
    else if constexpr (std::is_same_v<T, sf::Event::Resized>)
        return "Resized";
    else if constexpr (std::is_same_v<T, sf::Event::FocusLost>)
        return "FocusLost";
    else if constexpr (std::is_same_v<T, sf::Event::FocusGained>)
        return "FocusGained";
    else if constexpr (std::is_same_v<T, sf::Event::TextEntered>)
        return "TextEntered";
    else if constexpr (std::is_same_v<T, sf::Event::KeyPressed>)
        return "KeyPressed";
    else if constexpr (std::is_same_v<T, sf::Event::KeyReleased>)
        return "KeyReleased";
    else if constexpr (std::is_same_v<T, sf::Event::MouseWheelScrolled>)
        return "MouseWheelScrolled";
    else if constexpr (std::is_same_v<T, sf::Event::MouseButtonPressed>)
        return "MouseButtonPressed";
    else if constexpr (std::is_same_v<T, sf::Event::MouseButtonReleased>)
        return "MouseButtonReleased";
    else if constexpr (std::is_same_v<T, sf::Event::MouseMoved>)
        return "MouseMoved";
    else if constexpr (std::is_same_v<T, sf::Event::MouseMovedRaw>)
        return "MouseMovedRaw";
    else if constexpr (std::is_same_v<T, sf::Event::MouseEntered>)
        return "MouseEntered";
    else if constexpr (std::is_same_v<T, sf::Event::MouseLeft>)
        return "MouseLeft";
    else if constexpr (std::is_same_v<T, sf::Event::JoystickButtonPressed>)
        return "JoystickButtonPressed";
    else if constexpr (std::is_same_v<T, sf::Event::JoystickButtonReleased>)
        return "JoystickButtonReleased";
    else if constexpr (std::is_same_v<T, sf::Event::JoystickMoved>)
        return "JoystickMoved";
    else if constexpr (std::is_same_v<T, sf::Event::JoystickConnected>)
        return "JoystickConnected";
    else if constexpr (std::is_same_v<T, sf::Event::JoystickDisconnected>)
        return "JoystickDisconnected";
    else if constexpr (std::is_same_v<T, sf::Event::TouchBegan>)
        return "TouchBegan";
    else if constexpr (std::is_same_v<T, sf::Event::TouchMoved>)
        return "TouchMoved";
    else if constexpr (std::is_same_v<T, sf::Event::TouchEnded>)
        return "TouchEnded";
    else if constexpr (std::is_same_v<T, sf::Event::SensorChanged>)
        return "SensorChanged";
    else
        return "Unknown";
}

template <typename T>
void bind_empty_event_subtype(sol::table sf, const char* name)
{
    sf.new_usertype<T>(name, sol::constructors<T()>());
}

} // namespace

void bind_Event(sol::state_view lua)
{
    sol::table sf = lua_sf::sf_table(lua);

    bind_empty_event_subtype<sf::Event::Closed>(sf, "Event_Closed");
    bind_empty_event_subtype<sf::Event::FocusLost>(sf, "Event_FocusLost");
    bind_empty_event_subtype<sf::Event::FocusGained>(sf, "Event_FocusGained");
    bind_empty_event_subtype<sf::Event::MouseEntered>(sf, "Event_MouseEntered");
    bind_empty_event_subtype<sf::Event::MouseLeft>(sf, "Event_MouseLeft");

    auto resized = sf.new_usertype<sf::Event::Resized>("Event_Resized", sol::constructors<sf::Event::Resized()>());
    resized["size"] = &sf::Event::Resized::size;

    auto textEntered = sf.new_usertype<sf::Event::TextEntered>("Event_TextEntered", sol::constructors<sf::Event::TextEntered()>());
    textEntered.set(
        "unicode",
        sol::property(
            [](const sf::Event::TextEntered& self) { return static_cast<std::uint32_t>(self.unicode); },
            [](sf::Event::TextEntered& self, std::uint32_t value) { self.unicode = static_cast<char32_t>(value); }));

    auto keyPressed = sf.new_usertype<sf::Event::KeyPressed>("Event_KeyPressed", sol::constructors<sf::Event::KeyPressed()>());
    keyPressed["code"] = &sf::Event::KeyPressed::code;
    keyPressed["scancode"] = &sf::Event::KeyPressed::scancode;
    keyPressed["alt"] = &sf::Event::KeyPressed::alt;
    keyPressed["control"] = &sf::Event::KeyPressed::control;
    keyPressed["shift"] = &sf::Event::KeyPressed::shift;
    keyPressed["system"] = &sf::Event::KeyPressed::system;

    auto keyReleased = sf.new_usertype<sf::Event::KeyReleased>("Event_KeyReleased", sol::constructors<sf::Event::KeyReleased()>());
    keyReleased["code"] = &sf::Event::KeyReleased::code;
    keyReleased["scancode"] = &sf::Event::KeyReleased::scancode;
    keyReleased["alt"] = &sf::Event::KeyReleased::alt;
    keyReleased["control"] = &sf::Event::KeyReleased::control;
    keyReleased["shift"] = &sf::Event::KeyReleased::shift;
    keyReleased["system"] = &sf::Event::KeyReleased::system;

    auto wheelScrolled = sf.new_usertype<sf::Event::MouseWheelScrolled>(
        "Event_MouseWheelScrolled",
        sol::constructors<sf::Event::MouseWheelScrolled()>());
    wheelScrolled["wheel"] = &sf::Event::MouseWheelScrolled::wheel;
    wheelScrolled["delta"] = &sf::Event::MouseWheelScrolled::delta;
    wheelScrolled["position"] = &sf::Event::MouseWheelScrolled::position;

    auto buttonPressed = sf.new_usertype<sf::Event::MouseButtonPressed>(
        "Event_MouseButtonPressed",
        sol::constructors<sf::Event::MouseButtonPressed()>());
    buttonPressed["button"] = &sf::Event::MouseButtonPressed::button;
    buttonPressed["position"] = &sf::Event::MouseButtonPressed::position;

    auto buttonReleased = sf.new_usertype<sf::Event::MouseButtonReleased>(
        "Event_MouseButtonReleased",
        sol::constructors<sf::Event::MouseButtonReleased()>());
    buttonReleased["button"] = &sf::Event::MouseButtonReleased::button;
    buttonReleased["position"] = &sf::Event::MouseButtonReleased::position;

    auto mouseMoved = sf.new_usertype<sf::Event::MouseMoved>("Event_MouseMoved", sol::constructors<sf::Event::MouseMoved()>());
    mouseMoved["position"] = &sf::Event::MouseMoved::position;

    auto rawMoved = sf.new_usertype<sf::Event::MouseMovedRaw>("Event_MouseMovedRaw", sol::constructors<sf::Event::MouseMovedRaw()>());
    rawMoved["delta"] = &sf::Event::MouseMovedRaw::delta;

    auto joystickButtonPressed = sf.new_usertype<sf::Event::JoystickButtonPressed>(
        "Event_JoystickButtonPressed",
        sol::constructors<sf::Event::JoystickButtonPressed()>());
    joystickButtonPressed["joystickId"] = &sf::Event::JoystickButtonPressed::joystickId;
    joystickButtonPressed["button"] = &sf::Event::JoystickButtonPressed::button;

    auto joystickButtonReleased = sf.new_usertype<sf::Event::JoystickButtonReleased>(
        "Event_JoystickButtonReleased",
        sol::constructors<sf::Event::JoystickButtonReleased()>());
    joystickButtonReleased["joystickId"] = &sf::Event::JoystickButtonReleased::joystickId;
    joystickButtonReleased["button"] = &sf::Event::JoystickButtonReleased::button;

    auto joystickMoved = sf.new_usertype<sf::Event::JoystickMoved>("Event_JoystickMoved", sol::constructors<sf::Event::JoystickMoved()>());
    joystickMoved["joystickId"] = &sf::Event::JoystickMoved::joystickId;
    joystickMoved["axis"] = &sf::Event::JoystickMoved::axis;
    joystickMoved["position"] = &sf::Event::JoystickMoved::position;

    auto joystickConnected = sf.new_usertype<sf::Event::JoystickConnected>(
        "Event_JoystickConnected",
        sol::constructors<sf::Event::JoystickConnected()>());
    joystickConnected["joystickId"] = &sf::Event::JoystickConnected::joystickId;

    auto joystickDisconnected = sf.new_usertype<sf::Event::JoystickDisconnected>(
        "Event_JoystickDisconnected",
        sol::constructors<sf::Event::JoystickDisconnected()>());
    joystickDisconnected["joystickId"] = &sf::Event::JoystickDisconnected::joystickId;

    auto touchBegan = sf.new_usertype<sf::Event::TouchBegan>("Event_TouchBegan", sol::constructors<sf::Event::TouchBegan()>());
    touchBegan["finger"] = &sf::Event::TouchBegan::finger;
    touchBegan["position"] = &sf::Event::TouchBegan::position;

    auto touchMoved = sf.new_usertype<sf::Event::TouchMoved>("Event_TouchMoved", sol::constructors<sf::Event::TouchMoved()>());
    touchMoved["finger"] = &sf::Event::TouchMoved::finger;
    touchMoved["position"] = &sf::Event::TouchMoved::position;

    auto touchEnded = sf.new_usertype<sf::Event::TouchEnded>("Event_TouchEnded", sol::constructors<sf::Event::TouchEnded()>());
    touchEnded["finger"] = &sf::Event::TouchEnded::finger;
    touchEnded["position"] = &sf::Event::TouchEnded::position;

    auto sensorChanged = sf.new_usertype<sf::Event::SensorChanged>("Event_SensorChanged", sol::constructors<sf::Event::SensorChanged()>());
    sensorChanged["type"] = &sf::Event::SensorChanged::type;
    sensorChanged["value"] = &sf::Event::SensorChanged::value;

    auto event = sf.new_usertype<sf::Event>("Event", sol::no_constructor);
    event.set_function(
        "new",
        sol::factories(
            [](const sf::Event::Closed& value) { return sf::Event(value); },
            [](const sf::Event::Resized& value) { return sf::Event(value); },
            [](const sf::Event::FocusLost& value) { return sf::Event(value); },
            [](const sf::Event::FocusGained& value) { return sf::Event(value); },
            [](const sf::Event::TextEntered& value) { return sf::Event(value); },
            [](const sf::Event::KeyPressed& value) { return sf::Event(value); },
            [](const sf::Event::KeyReleased& value) { return sf::Event(value); },
            [](const sf::Event::MouseWheelScrolled& value) { return sf::Event(value); },
            [](const sf::Event::MouseButtonPressed& value) { return sf::Event(value); },
            [](const sf::Event::MouseButtonReleased& value) { return sf::Event(value); },
            [](const sf::Event::MouseMoved& value) { return sf::Event(value); },
            [](const sf::Event::MouseMovedRaw& value) { return sf::Event(value); },
            [](const sf::Event::MouseEntered& value) { return sf::Event(value); },
            [](const sf::Event::MouseLeft& value) { return sf::Event(value); },
            [](const sf::Event::JoystickButtonPressed& value) { return sf::Event(value); },
            [](const sf::Event::JoystickButtonReleased& value) { return sf::Event(value); },
            [](const sf::Event::JoystickMoved& value) { return sf::Event(value); },
            [](const sf::Event::JoystickConnected& value) { return sf::Event(value); },
            [](const sf::Event::JoystickDisconnected& value) { return sf::Event(value); },
            [](const sf::Event::TouchBegan& value) { return sf::Event(value); },
            [](const sf::Event::TouchMoved& value) { return sf::Event(value); },
            [](const sf::Event::TouchEnded& value) { return sf::Event(value); },
            [](const sf::Event::SensorChanged& value) { return sf::Event(value); }));

    event.set_function("type", [](const sf::Event& self) {
        return self.visit([](const auto& value) { return event_type_name<std::decay_t<decltype(value)>>(); });
    });
    event.set_function("get", [lua](sf::Event& self) -> sol::object {
        return self.visit([lua](auto& value) -> sol::object { return sol::make_object(lua, std::ref(value)); });
    });

    event.set_function("isClosed", &event_is<sf::Event::Closed>);
    event.set_function("isResized", &event_is<sf::Event::Resized>);
    event.set_function("isFocusLost", &event_is<sf::Event::FocusLost>);
    event.set_function("isFocusGained", &event_is<sf::Event::FocusGained>);
    event.set_function("isTextEntered", &event_is<sf::Event::TextEntered>);
    event.set_function("isKeyPressed", &event_is<sf::Event::KeyPressed>);
    event.set_function("isKeyReleased", &event_is<sf::Event::KeyReleased>);
    event.set_function("isMouseWheelScrolled", &event_is<sf::Event::MouseWheelScrolled>);
    event.set_function("isMouseButtonPressed", &event_is<sf::Event::MouseButtonPressed>);
    event.set_function("isMouseButtonReleased", &event_is<sf::Event::MouseButtonReleased>);
    event.set_function("isMouseMoved", &event_is<sf::Event::MouseMoved>);
    event.set_function("isMouseMovedRaw", &event_is<sf::Event::MouseMovedRaw>);
    event.set_function("isMouseEntered", &event_is<sf::Event::MouseEntered>);
    event.set_function("isMouseLeft", &event_is<sf::Event::MouseLeft>);
    event.set_function("isJoystickButtonPressed", &event_is<sf::Event::JoystickButtonPressed>);
    event.set_function("isJoystickButtonReleased", &event_is<sf::Event::JoystickButtonReleased>);
    event.set_function("isJoystickMoved", &event_is<sf::Event::JoystickMoved>);
    event.set_function("isJoystickConnected", &event_is<sf::Event::JoystickConnected>);
    event.set_function("isJoystickDisconnected", &event_is<sf::Event::JoystickDisconnected>);
    event.set_function("isTouchBegan", &event_is<sf::Event::TouchBegan>);
    event.set_function("isTouchMoved", &event_is<sf::Event::TouchMoved>);
    event.set_function("isTouchEnded", &event_is<sf::Event::TouchEnded>);
    event.set_function("isSensorChanged", &event_is<sf::Event::SensorChanged>);

    event.set_function("getIfClosed", [lua](sf::Event& self) { return event_get_if<sf::Event::Closed>(lua, self); });
    event.set_function("getIfResized", [lua](sf::Event& self) { return event_get_if<sf::Event::Resized>(lua, self); });
    event.set_function("getIfFocusLost", [lua](sf::Event& self) { return event_get_if<sf::Event::FocusLost>(lua, self); });
    event.set_function("getIfFocusGained", [lua](sf::Event& self) { return event_get_if<sf::Event::FocusGained>(lua, self); });
    event.set_function("getIfTextEntered", [lua](sf::Event& self) { return event_get_if<sf::Event::TextEntered>(lua, self); });
    event.set_function("getIfKeyPressed", [lua](sf::Event& self) { return event_get_if<sf::Event::KeyPressed>(lua, self); });
    event.set_function("getIfKeyReleased", [lua](sf::Event& self) { return event_get_if<sf::Event::KeyReleased>(lua, self); });
    event.set_function("getIfMouseWheelScrolled", [lua](sf::Event& self) { return event_get_if<sf::Event::MouseWheelScrolled>(lua, self); });
    event.set_function("getIfMouseButtonPressed", [lua](sf::Event& self) { return event_get_if<sf::Event::MouseButtonPressed>(lua, self); });
    event.set_function("getIfMouseButtonReleased", [lua](sf::Event& self) { return event_get_if<sf::Event::MouseButtonReleased>(lua, self); });
    event.set_function("getIfMouseMoved", [lua](sf::Event& self) { return event_get_if<sf::Event::MouseMoved>(lua, self); });
    event.set_function("getIfMouseMovedRaw", [lua](sf::Event& self) { return event_get_if<sf::Event::MouseMovedRaw>(lua, self); });
    event.set_function("getIfMouseEntered", [lua](sf::Event& self) { return event_get_if<sf::Event::MouseEntered>(lua, self); });
    event.set_function("getIfMouseLeft", [lua](sf::Event& self) { return event_get_if<sf::Event::MouseLeft>(lua, self); });
    event.set_function("getIfJoystickButtonPressed", [lua](sf::Event& self) { return event_get_if<sf::Event::JoystickButtonPressed>(lua, self); });
    event.set_function("getIfJoystickButtonReleased", [lua](sf::Event& self) { return event_get_if<sf::Event::JoystickButtonReleased>(lua, self); });
    event.set_function("getIfJoystickMoved", [lua](sf::Event& self) { return event_get_if<sf::Event::JoystickMoved>(lua, self); });
    event.set_function("getIfJoystickConnected", [lua](sf::Event& self) { return event_get_if<sf::Event::JoystickConnected>(lua, self); });
    event.set_function("getIfJoystickDisconnected", [lua](sf::Event& self) { return event_get_if<sf::Event::JoystickDisconnected>(lua, self); });
    event.set_function("getIfTouchBegan", [lua](sf::Event& self) { return event_get_if<sf::Event::TouchBegan>(lua, self); });
    event.set_function("getIfTouchMoved", [lua](sf::Event& self) { return event_get_if<sf::Event::TouchMoved>(lua, self); });
    event.set_function("getIfTouchEnded", [lua](sf::Event& self) { return event_get_if<sf::Event::TouchEnded>(lua, self); });
    event.set_function("getIfSensorChanged", [lua](sf::Event& self) { return event_get_if<sf::Event::SensorChanged>(lua, self); });
}
