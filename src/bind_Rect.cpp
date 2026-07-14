#include "bind_Rect.hpp"

void bind_Rect(sol::state_view lua) {
  sol::table sf = lua_sf::sf_table(lua);

  LUASF_STUB_CLASS("sf.IntRect");
  LUASF_STUB_FIELD("position", "sf.Vector2i");
  LUASF_STUB_FIELD("size", "sf.Vector2i");
  LUASF_STUB_FUNCTION(
      "sf.IntRect", "new",
      "fun(position: sf.Vector2i, size: sf.Vector2i): sf.IntRect");
  LUASF_STUB_OVERLOAD("sf.IntRect", "new", "fun(): sf.IntRect");
  LUASF_STUB_OVERLOAD("sf.IntRect", "new",
                      "fun(x: integer, y: integer, width: integer, height: "
                      "integer): sf.IntRect");
  LUASF_STUB_FUNCTION("sf.IntRect", "contains",
                      "fun(self: sf.IntRect, point: sf.Vector2i): boolean");
  LUASF_STUB_FUNCTION("sf.IntRect", "getCenter",
                      "fun(self: sf.IntRect): sf.Vector2i");
  LUASF_STUB_FUNCTION(
      "sf.IntRect", "findIntersection",
      "fun(self: sf.IntRect, other: sf.IntRect): sf.IntRect|nil");

  LUASF_STUB_CLASS("sf.FloatRect");
  LUASF_STUB_FIELD("position", "sf.Vector2f");
  LUASF_STUB_FIELD("size", "sf.Vector2f");
  LUASF_STUB_FUNCTION(
      "sf.FloatRect", "new",
      "fun(position: sf.Vector2f, size: sf.Vector2f): sf.FloatRect");
  LUASF_STUB_OVERLOAD("sf.FloatRect", "new", "fun(): sf.FloatRect");
  LUASF_STUB_OVERLOAD(
      "sf.FloatRect", "new",
      "fun(x: number, y: number, width: number, height: number): sf.FloatRect");
  LUASF_STUB_FUNCTION("sf.FloatRect", "contains",
                      "fun(self: sf.FloatRect, point: sf.Vector2f): boolean");
  LUASF_STUB_FUNCTION("sf.FloatRect", "getCenter",
                      "fun(self: sf.FloatRect): sf.Vector2f");
  LUASF_STUB_FUNCTION(
      "sf.FloatRect", "findIntersection",
      "fun(self: sf.FloatRect, other: sf.FloatRect): sf.FloatRect|nil");

  lua_sf::bind_RectT<int>(lua, sf, "IntRect");
  lua_sf::bind_RectT<float>(lua, sf, "FloatRect");
}
