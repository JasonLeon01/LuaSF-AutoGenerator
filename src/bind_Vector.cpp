#include "bind_Vector.hpp"

void bind_Vector(sol::state_view lua) {
  sol::table sf = lua_sf::sf_table(lua);

  LUASF_STUB_CLASS("sf.Vector2i");
  LUASF_STUB_FIELD("x", "integer");
  LUASF_STUB_FIELD("y", "integer");
  LUASF_STUB_FUNCTION("sf.Vector2i", "new",
                      "fun(x: integer, y: integer): sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Vector2i", "unpack",
                      "fun(self: sf.Vector2i): integer, integer");
  LUASF_STUB_FUNCTION("sf.Vector2i", "lengthSquared",
                      "fun(self: sf.Vector2i): integer");
  LUASF_STUB_FUNCTION("sf.Vector2i", "dot",
                      "fun(self: sf.Vector2i, rhs: sf.Vector2i): integer");
  LUASF_STUB_FUNCTION("sf.Vector2i", "cross",
                      "fun(self: sf.Vector2i, rhs: sf.Vector2i): integer");
  LUASF_STUB_FUNCTION("sf.Vector2i", "perpendicular",
                      "fun(self: sf.Vector2i): sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Vector2i", "componentWiseMul",
                      "fun(self: sf.Vector2i, rhs: sf.Vector2i): sf.Vector2i");
  LUASF_STUB_FUNCTION("sf.Vector2i", "componentWiseDiv",
                      "fun(self: sf.Vector2i, rhs: sf.Vector2i): sf.Vector2i");
  LUASF_STUB_OPERATOR("sf.Vector2i", "add(sf.Vector2i): sf.Vector2i");
  LUASF_STUB_OPERATOR("sf.Vector2i", "sub(sf.Vector2i): sf.Vector2i");
  LUASF_STUB_OPERATOR("sf.Vector2i", "mul(integer): sf.Vector2i");
  LUASF_STUB_OPERATOR("sf.Vector2i", "div(integer): sf.Vector2i");
  LUASF_STUB_OPERATOR("sf.Vector2i", "unm: sf.Vector2i");
  LUASF_STUB_OPERATOR("sf.Vector2i", "eq(sf.Vector2i): boolean");

  LUASF_STUB_CLASS("sf.Vector2f");
  LUASF_STUB_FIELD("x", "number");
  LUASF_STUB_FIELD("y", "number");
  LUASF_STUB_FUNCTION("sf.Vector2f", "new",
                      "fun(x: number, y: number): sf.Vector2f");
  LUASF_STUB_FUNCTION("sf.Vector2f", "unpack",
                      "fun(self: sf.Vector2f): number, number");
  LUASF_STUB_FUNCTION("sf.Vector2f", "lengthSquared",
                      "fun(self: sf.Vector2f): number");
  LUASF_STUB_FUNCTION("sf.Vector2f", "length",
                      "fun(self: sf.Vector2f): number");
  LUASF_STUB_FUNCTION("sf.Vector2f", "dot",
                      "fun(self: sf.Vector2f, rhs: sf.Vector2f): number");
  LUASF_STUB_FUNCTION("sf.Vector2f", "cross",
                      "fun(self: sf.Vector2f, rhs: sf.Vector2f): number");
  LUASF_STUB_FUNCTION("sf.Vector2f", "normalized",
                      "fun(self: sf.Vector2f): sf.Vector2f");
  LUASF_STUB_FUNCTION("sf.Vector2f", "perpendicular",
                      "fun(self: sf.Vector2f): sf.Vector2f");
  LUASF_STUB_FUNCTION("sf.Vector2f", "angleTo",
                      "fun(self: sf.Vector2f, rhs: sf.Vector2f): sf.Angle");
  LUASF_STUB_FUNCTION("sf.Vector2f", "angle",
                      "fun(self: sf.Vector2f): sf.Angle");
  LUASF_STUB_FUNCTION("sf.Vector2f", "rotatedBy",
                      "fun(self: sf.Vector2f, phi: sf.Angle): sf.Vector2f");
  LUASF_STUB_FUNCTION("sf.Vector2f", "projectedOnto",
                      "fun(self: sf.Vector2f, axis: sf.Vector2f): sf.Vector2f");
  LUASF_STUB_FUNCTION("sf.Vector2f", "componentWiseMul",
                      "fun(self: sf.Vector2f, rhs: sf.Vector2f): sf.Vector2f");
  LUASF_STUB_FUNCTION("sf.Vector2f", "componentWiseDiv",
                      "fun(self: sf.Vector2f, rhs: sf.Vector2f): sf.Vector2f");
  LUASF_STUB_OPERATOR("sf.Vector2f", "add(sf.Vector2f): sf.Vector2f");
  LUASF_STUB_OPERATOR("sf.Vector2f", "sub(sf.Vector2f): sf.Vector2f");
  LUASF_STUB_OPERATOR("sf.Vector2f", "mul(number): sf.Vector2f");
  LUASF_STUB_OPERATOR("sf.Vector2f", "div(number): sf.Vector2f");
  LUASF_STUB_OPERATOR("sf.Vector2f", "unm: sf.Vector2f");
  LUASF_STUB_OPERATOR("sf.Vector2f", "eq(sf.Vector2f): boolean");

  LUASF_STUB_CLASS("sf.Vector2u");
  LUASF_STUB_FIELD("x", "integer");
  LUASF_STUB_FIELD("y", "integer");
  LUASF_STUB_FUNCTION("sf.Vector2u", "new",
                      "fun(x: integer, y: integer): sf.Vector2u");
  LUASF_STUB_FUNCTION("sf.Vector2u", "unpack",
                      "fun(self: sf.Vector2u): integer, integer");
  LUASF_STUB_OPERATOR("sf.Vector2u", "add(sf.Vector2u): sf.Vector2u");
  LUASF_STUB_OPERATOR("sf.Vector2u", "sub(sf.Vector2u): sf.Vector2u");
  LUASF_STUB_OPERATOR("sf.Vector2u", "mul(integer): sf.Vector2u");
  LUASF_STUB_OPERATOR("sf.Vector2u", "div(integer): sf.Vector2u");
  LUASF_STUB_OPERATOR("sf.Vector2u", "unm: sf.Vector2u");
  LUASF_STUB_OPERATOR("sf.Vector2u", "eq(sf.Vector2u): boolean");

  LUASF_STUB_CLASS("sf.Vector2b");
  LUASF_STUB_FIELD("x", "boolean");
  LUASF_STUB_FIELD("y", "boolean");
  LUASF_STUB_FUNCTION("sf.Vector2b", "new",
                      "fun(x: boolean, y: boolean): sf.Vector2b");
  LUASF_STUB_FUNCTION("sf.Vector2b", "unpack",
                      "fun(self: sf.Vector2b): boolean, boolean");
  LUASF_STUB_OPERATOR("sf.Vector2b", "eq(sf.Vector2b): boolean");

  LUASF_STUB_CLASS("sf.Vector3i");
  LUASF_STUB_FIELD("x", "integer");
  LUASF_STUB_FIELD("y", "integer");
  LUASF_STUB_FIELD("z", "integer");
  LUASF_STUB_FUNCTION("sf.Vector3i", "new",
                      "fun(x: integer, y: integer, z: integer): sf.Vector3i");
  LUASF_STUB_FUNCTION("sf.Vector3i", "unpack",
                      "fun(self: sf.Vector3i): integer, integer, integer");
  LUASF_STUB_OPERATOR("sf.Vector3i", "add(sf.Vector3i): sf.Vector3i");
  LUASF_STUB_OPERATOR("sf.Vector3i", "sub(sf.Vector3i): sf.Vector3i");
  LUASF_STUB_OPERATOR("sf.Vector3i", "mul(integer): sf.Vector3i");
  LUASF_STUB_OPERATOR("sf.Vector3i", "div(integer): sf.Vector3i");
  LUASF_STUB_OPERATOR("sf.Vector3i", "unm: sf.Vector3i");
  LUASF_STUB_OPERATOR("sf.Vector3i", "eq(sf.Vector3i): boolean");

  LUASF_STUB_CLASS("sf.Vector3f");
  LUASF_STUB_FIELD("x", "number");
  LUASF_STUB_FIELD("y", "number");
  LUASF_STUB_FIELD("z", "number");
  LUASF_STUB_FUNCTION("sf.Vector3f", "new",
                      "fun(x: number, y: number, z: number): sf.Vector3f");
  LUASF_STUB_FUNCTION("sf.Vector3f", "unpack",
                      "fun(self: sf.Vector3f): number, number, number");
  LUASF_STUB_FUNCTION("sf.Vector3f", "length",
                      "fun(self: sf.Vector3f): number");
  LUASF_STUB_FUNCTION("sf.Vector3f", "normalized",
                      "fun(self: sf.Vector3f): sf.Vector3f");
  LUASF_STUB_OPERATOR("sf.Vector3f", "add(sf.Vector3f): sf.Vector3f");
  LUASF_STUB_OPERATOR("sf.Vector3f", "sub(sf.Vector3f): sf.Vector3f");
  LUASF_STUB_OPERATOR("sf.Vector3f", "mul(number): sf.Vector3f");
  LUASF_STUB_OPERATOR("sf.Vector3f", "div(number): sf.Vector3f");
  LUASF_STUB_OPERATOR("sf.Vector3f", "unm: sf.Vector3f");
  LUASF_STUB_OPERATOR("sf.Vector3f", "eq(sf.Vector3f): boolean");

  LUASF_STUB_CLASS("sf.Vector3u");
  LUASF_STUB_FIELD("x", "integer");
  LUASF_STUB_FIELD("y", "integer");
  LUASF_STUB_FIELD("z", "integer");
  LUASF_STUB_FUNCTION("sf.Vector3u", "new",
                      "fun(x: integer, y: integer, z: integer): sf.Vector3u");
  LUASF_STUB_FUNCTION("sf.Vector3u", "unpack",
                      "fun(self: sf.Vector3u): integer, integer, integer");
  LUASF_STUB_OPERATOR("sf.Vector3u", "add(sf.Vector3u): sf.Vector3u");
  LUASF_STUB_OPERATOR("sf.Vector3u", "sub(sf.Vector3u): sf.Vector3u");
  LUASF_STUB_OPERATOR("sf.Vector3u", "mul(integer): sf.Vector3u");
  LUASF_STUB_OPERATOR("sf.Vector3u", "div(integer): sf.Vector3u");
  LUASF_STUB_OPERATOR("sf.Vector3u", "unm: sf.Vector3u");
  LUASF_STUB_OPERATOR("sf.Vector3u", "eq(sf.Vector3u): boolean");

  LUASF_STUB_CLASS("sf.Vector3b");
  LUASF_STUB_FIELD("x", "boolean");
  LUASF_STUB_FIELD("y", "boolean");
  LUASF_STUB_FIELD("z", "boolean");
  LUASF_STUB_FUNCTION("sf.Vector3b", "new",
                      "fun(x: boolean, y: boolean, z: boolean): sf.Vector3b");
  LUASF_STUB_FUNCTION("sf.Vector3b", "unpack",
                      "fun(self: sf.Vector3b): boolean, boolean, boolean");
  LUASF_STUB_OPERATOR("sf.Vector3b", "eq(sf.Vector3b): boolean");

  LUASF_STUB_CLASS("sf.Vector4i");
  LUASF_STUB_FIELD("x", "integer");
  LUASF_STUB_FIELD("y", "integer");
  LUASF_STUB_FIELD("z", "integer");
  LUASF_STUB_FIELD("w", "integer");
  LUASF_STUB_FUNCTION(
      "sf.Vector4i", "new",
      "fun(x: integer, y: integer, z: integer, w: integer): sf.Vector4i");

  LUASF_STUB_CLASS("sf.Vector4f");
  LUASF_STUB_FIELD("x", "number");
  LUASF_STUB_FIELD("y", "number");
  LUASF_STUB_FIELD("z", "number");
  LUASF_STUB_FIELD("w", "number");
  LUASF_STUB_FUNCTION(
      "sf.Vector4f", "new",
      "fun(x: number, y: number, z: number, w: number): sf.Vector4f");

  LUASF_STUB_CLASS("sf.Vector4b");
  LUASF_STUB_FIELD("x", "boolean");
  LUASF_STUB_FIELD("y", "boolean");
  LUASF_STUB_FIELD("z", "boolean");
  LUASF_STUB_FIELD("w", "boolean");
  LUASF_STUB_FUNCTION(
      "sf.Vector4b", "new",
      "fun(x: boolean, y: boolean, z: boolean, w: boolean): sf.Vector4b");

  lua_sf::bind_Vector2T<int>(sf, "Vector2i");
  lua_sf::bind_Vector2T<float>(sf, "Vector2f");
  lua_sf::bind_Vector2T<unsigned int>(sf, "Vector2u");
  lua_sf::bind_Vector2T<bool>(sf, "Vector2b");

  lua_sf::bind_Vector3T<int>(sf, "Vector3i");
  lua_sf::bind_Vector3T<float>(sf, "Vector3f");
  lua_sf::bind_Vector3T<unsigned int>(sf, "Vector3u");
  lua_sf::bind_Vector3T<bool>(sf, "Vector3b");

  lua_sf::bind_Vector4T<int>(sf, "Vector4i");
  lua_sf::bind_Vector4T<float>(sf, "Vector4f");
  lua_sf::bind_Vector4T<bool>(sf, "Vector4b");

  sf["Ivec2"] = sf["Vector2i"];
  sf["Vec2"] = sf["Vector2f"];
  sf["Bvec2"] = sf["Vector2b"];
  sf["Ivec3"] = sf["Vector3i"];
  sf["Vec3"] = sf["Vector3f"];
  sf["Bvec3"] = sf["Vector3b"];
  sf["Ivec4"] = sf["Vector4i"];
  sf["Vec4"] = sf["Vector4f"];
  sf["Bvec4"] = sf["Vector4b"];

  LUASF_STUB_VALUE("sf", "Ivec2", "sf.Vector2i");
  LUASF_STUB_VALUE("sf", "Vec2", "sf.Vector2f");
  LUASF_STUB_VALUE("sf", "Bvec2", "sf.Vector2b");
  LUASF_STUB_VALUE("sf", "Ivec3", "sf.Vector3i");
  LUASF_STUB_VALUE("sf", "Vec3", "sf.Vector3f");
  LUASF_STUB_VALUE("sf", "Bvec3", "sf.Vector3b");
  LUASF_STUB_VALUE("sf", "Ivec4", "sf.Vector4i");
  LUASF_STUB_VALUE("sf", "Vec4", "sf.Vector4f");
  LUASF_STUB_VALUE("sf", "Bvec4", "sf.Vector4b");
}
