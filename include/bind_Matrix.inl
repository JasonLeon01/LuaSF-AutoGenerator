#pragma once

#include <algorithm>
#include <sstream>
#include <vector>

namespace lua_sf {

template <std::size_t N>
void bind_MatrixT(sol::state_view lua, sol::table sfTable,
                  const std::string &name) {
  using Matrix = sf::priv::Matrix<N, N>;

  auto type = sfTable.new_usertype<Matrix>(name.c_str(), sol::no_constructor);
  type.set_function(
      "new",
      sol::factories(
          [](sol::object values) {
            auto buffer = array_from_object<float>(values);
            if (buffer.size() != N * N)
              throw std::runtime_error(
                  "matrix constructor expects exactly N * N float values");
            return Matrix{buffer.data()};
          },
          [](const sf::Transform &transform) { return Matrix{transform}; }));

  type.set(
      "array",
      sol::property(
          [](const Matrix &self) {
            std::vector<float> values(self.array.begin(), self.array.end());
            return sol::as_table(std::move(values));
          },
          [](Matrix &self, sol::object values) {
            auto buffer = array_from_object<float>(values);
            if (buffer.size() != N * N)
              throw std::runtime_error(
                  "matrix array assignment expects exactly N * N float values");
            std::copy(buffer.begin(), buffer.end(), self.array.begin());
          }));

  type.set_function("copyMatrix",
                    [](const sf::Transform &source, Matrix &dest) {
                      sf::priv::copyMatrix(source, dest);
                    });
  type[sol::meta_function::to_string] = [name](const Matrix &self) {
    std::ostringstream stream;
    stream << name << "(";
    for (std::size_t i = 0; i < self.array.size(); ++i) {
      if (i != 0)
        stream << ", ";
      stream << self.array[i];
    }
    stream << ")";
    return stream.str();
  };

  (void)lua;
}

} // namespace lua_sf
