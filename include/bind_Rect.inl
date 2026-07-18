#pragma once

#include <sstream>

namespace lua_sf {

template <typename T>
void bind_RectT(sol::state_view lua, sol::table sfTable,
                const std::string &name) {
  using Rect = sf::Rect<T>;
  using Scalar = LuaNumeric<T>;
  using Vector = sf::Vector2<T>;

  auto type = sfTable.new_usertype<Rect>(name.c_str(), sol::no_constructor);
  type.set_function("new", sol::factories([] { return Rect{}; },
                                          [](Vector position, Vector size) {
                                            return Rect{position, size};
                                          },
                                          [](Scalar x, Scalar y, Scalar width,
                                             Scalar height) {
                                            return Rect{
                                                {unwrapLuaNumeric<T>(x),
                                                 unwrapLuaNumeric<T>(y)},
                                                {unwrapLuaNumeric<T>(width),
                                                 unwrapLuaNumeric<T>(height)}};
                                          }));
  type["position"] =
      sol::policies(&Rect::position, sol::self_dependency{});
  type["size"] = sol::policies(&Rect::size, sol::self_dependency{});
  type.set_function("contains", [](const Rect &self, Vector point) {
    return self.contains(point);
  });
  type.set_function("getCenter",
                    [](const Rect &self) { return self.getCenter(); });
  type.set_function("findIntersection",
                    [lua](const Rect &self, const Rect &other) -> sol::object {
                      return optional_to_object(lua,
                                                self.findIntersection(other));
                    });
  type[sol::meta_function::equal_to] = [](const Rect &left, const Rect &right) {
    return left == right;
  };
  type[sol::meta_function::to_string] = [name](const Rect &self) {
    std::ostringstream stream;
    stream << name << "(" << self.position.x << ", " << self.position.y << ", "
           << self.size.x << ", " << self.size.y << ")";
    return stream.str();
  };
}

} // namespace lua_sf
