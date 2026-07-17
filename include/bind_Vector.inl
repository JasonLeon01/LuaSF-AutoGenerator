#pragma once

#include <sstream>
#include <tuple>
#include <type_traits>

namespace lua_sf {

template <typename Vector>
std::string vector_to_string(const std::string &name, const Vector &self) {
  std::ostringstream stream;
  stream << name << "(" << self.x << ", " << self.y;
  if constexpr (requires { self.z; })
    stream << ", " << self.z;
  if constexpr (requires { self.w; })
    stream << ", " << self.w;
  stream << ")";
  return stream.str();
}

template <typename T>
void bind_Vector2T(sol::table sfTable, const std::string &name) {
  using Vector = sf::Vector2<T>;
  using Scalar = LuaNumeric<T>;

  auto type = sfTable.new_usertype<Vector>(name.c_str(), sol::no_constructor);
  if constexpr (std::is_floating_point_v<T>) {
    type.set_function(
        "new",
        sol::factories([] { return Vector{}; },
                       [](Scalar x, Scalar y) {
                         return Vector{unwrapLuaNumeric<T>(x),
                                       unwrapLuaNumeric<T>(y)};
                       },
                       [](Scalar r, sf::Angle phi) {
                         return Vector{unwrapLuaNumeric<T>(r), phi};
                       }));
  } else {
    type.set_function(
        "new", sol::factories([] { return Vector{}; },
                               [](Scalar x, Scalar y) {
                                 return Vector{unwrapLuaNumeric<T>(x),
                                               unwrapLuaNumeric<T>(y)};
                               }));
  }

  if constexpr (is_lua_integral_v<T>) {
    type.set("x", sol::property([](const Vector &self) { return self.x; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.x = value.value();
                                }));
    type.set("y", sol::property([](const Vector &self) { return self.y; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.y = value.value();
                                }));
  } else {
    type["x"] = &Vector::x;
    type["y"] = &Vector::y;
  }
  type.set_function("unpack", [](const Vector &self) {
    return std::make_tuple(self.x, self.y);
  });
  type[sol::meta_function::to_string] = [name](const Vector &self) {
    return vector_to_string(name, self);
  };
  type[sol::meta_function::equal_to] =
      [](const Vector &left, const Vector &right) { return left == right; };

  if constexpr (!std::is_same_v<T, bool>) {
    type.set_function("lengthSquared",
                      [](const Vector &self) { return self.lengthSquared(); });
    type.set_function(
        "dot", [](const Vector &self, Vector rhs) { return self.dot(rhs); });
    type.set_function("cross", [](const Vector &self, Vector rhs) {
      return self.cross(rhs);
    });
    type.set_function("perpendicular",
                      [](const Vector &self) { return self.perpendicular(); });
    type.set_function("componentWiseMul", [](const Vector &self, Vector rhs) {
      return self.componentWiseMul(rhs);
    });
    type.set_function("componentWiseDiv", [](const Vector &self, Vector rhs) {
      return self.componentWiseDiv(rhs);
    });
    type[sol::meta_function::unary_minus] = [](Vector self) { return -self; };
    type[sol::meta_function::addition] = [](Vector left, Vector right) {
      return left + right;
    };
    type[sol::meta_function::subtraction] = [](Vector left, Vector right) {
      return left - right;
    };
    type[sol::meta_function::multiplication] =
        sol::overload([](Vector left, Scalar right) {
                        return left * unwrapLuaNumeric<T>(right);
                      },
                      [](Scalar left, Vector right) {
                        return unwrapLuaNumeric<T>(left) * right;
                      });
    type[sol::meta_function::division] = [](Vector left, Scalar right) {
      return left / unwrapLuaNumeric<T>(right);
    };
  }

  if constexpr (std::is_floating_point_v<T>) {
    type.set_function("length",
                      [](const Vector &self) { return self.length(); });
    type.set_function("normalized",
                      [](const Vector &self) { return self.normalized(); });
    type.set_function("angleTo", [](const Vector &self, Vector rhs) {
      return self.angleTo(rhs);
    });
    type.set_function("angle", [](const Vector &self) { return self.angle(); });
    type.set_function("rotatedBy", [](const Vector &self, sf::Angle phi) {
      return self.rotatedBy(phi);
    });
    type.set_function("projectedOnto", [](const Vector &self, Vector axis) {
      return self.projectedOnto(axis);
    });
  }
}

template <typename T>
void bind_Vector3T(sol::table sfTable, const std::string &name) {
  using Vector = sf::Vector3<T>;
  using Scalar = LuaNumeric<T>;

  auto type = sfTable.new_usertype<Vector>(name.c_str(), sol::no_constructor);
  type.set_function(
      "new", sol::factories([] { return Vector{}; },
                            [](Scalar x, Scalar y, Scalar z) {
                              return Vector{unwrapLuaNumeric<T>(x),
                                            unwrapLuaNumeric<T>(y),
                                            unwrapLuaNumeric<T>(z)};
                            }));
  if constexpr (is_lua_integral_v<T>) {
    type.set("x", sol::property([](const Vector &self) { return self.x; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.x = value.value();
                                }));
    type.set("y", sol::property([](const Vector &self) { return self.y; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.y = value.value();
                                }));
    type.set("z", sol::property([](const Vector &self) { return self.z; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.z = value.value();
                                }));
  } else {
    type["x"] = &Vector::x;
    type["y"] = &Vector::y;
    type["z"] = &Vector::z;
  }
  type.set_function("unpack", [](const Vector &self) {
    return std::make_tuple(self.x, self.y, self.z);
  });
  type[sol::meta_function::to_string] = [name](const Vector &self) {
    return vector_to_string(name, self);
  };
  type[sol::meta_function::equal_to] =
      [](const Vector &left, const Vector &right) { return left == right; };

  if constexpr (!std::is_same_v<T, bool>) {
    type.set_function("lengthSquared",
                      [](const Vector &self) { return self.lengthSquared(); });
    type.set_function("dot", [](const Vector &self, const Vector &rhs) {
      return self.dot(rhs);
    });
    type.set_function("cross", [](const Vector &self, const Vector &rhs) {
      return self.cross(rhs);
    });
    type.set_function("componentWiseMul",
                      [](const Vector &self, const Vector &rhs) {
                        return self.componentWiseMul(rhs);
                      });
    type.set_function("componentWiseDiv",
                      [](const Vector &self, const Vector &rhs) {
                        return self.componentWiseDiv(rhs);
                      });
    type[sol::meta_function::unary_minus] = [](const Vector &self) {
      return -self;
    };
    type[sol::meta_function::addition] =
        [](const Vector &left, const Vector &right) { return left + right; };
    type[sol::meta_function::subtraction] =
        [](const Vector &left, const Vector &right) { return left - right; };
    type[sol::meta_function::multiplication] =
        sol::overload([](const Vector &left, Scalar right) {
                        return left * unwrapLuaNumeric<T>(right);
                      },
                      [](Scalar left, const Vector &right) {
                        return unwrapLuaNumeric<T>(left) * right;
                      });
    type[sol::meta_function::division] = [](const Vector &left, Scalar right) {
      return left / unwrapLuaNumeric<T>(right);
    };
  }

  if constexpr (std::is_floating_point_v<T>) {
    type.set_function("length",
                      [](const Vector &self) { return self.length(); });
    type.set_function("normalized",
                      [](const Vector &self) { return self.normalized(); });
  }
}

template <typename T>
void bind_Vector4T(sol::table sfTable, const std::string &name) {
  using Vector = sf::priv::Vector4<T>;
  using Scalar = LuaNumeric<T>;

  auto type = sfTable.new_usertype<Vector>(name.c_str(), sol::no_constructor);
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int>) {
    type.set_function(
        "new",
        sol::factories([] { return Vector{}; },
                       [](Scalar x, Scalar y, Scalar z, Scalar w) {
                         return Vector{unwrapLuaNumeric<T>(x),
                                       unwrapLuaNumeric<T>(y),
                                       unwrapLuaNumeric<T>(z),
                                       unwrapLuaNumeric<T>(w)};
                       },
                       [](sf::Color color) { return Vector{color}; }));
  } else {
    type.set_function(
        "new", sol::factories([] { return Vector{}; },
                               [](Scalar x, Scalar y, Scalar z, Scalar w) {
                                 return Vector{unwrapLuaNumeric<T>(x),
                                               unwrapLuaNumeric<T>(y),
                                               unwrapLuaNumeric<T>(z),
                                               unwrapLuaNumeric<T>(w)};
                               }));
  }

  if constexpr (is_lua_integral_v<T>) {
    type.set("x", sol::property([](const Vector &self) { return self.x; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.x = value.value();
                                }));
    type.set("y", sol::property([](const Vector &self) { return self.y; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.y = value.value();
                                }));
    type.set("z", sol::property([](const Vector &self) { return self.z; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.z = value.value();
                                }));
    type.set("w", sol::property([](const Vector &self) { return self.w; },
                                [](Vector &self, LuaIntegral<T> value) {
                                  self.w = value.value();
                                }));
  } else {
    type["x"] = &Vector::x;
    type["y"] = &Vector::y;
    type["z"] = &Vector::z;
    type["w"] = &Vector::w;
  }
  type.set_function("unpack", [](const Vector &self) {
    return std::make_tuple(self.x, self.y, self.z, self.w);
  });
  type[sol::meta_function::to_string] = [name](const Vector &self) {
    return vector_to_string(name, self);
  };
}

} // namespace lua_sf
