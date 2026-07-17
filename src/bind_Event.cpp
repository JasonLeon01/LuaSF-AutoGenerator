#include "bind_Event.hpp"

#include <functional>

namespace {

template <typename T>
sol::object event_get_if(sol::state_view lua, sf::Event &event) {
  if (auto *value = event.getIf<T>())
    return sol::make_object(lua, std::ref(*value));
  return sol::make_object(lua, lua_sf::LUASF_SOL_NIL);
}

template <typename T> bool event_is(const sf::Event &event) {
  return event.is<T>();
}

template <typename T> const char *event_type_name() {
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
void bind_empty_event_subtype(sol::table sf, const char *name) {
  sf.new_usertype<T>(name, sol::constructors<T()>());
}

template <typename UserType, typename EventType, typename FieldType>
void bind_event_field(UserType &type, const char *name,
                      FieldType EventType::*field) {
  if constexpr (lua_sf::is_lua_integral_v<FieldType>) {
    using LuaFieldType =
        std::conditional_t<std::is_signed_v<FieldType>, std::int64_t,
                           std::uint64_t>;
    type.set(
        name,
        sol::property(
            [field](const EventType &self) {
              return static_cast<LuaFieldType>(self.*field);
            },
            [field](EventType &self, lua_sf::LuaIntegral<FieldType> value) {
              self.*field = value.value();
            }));
  } else {
    type[name] = field;
  }
}

} // namespace

void bind_Event(sol::state_view lua) {
  sol::table sf = lua_sf::sf_table(lua);

  LUASF_STUB_CLASS("sf.Event_Closed");
  LUASF_STUB_FUNCTION("sf.Event_Closed", "new", "fun(): sf.Event_Closed");
  bind_empty_event_subtype<sf::Event::Closed>(sf, "Event_Closed");

  LUASF_STUB_CLASS("sf.Event_FocusLost");
  LUASF_STUB_FUNCTION("sf.Event_FocusLost", "new", "fun(): sf.Event_FocusLost");
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
  LUASF_STUB_FUNCTION("sf.Event_MouseLeft", "new", "fun(): sf.Event_MouseLeft");
  bind_empty_event_subtype<sf::Event::MouseLeft>(sf, "Event_MouseLeft");

  LUASF_STUB_CLASS("sf.Event_Resized");
  LUASF_STUB_FIELD("size", "sf.Vector2u");
  LUASF_STUB_FUNCTION("sf.Event_Resized", "new", "fun(): sf.Event_Resized");
  auto resized = sf.new_usertype<sf::Event::Resized>(
      "Event_Resized", sol::constructors<sf::Event::Resized()>());
  resized["size"] = &sf::Event::Resized::size;

  LUASF_STUB_CLASS("sf.Event_TextEntered");
  LUASF_STUB_FIELD("unicode", "integer");
  LUASF_STUB_FUNCTION("sf.Event_TextEntered", "new",
                      "fun(): sf.Event_TextEntered");
  auto textEntered = sf.new_usertype<sf::Event::TextEntered>(
      "Event_TextEntered", sol::constructors<sf::Event::TextEntered()>());
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
  auto keyPressed = sf.new_usertype<sf::Event::KeyPressed>(
      "Event_KeyPressed", sol::constructors<sf::Event::KeyPressed()>());
  keyPressed["code"] = &sf::Event::KeyPressed::code;
  keyPressed["scancode"] = &sf::Event::KeyPressed::scancode;
  keyPressed["alt"] = &sf::Event::KeyPressed::alt;
  keyPressed["control"] = &sf::Event::KeyPressed::control;
  keyPressed["shift"] = &sf::Event::KeyPressed::shift;
  keyPressed["system"] = &sf::Event::KeyPressed::system;

  LUASF_STUB_CLASS("sf.Event_KeyReleased");
  LUASF_STUB_FIELD("code", "sf.Keyboard.Key");
  LUASF_STUB_FIELD("scancode", "sf.Keyboard.Scancode");
  LUASF_STUB_FIELD("alt", "boolean");
  LUASF_STUB_FIELD("control", "boolean");
  LUASF_STUB_FIELD("shift", "boolean");
  LUASF_STUB_FIELD("system", "boolean");
  LUASF_STUB_FUNCTION("sf.Event_KeyReleased", "new",
                      "fun(): sf.Event_KeyReleased");
  auto keyReleased = sf.new_usertype<sf::Event::KeyReleased>(
      "Event_KeyReleased", sol::constructors<sf::Event::KeyReleased()>());
  keyReleased["code"] = &sf::Event::KeyReleased::code;
  keyReleased["scancode"] = &sf::Event::KeyReleased::scancode;
  keyReleased["alt"] = &sf::Event::KeyReleased::alt;
  keyReleased["control"] = &sf::Event::KeyReleased::control;
  keyReleased["shift"] = &sf::Event::KeyReleased::shift;
  keyReleased["system"] = &sf::Event::KeyReleased::system;

  LUASF_STUB_CLASS("sf.Event_MouseWheelScrolled");
  LUASF_STUB_FIELD("wheel", "sf.Mouse.Wheel");
  LUASF_STUB_FIELD("delta", "number");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_MouseWheelScrolled", "new",
                      "fun(): sf.Event_MouseWheelScrolled");
  auto wheelScrolled = sf.new_usertype<sf::Event::MouseWheelScrolled>(
      "Event_MouseWheelScrolled",
      sol::constructors<sf::Event::MouseWheelScrolled()>());
  wheelScrolled["wheel"] = &sf::Event::MouseWheelScrolled::wheel;
  wheelScrolled["delta"] = &sf::Event::MouseWheelScrolled::delta;
  wheelScrolled["position"] = &sf::Event::MouseWheelScrolled::position;

  LUASF_STUB_CLASS("sf.Event_MouseButtonPressed");
  LUASF_STUB_FIELD("button", "sf.Mouse.Button");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_MouseButtonPressed", "new",
                      "fun(): sf.Event_MouseButtonPressed");
  auto buttonPressed = sf.new_usertype<sf::Event::MouseButtonPressed>(
      "Event_MouseButtonPressed",
      sol::constructors<sf::Event::MouseButtonPressed()>());
  buttonPressed["button"] = &sf::Event::MouseButtonPressed::button;
  buttonPressed["position"] = &sf::Event::MouseButtonPressed::position;

  LUASF_STUB_CLASS("sf.Event_MouseButtonReleased");
  LUASF_STUB_FIELD("button", "sf.Mouse.Button");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_MouseButtonReleased", "new",
                      "fun(): sf.Event_MouseButtonReleased");
  auto buttonReleased = sf.new_usertype<sf::Event::MouseButtonReleased>(
      "Event_MouseButtonReleased",
      sol::constructors<sf::Event::MouseButtonReleased()>());
  buttonReleased["button"] = &sf::Event::MouseButtonReleased::button;
  buttonReleased["position"] = &sf::Event::MouseButtonReleased::position;

  LUASF_STUB_CLASS("sf.Event_MouseMoved");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_MouseMoved", "new",
                      "fun(): sf.Event_MouseMoved");
  auto mouseMoved = sf.new_usertype<sf::Event::MouseMoved>(
      "Event_MouseMoved", sol::constructors<sf::Event::MouseMoved()>());
  mouseMoved["position"] = &sf::Event::MouseMoved::position;

  LUASF_STUB_CLASS("sf.Event_MouseMovedRaw");
  LUASF_STUB_FIELD("delta", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_MouseMovedRaw", "new",
                      "fun(): sf.Event_MouseMovedRaw");
  auto rawMoved = sf.new_usertype<sf::Event::MouseMovedRaw>(
      "Event_MouseMovedRaw", sol::constructors<sf::Event::MouseMovedRaw()>());
  rawMoved["delta"] = &sf::Event::MouseMovedRaw::delta;

  LUASF_STUB_CLASS("sf.Event_JoystickButtonPressed");
  LUASF_STUB_FIELD("joystickId", "integer");
  LUASF_STUB_FIELD("button", "integer");
  LUASF_STUB_FUNCTION("sf.Event_JoystickButtonPressed", "new",
                      "fun(): sf.Event_JoystickButtonPressed");
  auto joystickButtonPressed =
      sf.new_usertype<sf::Event::JoystickButtonPressed>(
          "Event_JoystickButtonPressed",
          sol::constructors<sf::Event::JoystickButtonPressed()>());
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
      sf.new_usertype<sf::Event::JoystickButtonReleased>(
          "Event_JoystickButtonReleased",
          sol::constructors<sf::Event::JoystickButtonReleased()>());
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
  auto joystickMoved = sf.new_usertype<sf::Event::JoystickMoved>(
      "Event_JoystickMoved", sol::constructors<sf::Event::JoystickMoved()>());
  bind_event_field(joystickMoved, "joystickId",
                   &sf::Event::JoystickMoved::joystickId);
  joystickMoved["axis"] = &sf::Event::JoystickMoved::axis;
  joystickMoved["position"] = &sf::Event::JoystickMoved::position;

  LUASF_STUB_CLASS("sf.Event_JoystickConnected");
  LUASF_STUB_FIELD("joystickId", "integer");
  LUASF_STUB_FUNCTION("sf.Event_JoystickConnected", "new",
                      "fun(): sf.Event_JoystickConnected");
  auto joystickConnected = sf.new_usertype<sf::Event::JoystickConnected>(
      "Event_JoystickConnected",
      sol::constructors<sf::Event::JoystickConnected()>());
  bind_event_field(joystickConnected, "joystickId",
                   &sf::Event::JoystickConnected::joystickId);

  LUASF_STUB_CLASS("sf.Event_JoystickDisconnected");
  LUASF_STUB_FIELD("joystickId", "integer");
  LUASF_STUB_FUNCTION("sf.Event_JoystickDisconnected", "new",
                      "fun(): sf.Event_JoystickDisconnected");
  auto joystickDisconnected = sf.new_usertype<sf::Event::JoystickDisconnected>(
      "Event_JoystickDisconnected",
      sol::constructors<sf::Event::JoystickDisconnected()>());
  bind_event_field(joystickDisconnected, "joystickId",
                   &sf::Event::JoystickDisconnected::joystickId);

  LUASF_STUB_CLASS("sf.Event_TouchBegan");
  LUASF_STUB_FIELD("finger", "integer");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_TouchBegan", "new",
                      "fun(): sf.Event_TouchBegan");
  auto touchBegan = sf.new_usertype<sf::Event::TouchBegan>(
      "Event_TouchBegan", sol::constructors<sf::Event::TouchBegan()>());
  bind_event_field(touchBegan, "finger", &sf::Event::TouchBegan::finger);
  touchBegan["position"] = &sf::Event::TouchBegan::position;

  LUASF_STUB_CLASS("sf.Event_TouchMoved");
  LUASF_STUB_FIELD("finger", "integer");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_TouchMoved", "new",
                      "fun(): sf.Event_TouchMoved");
  auto touchMoved = sf.new_usertype<sf::Event::TouchMoved>(
      "Event_TouchMoved", sol::constructors<sf::Event::TouchMoved()>());
  bind_event_field(touchMoved, "finger", &sf::Event::TouchMoved::finger);
  touchMoved["position"] = &sf::Event::TouchMoved::position;

  LUASF_STUB_CLASS("sf.Event_TouchEnded");
  LUASF_STUB_FIELD("finger", "integer");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Event_TouchEnded", "new",
                      "fun(): sf.Event_TouchEnded");
  auto touchEnded = sf.new_usertype<sf::Event::TouchEnded>(
      "Event_TouchEnded", sol::constructors<sf::Event::TouchEnded()>());
  bind_event_field(touchEnded, "finger", &sf::Event::TouchEnded::finger);
  touchEnded["position"] = &sf::Event::TouchEnded::position;

  LUASF_STUB_CLASS("sf.Event_SensorChanged");
  LUASF_STUB_FIELD("type", "sf.Sensor.Type");
  LUASF_STUB_FIELD("value", "sf.Vector3f");
  LUASF_STUB_FUNCTION("sf.Event_SensorChanged", "new",
                      "fun(): sf.Event_SensorChanged");
  auto sensorChanged = sf.new_usertype<sf::Event::SensorChanged>(
      "Event_SensorChanged", sol::constructors<sf::Event::SensorChanged()>());
  sensorChanged["type"] = &sf::Event::SensorChanged::type;
  sensorChanged["value"] = &sf::Event::SensorChanged::value;

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
  LUASF_STUB_OVERLOAD("sf.Event", "new",
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
  auto event = sf.new_usertype<sf::Event>("Event", sol::no_constructor);
  event.set_function(
      "new",
      sol::factories(
          [](const sf::Event::Closed &value) { return sf::Event(value); },
          [](const sf::Event::Resized &value) { return sf::Event(value); },
          [](const sf::Event::FocusLost &value) { return sf::Event(value); },
          [](const sf::Event::FocusGained &value) { return sf::Event(value); },
          [](const sf::Event::TextEntered &value) { return sf::Event(value); },
          [](const sf::Event::KeyPressed &value) { return sf::Event(value); },
          [](const sf::Event::KeyReleased &value) { return sf::Event(value); },
          [](const sf::Event::MouseWheelScrolled &value) {
            return sf::Event(value);
          },
          [](const sf::Event::MouseButtonPressed &value) {
            return sf::Event(value);
          },
          [](const sf::Event::MouseButtonReleased &value) {
            return sf::Event(value);
          },
          [](const sf::Event::MouseMoved &value) { return sf::Event(value); },
          [](const sf::Event::MouseMovedRaw &value) {
            return sf::Event(value);
          },
          [](const sf::Event::MouseEntered &value) { return sf::Event(value); },
          [](const sf::Event::MouseLeft &value) { return sf::Event(value); },
          [](const sf::Event::JoystickButtonPressed &value) {
            return sf::Event(value);
          },
          [](const sf::Event::JoystickButtonReleased &value) {
            return sf::Event(value);
          },
          [](const sf::Event::JoystickMoved &value) {
            return sf::Event(value);
          },
          [](const sf::Event::JoystickConnected &value) {
            return sf::Event(value);
          },
          [](const sf::Event::JoystickDisconnected &value) {
            return sf::Event(value);
          },
          [](const sf::Event::TouchBegan &value) { return sf::Event(value); },
          [](const sf::Event::TouchMoved &value) { return sf::Event(value); },
          [](const sf::Event::TouchEnded &value) { return sf::Event(value); },
          [](const sf::Event::SensorChanged &value) {
            return sf::Event(value);
          }));

  LUASF_STUB_FUNCTION("sf.Event", "type", "fun(self: sf.Event): string");
  event.set_function("type", [](const sf::Event &self) {
    return self.visit([](const auto &value) {
      return event_type_name<std::decay_t<decltype(value)>>();
    });
  });
  LUASF_STUB_FUNCTION("sf.Event", "get", "fun(self: sf.Event): any");
  event.set_function("get", [lua](sf::Event &self) -> sol::object {
    return self.visit([lua](auto &value) -> sol::object {
      return sol::make_object(lua, std::ref(value));
    });
  });

  LUASF_STUB_FUNCTION("sf.Event", "isClosed", "fun(self: sf.Event): boolean");
  event.set_function("isClosed", &event_is<sf::Event::Closed>);
  LUASF_STUB_FUNCTION("sf.Event", "isResized", "fun(self: sf.Event): boolean");
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
  event.set_function("getIfClosed", [lua](sf::Event &self) {
    return event_get_if<sf::Event::Closed>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfResized",
                      "fun(self: sf.Event): sf.Event_Resized|nil");
  event.set_function("getIfResized", [lua](sf::Event &self) {
    return event_get_if<sf::Event::Resized>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfFocusLost",
                      "fun(self: sf.Event): sf.Event_FocusLost|nil");
  event.set_function("getIfFocusLost", [lua](sf::Event &self) {
    return event_get_if<sf::Event::FocusLost>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfFocusGained",
                      "fun(self: sf.Event): sf.Event_FocusGained|nil");
  event.set_function("getIfFocusGained", [lua](sf::Event &self) {
    return event_get_if<sf::Event::FocusGained>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfTextEntered",
                      "fun(self: sf.Event): sf.Event_TextEntered|nil");
  event.set_function("getIfTextEntered", [lua](sf::Event &self) {
    return event_get_if<sf::Event::TextEntered>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfKeyPressed",
                      "fun(self: sf.Event): sf.Event_KeyPressed|nil");
  event.set_function("getIfKeyPressed", [lua](sf::Event &self) {
    return event_get_if<sf::Event::KeyPressed>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfKeyReleased",
                      "fun(self: sf.Event): sf.Event_KeyReleased|nil");
  event.set_function("getIfKeyReleased", [lua](sf::Event &self) {
    return event_get_if<sf::Event::KeyReleased>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfMouseWheelScrolled",
                      "fun(self: sf.Event): sf.Event_MouseWheelScrolled|nil");
  event.set_function("getIfMouseWheelScrolled", [lua](sf::Event &self) {
    return event_get_if<sf::Event::MouseWheelScrolled>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfMouseButtonPressed",
                      "fun(self: sf.Event): sf.Event_MouseButtonPressed|nil");
  event.set_function("getIfMouseButtonPressed", [lua](sf::Event &self) {
    return event_get_if<sf::Event::MouseButtonPressed>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfMouseButtonReleased",
                      "fun(self: sf.Event): sf.Event_MouseButtonReleased|nil");
  event.set_function("getIfMouseButtonReleased", [lua](sf::Event &self) {
    return event_get_if<sf::Event::MouseButtonReleased>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfMouseMoved",
                      "fun(self: sf.Event): sf.Event_MouseMoved|nil");
  event.set_function("getIfMouseMoved", [lua](sf::Event &self) {
    return event_get_if<sf::Event::MouseMoved>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfMouseMovedRaw",
                      "fun(self: sf.Event): sf.Event_MouseMovedRaw|nil");
  event.set_function("getIfMouseMovedRaw", [lua](sf::Event &self) {
    return event_get_if<sf::Event::MouseMovedRaw>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfMouseEntered",
                      "fun(self: sf.Event): sf.Event_MouseEntered|nil");
  event.set_function("getIfMouseEntered", [lua](sf::Event &self) {
    return event_get_if<sf::Event::MouseEntered>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfMouseLeft",
                      "fun(self: sf.Event): sf.Event_MouseLeft|nil");
  event.set_function("getIfMouseLeft", [lua](sf::Event &self) {
    return event_get_if<sf::Event::MouseLeft>(lua, self);
  });
  LUASF_STUB_FUNCTION(
      "sf.Event", "getIfJoystickButtonPressed",
      "fun(self: sf.Event): sf.Event_JoystickButtonPressed|nil");
  event.set_function("getIfJoystickButtonPressed", [lua](sf::Event &self) {
    return event_get_if<sf::Event::JoystickButtonPressed>(lua, self);
  });
  LUASF_STUB_FUNCTION(
      "sf.Event", "getIfJoystickButtonReleased",
      "fun(self: sf.Event): sf.Event_JoystickButtonReleased|nil");
  event.set_function("getIfJoystickButtonReleased", [lua](sf::Event &self) {
    return event_get_if<sf::Event::JoystickButtonReleased>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfJoystickMoved",
                      "fun(self: sf.Event): sf.Event_JoystickMoved|nil");
  event.set_function("getIfJoystickMoved", [lua](sf::Event &self) {
    return event_get_if<sf::Event::JoystickMoved>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfJoystickConnected",
                      "fun(self: sf.Event): sf.Event_JoystickConnected|nil");
  event.set_function("getIfJoystickConnected", [lua](sf::Event &self) {
    return event_get_if<sf::Event::JoystickConnected>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfJoystickDisconnected",
                      "fun(self: sf.Event): sf.Event_JoystickDisconnected|nil");
  event.set_function("getIfJoystickDisconnected", [lua](sf::Event &self) {
    return event_get_if<sf::Event::JoystickDisconnected>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfTouchBegan",
                      "fun(self: sf.Event): sf.Event_TouchBegan|nil");
  event.set_function("getIfTouchBegan", [lua](sf::Event &self) {
    return event_get_if<sf::Event::TouchBegan>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfTouchMoved",
                      "fun(self: sf.Event): sf.Event_TouchMoved|nil");
  event.set_function("getIfTouchMoved", [lua](sf::Event &self) {
    return event_get_if<sf::Event::TouchMoved>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfTouchEnded",
                      "fun(self: sf.Event): sf.Event_TouchEnded|nil");
  event.set_function("getIfTouchEnded", [lua](sf::Event &self) {
    return event_get_if<sf::Event::TouchEnded>(lua, self);
  });
  LUASF_STUB_FUNCTION("sf.Event", "getIfSensorChanged",
                      "fun(self: sf.Event): sf.Event_SensorChanged|nil");
  event.set_function("getIfSensorChanged", [lua](sf::Event &self) {
    return event_get_if<sf::Event::SensorChanged>(lua, self);
  });
}
