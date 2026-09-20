#include "bind_Event.hpp"

#include <functional>

namespace {

template <typename T>
bool event_is(const sf::Event& event) {
    return event.is<T>();
}

template <typename T>
const char* event_type_name() {
    if constexpr (std::is_same_v<T, sf::Event::Closed>) {
        return "Closed";
    } else if constexpr (std::is_same_v<T, sf::Event::Resized>) {
        return "Resized";
    } else if constexpr (std::is_same_v<T, sf::Event::FocusLost>) {
        return "FocusLost";
    } else if constexpr (std::is_same_v<T, sf::Event::FocusGained>) {
        return "FocusGained";
    } else if constexpr (std::is_same_v<T, sf::Event::TextEntered>) {
        return "TextEntered";
    } else if constexpr (std::is_same_v<T, sf::Event::KeyPressed>) {
        return "KeyPressed";
    } else if constexpr (std::is_same_v<T, sf::Event::KeyReleased>) {
        return "KeyReleased";
    } else if constexpr (std::is_same_v<T, sf::Event::MouseWheelScrolled>) {
        return "MouseWheelScrolled";
    } else if constexpr (std::is_same_v<T, sf::Event::MouseButtonPressed>) {
        return "MouseButtonPressed";
    } else if constexpr (std::is_same_v<T, sf::Event::MouseButtonReleased>) {
        return "MouseButtonReleased";
    } else if constexpr (std::is_same_v<T, sf::Event::MouseMoved>) {
        return "MouseMoved";
    } else if constexpr (std::is_same_v<T, sf::Event::MouseMovedRaw>) {
        return "MouseMovedRaw";
    } else if constexpr (std::is_same_v<T, sf::Event::MouseEntered>) {
        return "MouseEntered";
    } else if constexpr (std::is_same_v<T, sf::Event::MouseLeft>) {
        return "MouseLeft";
    } else if constexpr (std::is_same_v<T, sf::Event::JoystickButtonPressed>) {
        return "JoystickButtonPressed";
    } else if constexpr (std::is_same_v<T, sf::Event::JoystickButtonReleased>) {
        return "JoystickButtonReleased";
    } else if constexpr (std::is_same_v<T, sf::Event::JoystickMoved>) {
        return "JoystickMoved";
    } else if constexpr (std::is_same_v<T, sf::Event::JoystickConnected>) {
        return "JoystickConnected";
    } else if constexpr (std::is_same_v<T, sf::Event::JoystickDisconnected>) {
        return "JoystickDisconnected";
    } else if constexpr (std::is_same_v<T, sf::Event::TouchBegan>) {
        return "TouchBegan";
    } else if constexpr (std::is_same_v<T, sf::Event::TouchMoved>) {
        return "TouchMoved";
    } else if constexpr (std::is_same_v<T, sf::Event::TouchEnded>) {
        return "TouchEnded";
    } else if constexpr (std::is_same_v<T, sf::Event::SensorChanged>) {
        return "SensorChanged";
    } else {
        return "Unknown";
    }
}

template <typename T>
auto bind_event_subtype(lua_glue::Table sf, const char* name) {
    auto type = lua_glue::BindStruct<T>(sf, name);
    lua_glue::BindConstructor<T>(type);
    const std::string owner = "sf." + std::string(name);
    const std::string signature = "fun(self: " + owner + "): " + owner;
    lua_sf::stub::function(owner.c_str(), "copy", signature.c_str());
    lua_sf::stub::function(owner.c_str(), "deepcopy", signature.c_str());
    return type;
}

template <typename T>
void bind_empty_event_subtype(lua_glue::Table sf, const char* name) {
    bind_event_subtype<T>(sf, name);
}

template <typename T, typename UserType>
void bind_event_get_if(UserType& type, lua_glue::StateView, const char* name) {
    lua_glue::BindCallable(
        type, name,
        [](sf::Event& self) {
            return self.getIf<T>();
        },
        lua_glue::ReturnPolicy::ReferenceInternal);
}

template <typename UserType, typename EventType, typename FieldType>
void bind_event_field(UserType& type, const char* name,
                      FieldType EventType::* field) {
    if constexpr (lua_sf::is_lua_integral_v<FieldType>) {
        using LuaFieldType = std::conditional_t<std::is_signed_v<FieldType>,
                                                std::int64_t, std::uint64_t>;
        lua_glue::BindProperty(
            type, name,
            [field](const EventType& self) {
                return static_cast<LuaFieldType>(self.*field);
            },
            [field](EventType& self, lua_sf::LuaIntegral<FieldType> value) {
                self.*field = value.value();
            });
    } else {
        lua_glue::BindAttr<FieldType>(type, name, field);
    }
}

}  // namespace

void bind_Event(lua_glue::StateView lua) {
    lua_glue::Table sf = lua_sf::sf_table(lua);

    LUASF_STUB_CLASS("sf.Event_Closed");
    LUASF_STUB_FUNCTION("sf.Event_Closed", "new", "fun(): sf.Event_Closed");
    bind_empty_event_subtype<sf::Event::Closed>(sf, "Event_Closed");

    LUASF_STUB_CLASS("sf.Event_FocusLost");
    LUASF_STUB_FUNCTION("sf.Event_FocusLost", "new",
                        "fun(): sf.Event_FocusLost");
    bind_empty_event_subtype<sf::Event::FocusLost>(sf, "Event_FocusLost");

    LUASF_STUB_CLASS("sf.Event_FocusGained");
    LUASF_STUB_FUNCTION("sf.Event_FocusGained", "new",
                        "fun(): sf.Event_FocusGained");
    bind_empty_event_subtype<sf::Event::FocusGained>(sf, "Event_FocusGained");

    LUASF_STUB_CLASS("sf.Event_MouseEntered");
    LUASF_STUB_FUNCTION("sf.Event_MouseEntered", "new",
                        "fun(): sf.Event_MouseEntered");
    bind_empty_event_subtype<sf::Event::MouseEntered>(sf, "Event_MouseEntered");

    LUASF_STUB_CLASS("sf.Event_MouseLeft");
    LUASF_STUB_FUNCTION("sf.Event_MouseLeft", "new",
                        "fun(): sf.Event_MouseLeft");
    bind_empty_event_subtype<sf::Event::MouseLeft>(sf, "Event_MouseLeft");

    LUASF_STUB_CLASS("sf.Event_Resized");
    LUASF_STUB_FIELD("size", "sf.Vector2u");
    LUASF_STUB_FUNCTION("sf.Event_Resized", "new", "fun(): sf.Event_Resized");
    auto resized = bind_event_subtype<sf::Event::Resized>(sf, "Event_Resized");
    bind_event_field(resized, "size", &sf::Event::Resized::size);

    LUASF_STUB_CLASS("sf.Event_TextEntered");
    LUASF_STUB_FIELD("unicode", "integer");
    LUASF_STUB_FUNCTION("sf.Event_TextEntered", "new",
                        "fun(): sf.Event_TextEntered");
    auto textEntered =
        bind_event_subtype<sf::Event::TextEntered>(sf, "Event_TextEntered");
    bind_event_field(textEntered, "unicode", &sf::Event::TextEntered::unicode);

    LUASF_STUB_CLASS("sf.Event_KeyPressed");
    LUASF_STUB_FIELD("code", "sf.Keyboard.Key");
    LUASF_STUB_FIELD("scancode", "sf.Keyboard.Scancode");
    LUASF_STUB_FIELD("alt", "boolean");
    LUASF_STUB_FIELD("control", "boolean");
    LUASF_STUB_FIELD("shift", "boolean");
    LUASF_STUB_FIELD("system", "boolean");
    LUASF_STUB_FUNCTION("sf.Event_KeyPressed", "new",
                        "fun(): sf.Event_KeyPressed");
    auto keyPressed =
        bind_event_subtype<sf::Event::KeyPressed>(sf, "Event_KeyPressed");
    bind_event_field(keyPressed, "code", &sf::Event::KeyPressed::code);
    bind_event_field(keyPressed, "scancode", &sf::Event::KeyPressed::scancode);
    bind_event_field(keyPressed, "alt", &sf::Event::KeyPressed::alt);
    bind_event_field(keyPressed, "control", &sf::Event::KeyPressed::control);
    bind_event_field(keyPressed, "shift", &sf::Event::KeyPressed::shift);
    bind_event_field(keyPressed, "system", &sf::Event::KeyPressed::system);

    LUASF_STUB_CLASS("sf.Event_KeyReleased");
    LUASF_STUB_FIELD("code", "sf.Keyboard.Key");
    LUASF_STUB_FIELD("scancode", "sf.Keyboard.Scancode");
    LUASF_STUB_FIELD("alt", "boolean");
    LUASF_STUB_FIELD("control", "boolean");
    LUASF_STUB_FIELD("shift", "boolean");
    LUASF_STUB_FIELD("system", "boolean");
    LUASF_STUB_FUNCTION("sf.Event_KeyReleased", "new",
                        "fun(): sf.Event_KeyReleased");
    auto keyReleased =
        bind_event_subtype<sf::Event::KeyReleased>(sf, "Event_KeyReleased");
    bind_event_field(keyReleased, "code", &sf::Event::KeyReleased::code);
    bind_event_field(keyReleased, "scancode",
                     &sf::Event::KeyReleased::scancode);
    bind_event_field(keyReleased, "alt", &sf::Event::KeyReleased::alt);
    bind_event_field(keyReleased, "control", &sf::Event::KeyReleased::control);
    bind_event_field(keyReleased, "shift", &sf::Event::KeyReleased::shift);
    bind_event_field(keyReleased, "system", &sf::Event::KeyReleased::system);

    LUASF_STUB_CLASS("sf.Event_MouseWheelScrolled");
    LUASF_STUB_FIELD("wheel", "sf.Mouse.Wheel");
    LUASF_STUB_FIELD("delta", "number");
    LUASF_STUB_FIELD("position", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_MouseWheelScrolled", "new",
                        "fun(): sf.Event_MouseWheelScrolled");
    auto wheelScrolled = bind_event_subtype<sf::Event::MouseWheelScrolled>(
        sf, "Event_MouseWheelScrolled");
    bind_event_field(wheelScrolled, "wheel",
                     &sf::Event::MouseWheelScrolled::wheel);
    bind_event_field(wheelScrolled, "delta",
                     &sf::Event::MouseWheelScrolled::delta);
    bind_event_field(wheelScrolled, "position",
                     &sf::Event::MouseWheelScrolled::position);

    LUASF_STUB_CLASS("sf.Event_MouseButtonPressed");
    LUASF_STUB_FIELD("button", "sf.Mouse.Button");
    LUASF_STUB_FIELD("position", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_MouseButtonPressed", "new",
                        "fun(): sf.Event_MouseButtonPressed");
    auto buttonPressed = bind_event_subtype<sf::Event::MouseButtonPressed>(
        sf, "Event_MouseButtonPressed");
    bind_event_field(buttonPressed, "button",
                     &sf::Event::MouseButtonPressed::button);
    bind_event_field(buttonPressed, "position",
                     &sf::Event::MouseButtonPressed::position);

    LUASF_STUB_CLASS("sf.Event_MouseButtonReleased");
    LUASF_STUB_FIELD("button", "sf.Mouse.Button");
    LUASF_STUB_FIELD("position", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_MouseButtonReleased", "new",
                        "fun(): sf.Event_MouseButtonReleased");
    auto buttonReleased = bind_event_subtype<sf::Event::MouseButtonReleased>(
        sf, "Event_MouseButtonReleased");
    bind_event_field(buttonReleased, "button",
                     &sf::Event::MouseButtonReleased::button);
    bind_event_field(buttonReleased, "position",
                     &sf::Event::MouseButtonReleased::position);

    LUASF_STUB_CLASS("sf.Event_MouseMoved");
    LUASF_STUB_FIELD("position", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_MouseMoved", "new",
                        "fun(): sf.Event_MouseMoved");
    auto mouseMoved =
        bind_event_subtype<sf::Event::MouseMoved>(sf, "Event_MouseMoved");
    bind_event_field(mouseMoved, "position", &sf::Event::MouseMoved::position);

    LUASF_STUB_CLASS("sf.Event_MouseMovedRaw");
    LUASF_STUB_FIELD("delta", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_MouseMovedRaw", "new",
                        "fun(): sf.Event_MouseMovedRaw");
    auto rawMoved =
        bind_event_subtype<sf::Event::MouseMovedRaw>(sf, "Event_MouseMovedRaw");
    bind_event_field(rawMoved, "delta", &sf::Event::MouseMovedRaw::delta);

    LUASF_STUB_CLASS("sf.Event_JoystickButtonPressed");
    LUASF_STUB_FIELD("joystickId", "integer");
    LUASF_STUB_FIELD("button", "integer");
    LUASF_STUB_FUNCTION("sf.Event_JoystickButtonPressed", "new",
                        "fun(): sf.Event_JoystickButtonPressed");
    auto joystickButtonPressed =
        bind_event_subtype<sf::Event::JoystickButtonPressed>(
            sf, "Event_JoystickButtonPressed");
    bind_event_field(joystickButtonPressed, "joystickId",
                     &sf::Event::JoystickButtonPressed::joystickId);
    bind_event_field(joystickButtonPressed, "button",
                     &sf::Event::JoystickButtonPressed::button);

    LUASF_STUB_CLASS("sf.Event_JoystickButtonReleased");
    LUASF_STUB_FIELD("joystickId", "integer");
    LUASF_STUB_FIELD("button", "integer");
    LUASF_STUB_FUNCTION("sf.Event_JoystickButtonReleased", "new",
                        "fun(): sf.Event_JoystickButtonReleased");
    auto joystickButtonReleased =
        bind_event_subtype<sf::Event::JoystickButtonReleased>(
            sf, "Event_JoystickButtonReleased");
    bind_event_field(joystickButtonReleased, "joystickId",
                     &sf::Event::JoystickButtonReleased::joystickId);
    bind_event_field(joystickButtonReleased, "button",
                     &sf::Event::JoystickButtonReleased::button);

    LUASF_STUB_CLASS("sf.Event_JoystickMoved");
    LUASF_STUB_FIELD("joystickId", "integer");
    LUASF_STUB_FIELD("axis", "sf.Joystick.Axis");
    LUASF_STUB_FIELD("position", "number");
    LUASF_STUB_FUNCTION("sf.Event_JoystickMoved", "new",
                        "fun(): sf.Event_JoystickMoved");
    auto joystickMoved =
        bind_event_subtype<sf::Event::JoystickMoved>(sf, "Event_JoystickMoved");
    bind_event_field(joystickMoved, "joystickId",
                     &sf::Event::JoystickMoved::joystickId);
    bind_event_field(joystickMoved, "axis", &sf::Event::JoystickMoved::axis);
    bind_event_field(joystickMoved, "position",
                     &sf::Event::JoystickMoved::position);

    LUASF_STUB_CLASS("sf.Event_JoystickConnected");
    LUASF_STUB_FIELD("joystickId", "integer");
    LUASF_STUB_FUNCTION("sf.Event_JoystickConnected", "new",
                        "fun(): sf.Event_JoystickConnected");
    auto joystickConnected = bind_event_subtype<sf::Event::JoystickConnected>(
        sf, "Event_JoystickConnected");
    bind_event_field(joystickConnected, "joystickId",
                     &sf::Event::JoystickConnected::joystickId);

    LUASF_STUB_CLASS("sf.Event_JoystickDisconnected");
    LUASF_STUB_FIELD("joystickId", "integer");
    LUASF_STUB_FUNCTION("sf.Event_JoystickDisconnected", "new",
                        "fun(): sf.Event_JoystickDisconnected");
    auto joystickDisconnected =
        bind_event_subtype<sf::Event::JoystickDisconnected>(
            sf, "Event_JoystickDisconnected");
    bind_event_field(joystickDisconnected, "joystickId",
                     &sf::Event::JoystickDisconnected::joystickId);

    LUASF_STUB_CLASS("sf.Event_TouchBegan");
    LUASF_STUB_FIELD("finger", "integer");
    LUASF_STUB_FIELD("position", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_TouchBegan", "new",
                        "fun(): sf.Event_TouchBegan");
    auto touchBegan =
        bind_event_subtype<sf::Event::TouchBegan>(sf, "Event_TouchBegan");
    bind_event_field(touchBegan, "finger", &sf::Event::TouchBegan::finger);
    bind_event_field(touchBegan, "position", &sf::Event::TouchBegan::position);

    LUASF_STUB_CLASS("sf.Event_TouchMoved");
    LUASF_STUB_FIELD("finger", "integer");
    LUASF_STUB_FIELD("position", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_TouchMoved", "new",
                        "fun(): sf.Event_TouchMoved");
    auto touchMoved =
        bind_event_subtype<sf::Event::TouchMoved>(sf, "Event_TouchMoved");
    bind_event_field(touchMoved, "finger", &sf::Event::TouchMoved::finger);
    bind_event_field(touchMoved, "position", &sf::Event::TouchMoved::position);

    LUASF_STUB_CLASS("sf.Event_TouchEnded");
    LUASF_STUB_FIELD("finger", "integer");
    LUASF_STUB_FIELD("position", "sf.Vector2i");
    LUASF_STUB_FUNCTION("sf.Event_TouchEnded", "new",
                        "fun(): sf.Event_TouchEnded");
    auto touchEnded =
        bind_event_subtype<sf::Event::TouchEnded>(sf, "Event_TouchEnded");
    bind_event_field(touchEnded, "finger", &sf::Event::TouchEnded::finger);
    bind_event_field(touchEnded, "position", &sf::Event::TouchEnded::position);

    LUASF_STUB_CLASS("sf.Event_SensorChanged");
    LUASF_STUB_FIELD("type", "sf.Sensor.Type");
    LUASF_STUB_FIELD("value", "sf.Vector3f");
    LUASF_STUB_FUNCTION("sf.Event_SensorChanged", "new",
                        "fun(): sf.Event_SensorChanged");
    auto sensorChanged =
        bind_event_subtype<sf::Event::SensorChanged>(sf, "Event_SensorChanged");
    bind_event_field(sensorChanged, "type", &sf::Event::SensorChanged::type);
    bind_event_field(sensorChanged, "value", &sf::Event::SensorChanged::value);

    LUASF_STUB_CLASS("sf.Event");
    LUASF_STUB_FUNCTION("sf.Event", "new",
                        "fun(value: sf.Event_Closed): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_Resized): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_FocusLost): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_FocusGained): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_TextEntered): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_KeyPressed): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_KeyReleased): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_MouseWheelScrolled): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_MouseButtonPressed): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_MouseButtonReleased): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_MouseMoved): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_MouseMovedRaw): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_MouseEntered): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_MouseLeft): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_JoystickButtonPressed): sf.Event");
    LUASF_STUB_OVERLOAD(
        "sf.Event", "new",
        "fun(value: sf.Event_JoystickButtonReleased): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_JoystickMoved): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_JoystickConnected): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_JoystickDisconnected): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_TouchBegan): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_TouchMoved): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_TouchEnded): sf.Event");
    LUASF_STUB_OVERLOAD("sf.Event", "new",
                        "fun(value: sf.Event_SensorChanged): sf.Event");
    auto event = lua_glue::BindStruct<sf::Event>(sf, "Event");
    lua_glue::BindConstructor<sf::Event, const sf::Event::Closed&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::Resized&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::FocusLost&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::FocusGained&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::TextEntered&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::KeyPressed&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::KeyReleased&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::MouseWheelScrolled&>(
        event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::MouseButtonPressed&>(
        event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::MouseButtonReleased&>(
        event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::MouseMoved&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::MouseMovedRaw&>(
        event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::MouseEntered&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::MouseLeft&>(event);
    lua_glue::BindConstructor<sf::Event,
                              const sf::Event::JoystickButtonPressed&>(event);
    lua_glue::BindConstructor<sf::Event,
                              const sf::Event::JoystickButtonReleased&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::JoystickMoved&>(
        event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::JoystickConnected&>(
        event);
    lua_glue::BindConstructor<sf::Event,
                              const sf::Event::JoystickDisconnected&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::TouchBegan&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::TouchMoved&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::TouchEnded&>(event);
    lua_glue::BindConstructor<sf::Event, const sf::Event::SensorChanged&>(
        event);
    LUASF_STUB_FUNCTION("sf.Event", "copy", "fun(self: sf.Event): sf.Event");
    LUASF_STUB_FUNCTION("sf.Event", "deepcopy",
                        "fun(self: sf.Event): sf.Event");

    LUASF_STUB_FUNCTION("sf.Event", "type", "fun(self: sf.Event): string");
    event.set_function("type", [](const sf::Event& self) {
        return self.visit([](const auto& value) {
            return event_type_name<std::decay_t<decltype(value)>>();
        });
    });
    LUASF_STUB_FUNCTION("sf.Event", "get", "fun(self: sf.Event): any");
    lua_glue::BindCallable(
        event, "get",
        [lua](sf::Event& self) -> lua_glue::Object {
            return self.visit([lua](auto& value) -> lua_glue::Object {
                return lua_glue::MakeObject(lua, std::ref(value));
            });
        },
        lua_glue::ReturnPolicy::ReferenceInternal);

    LUASF_STUB_FUNCTION("sf.Event", "isClosed", "fun(self: sf.Event): boolean");
    event.set_function("isClosed", &event_is<sf::Event::Closed>);
    LUASF_STUB_FUNCTION("sf.Event", "isResized",
                        "fun(self: sf.Event): boolean");
    event.set_function("isResized", &event_is<sf::Event::Resized>);
    LUASF_STUB_FUNCTION("sf.Event", "isFocusLost",
                        "fun(self: sf.Event): boolean");
    event.set_function("isFocusLost", &event_is<sf::Event::FocusLost>);
    LUASF_STUB_FUNCTION("sf.Event", "isFocusGained",
                        "fun(self: sf.Event): boolean");
    event.set_function("isFocusGained", &event_is<sf::Event::FocusGained>);
    LUASF_STUB_FUNCTION("sf.Event", "isTextEntered",
                        "fun(self: sf.Event): boolean");
    event.set_function("isTextEntered", &event_is<sf::Event::TextEntered>);
    LUASF_STUB_FUNCTION("sf.Event", "isKeyPressed",
                        "fun(self: sf.Event): boolean");
    event.set_function("isKeyPressed", &event_is<sf::Event::KeyPressed>);
    LUASF_STUB_FUNCTION("sf.Event", "isKeyReleased",
                        "fun(self: sf.Event): boolean");
    event.set_function("isKeyReleased", &event_is<sf::Event::KeyReleased>);
    LUASF_STUB_FUNCTION("sf.Event", "isMouseWheelScrolled",
                        "fun(self: sf.Event): boolean");
    event.set_function("isMouseWheelScrolled",
                       &event_is<sf::Event::MouseWheelScrolled>);
    LUASF_STUB_FUNCTION("sf.Event", "isMouseButtonPressed",
                        "fun(self: sf.Event): boolean");
    event.set_function("isMouseButtonPressed",
                       &event_is<sf::Event::MouseButtonPressed>);
    LUASF_STUB_FUNCTION("sf.Event", "isMouseButtonReleased",
                        "fun(self: sf.Event): boolean");
    event.set_function("isMouseButtonReleased",
                       &event_is<sf::Event::MouseButtonReleased>);
    LUASF_STUB_FUNCTION("sf.Event", "isMouseMoved",
                        "fun(self: sf.Event): boolean");
    event.set_function("isMouseMoved", &event_is<sf::Event::MouseMoved>);
    LUASF_STUB_FUNCTION("sf.Event", "isMouseMovedRaw",
                        "fun(self: sf.Event): boolean");
    event.set_function("isMouseMovedRaw", &event_is<sf::Event::MouseMovedRaw>);
    LUASF_STUB_FUNCTION("sf.Event", "isMouseEntered",
                        "fun(self: sf.Event): boolean");
    event.set_function("isMouseEntered", &event_is<sf::Event::MouseEntered>);
    LUASF_STUB_FUNCTION("sf.Event", "isMouseLeft",
                        "fun(self: sf.Event): boolean");
    event.set_function("isMouseLeft", &event_is<sf::Event::MouseLeft>);
    LUASF_STUB_FUNCTION("sf.Event", "isJoystickButtonPressed",
                        "fun(self: sf.Event): boolean");
    event.set_function("isJoystickButtonPressed",
                       &event_is<sf::Event::JoystickButtonPressed>);
    LUASF_STUB_FUNCTION("sf.Event", "isJoystickButtonReleased",
                        "fun(self: sf.Event): boolean");
    event.set_function("isJoystickButtonReleased",
                       &event_is<sf::Event::JoystickButtonReleased>);
    LUASF_STUB_FUNCTION("sf.Event", "isJoystickMoved",
                        "fun(self: sf.Event): boolean");
    event.set_function("isJoystickMoved", &event_is<sf::Event::JoystickMoved>);
    LUASF_STUB_FUNCTION("sf.Event", "isJoystickConnected",
                        "fun(self: sf.Event): boolean");
    event.set_function("isJoystickConnected",
                       &event_is<sf::Event::JoystickConnected>);
    LUASF_STUB_FUNCTION("sf.Event", "isJoystickDisconnected",
                        "fun(self: sf.Event): boolean");
    event.set_function("isJoystickDisconnected",
                       &event_is<sf::Event::JoystickDisconnected>);
    LUASF_STUB_FUNCTION("sf.Event", "isTouchBegan",
                        "fun(self: sf.Event): boolean");
    event.set_function("isTouchBegan", &event_is<sf::Event::TouchBegan>);
    LUASF_STUB_FUNCTION("sf.Event", "isTouchMoved",
                        "fun(self: sf.Event): boolean");
    event.set_function("isTouchMoved", &event_is<sf::Event::TouchMoved>);
    LUASF_STUB_FUNCTION("sf.Event", "isTouchEnded",
                        "fun(self: sf.Event): boolean");
    event.set_function("isTouchEnded", &event_is<sf::Event::TouchEnded>);
    LUASF_STUB_FUNCTION("sf.Event", "isSensorChanged",
                        "fun(self: sf.Event): boolean");
    event.set_function("isSensorChanged", &event_is<sf::Event::SensorChanged>);

    LUASF_STUB_FUNCTION("sf.Event", "getIfClosed",
                        "fun(self: sf.Event): sf.Event_Closed|nil");
    bind_event_get_if<sf::Event::Closed>(event, lua, "getIfClosed");
    LUASF_STUB_FUNCTION("sf.Event", "getIfResized",
                        "fun(self: sf.Event): sf.Event_Resized|nil");
    bind_event_get_if<sf::Event::Resized>(event, lua, "getIfResized");
    LUASF_STUB_FUNCTION("sf.Event", "getIfFocusLost",
                        "fun(self: sf.Event): sf.Event_FocusLost|nil");
    bind_event_get_if<sf::Event::FocusLost>(event, lua, "getIfFocusLost");
    LUASF_STUB_FUNCTION("sf.Event", "getIfFocusGained",
                        "fun(self: sf.Event): sf.Event_FocusGained|nil");
    bind_event_get_if<sf::Event::FocusGained>(event, lua, "getIfFocusGained");
    LUASF_STUB_FUNCTION("sf.Event", "getIfTextEntered",
                        "fun(self: sf.Event): sf.Event_TextEntered|nil");
    bind_event_get_if<sf::Event::TextEntered>(event, lua, "getIfTextEntered");
    LUASF_STUB_FUNCTION("sf.Event", "getIfKeyPressed",
                        "fun(self: sf.Event): sf.Event_KeyPressed|nil");
    bind_event_get_if<sf::Event::KeyPressed>(event, lua, "getIfKeyPressed");
    LUASF_STUB_FUNCTION("sf.Event", "getIfKeyReleased",
                        "fun(self: sf.Event): sf.Event_KeyReleased|nil");
    bind_event_get_if<sf::Event::KeyReleased>(event, lua, "getIfKeyReleased");
    LUASF_STUB_FUNCTION("sf.Event", "getIfMouseWheelScrolled",
                        "fun(self: sf.Event): sf.Event_MouseWheelScrolled|nil");
    bind_event_get_if<sf::Event::MouseWheelScrolled>(event, lua,
                                                     "getIfMouseWheelScrolled");
    LUASF_STUB_FUNCTION("sf.Event", "getIfMouseButtonPressed",
                        "fun(self: sf.Event): sf.Event_MouseButtonPressed|nil");
    bind_event_get_if<sf::Event::MouseButtonPressed>(event, lua,
                                                     "getIfMouseButtonPressed");
    LUASF_STUB_FUNCTION(
        "sf.Event", "getIfMouseButtonReleased",
        "fun(self: sf.Event): sf.Event_MouseButtonReleased|nil");
    bind_event_get_if<sf::Event::MouseButtonReleased>(
        event, lua, "getIfMouseButtonReleased");
    LUASF_STUB_FUNCTION("sf.Event", "getIfMouseMoved",
                        "fun(self: sf.Event): sf.Event_MouseMoved|nil");
    bind_event_get_if<sf::Event::MouseMoved>(event, lua, "getIfMouseMoved");
    LUASF_STUB_FUNCTION("sf.Event", "getIfMouseMovedRaw",
                        "fun(self: sf.Event): sf.Event_MouseMovedRaw|nil");
    bind_event_get_if<sf::Event::MouseMovedRaw>(event, lua,
                                                "getIfMouseMovedRaw");
    LUASF_STUB_FUNCTION("sf.Event", "getIfMouseEntered",
                        "fun(self: sf.Event): sf.Event_MouseEntered|nil");
    bind_event_get_if<sf::Event::MouseEntered>(event, lua, "getIfMouseEntered");
    LUASF_STUB_FUNCTION("sf.Event", "getIfMouseLeft",
                        "fun(self: sf.Event): sf.Event_MouseLeft|nil");
    bind_event_get_if<sf::Event::MouseLeft>(event, lua, "getIfMouseLeft");
    LUASF_STUB_FUNCTION(
        "sf.Event", "getIfJoystickButtonPressed",
        "fun(self: sf.Event): sf.Event_JoystickButtonPressed|nil");
    bind_event_get_if<sf::Event::JoystickButtonPressed>(
        event, lua, "getIfJoystickButtonPressed");
    LUASF_STUB_FUNCTION(
        "sf.Event", "getIfJoystickButtonReleased",
        "fun(self: sf.Event): sf.Event_JoystickButtonReleased|nil");
    bind_event_get_if<sf::Event::JoystickButtonReleased>(
        event, lua, "getIfJoystickButtonReleased");
    LUASF_STUB_FUNCTION("sf.Event", "getIfJoystickMoved",
                        "fun(self: sf.Event): sf.Event_JoystickMoved|nil");
    bind_event_get_if<sf::Event::JoystickMoved>(event, lua,
                                                "getIfJoystickMoved");
    LUASF_STUB_FUNCTION("sf.Event", "getIfJoystickConnected",
                        "fun(self: sf.Event): sf.Event_JoystickConnected|nil");
    bind_event_get_if<sf::Event::JoystickConnected>(event, lua,
                                                    "getIfJoystickConnected");
    LUASF_STUB_FUNCTION(
        "sf.Event", "getIfJoystickDisconnected",
        "fun(self: sf.Event): sf.Event_JoystickDisconnected|nil");
    bind_event_get_if<sf::Event::JoystickDisconnected>(
        event, lua, "getIfJoystickDisconnected");
    LUASF_STUB_FUNCTION("sf.Event", "getIfTouchBegan",
                        "fun(self: sf.Event): sf.Event_TouchBegan|nil");
    bind_event_get_if<sf::Event::TouchBegan>(event, lua, "getIfTouchBegan");
    LUASF_STUB_FUNCTION("sf.Event", "getIfTouchMoved",
                        "fun(self: sf.Event): sf.Event_TouchMoved|nil");
    bind_event_get_if<sf::Event::TouchMoved>(event, lua, "getIfTouchMoved");
    LUASF_STUB_FUNCTION("sf.Event", "getIfTouchEnded",
                        "fun(self: sf.Event): sf.Event_TouchEnded|nil");
    bind_event_get_if<sf::Event::TouchEnded>(event, lua, "getIfTouchEnded");
    LUASF_STUB_FUNCTION("sf.Event", "getIfSensorChanged",
                        "fun(self: sf.Event): sf.Event_SensorChanged|nil");
    bind_event_get_if<sf::Event::SensorChanged>(event, lua,
                                                "getIfSensorChanged");
}
