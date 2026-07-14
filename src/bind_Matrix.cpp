#include "bind_Matrix.hpp"

void bind_Matrix(sol::state_view lua) {
  sol::table sf = lua_sf::sf_table(lua);

  LUASF_STUB_CLASS("sf.Mat3");
  LUASF_STUB_FIELD("array", "number[]");
  LUASF_STUB_FUNCTION("sf.Mat3", "new", "fun(values: number[]): sf.Mat3");
  LUASF_STUB_FUNCTION("sf.Mat3", "copyMatrix",
                      "fun(source: sf.Transform, dest: sf.Mat3)");

  LUASF_STUB_CLASS("sf.Mat4");
  LUASF_STUB_FIELD("array", "number[]");
  LUASF_STUB_FUNCTION("sf.Mat4", "new", "fun(values: number[]): sf.Mat4");
  LUASF_STUB_FUNCTION("sf.Mat4", "copyMatrix",
                      "fun(source: sf.Transform, dest: sf.Mat4)");

  lua_sf::bind_MatrixT<3>(lua, sf, "Mat3");
  lua_sf::bind_MatrixT<4>(lua, sf, "Mat4");
}
