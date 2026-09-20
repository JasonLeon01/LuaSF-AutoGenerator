#pragma once

namespace lua_glue {

template <typename T>
T Object::as() const {
    lua_State* state = lua_state();
    if (state == nullptr) {
        if constexpr (std::is_same_v<T, Object> || std::is_same_v<T, Table> ||
                      std::is_same_v<T, Function>) {
            return T{};
        } else {
            throw std::runtime_error(
                "Cannot read an empty or closed Lua value");
        }
    }
    if constexpr (std::is_same_v<T, Object> || std::is_same_v<T, Table> ||
                  std::is_same_v<T, Function>) {
        return T(*this);
    } else {
        detail::AccessScope execution(state);
        if (!execution.active()) {
            throw std::runtime_error("Lua state is stopping");
        }
        StackGuard stack(state);
        push(state);
        return Read<T>(state, -1);
    }
}

template <typename T>
bool Object::is() const {
    if constexpr (std::is_same_v<T, Object>) {
        return get_type() != Type::None;
    } else if constexpr (std::is_same_v<T, Table>) {
        return get_type() == Type::Table;
    } else if constexpr (std::is_same_v<T, Function>) {
        return get_type() == Type::Function;
    }
    lua_State* state = lua_state();
    if (state == nullptr) {
        return false;
    }
    detail::AccessScope execution(state);
    if (!execution.active()) {
        return false;
    }
    StackGuard stack(state);
    push(state);
    return Check<T>(state, -1);
}

template <typename T>
Object MakeObject(lua_State* state, T&& value) {
    if (state == nullptr) {
        throw std::invalid_argument(
            "Cannot create a value without a Lua state");
    }
    if (InitializeState(state) != 0) {
        throw std::runtime_error("Lua state is stopping");
    }
    detail::AccessScope execution(state);
    if (!execution.active()) {
        throw std::runtime_error("Lua state is stopping");
    }
    StackGuard stack(state);
    const int count = Push(state, std::forward<T>(value));
    if (count != 1) {
        throw std::invalid_argument("A single Lua value is required");
    }
    return Object(state, -1);
}

template <typename T>
Object MakeObject(StateView state, T&& value) {
    return MakeObject(state.lua_state(), std::forward<T>(value));
}

template <typename Key>
FieldRef Object::operator[](Key&& key) const {
    return FieldRef(*this, MakeObject(lua_state(), std::forward<Key>(key)));
}

namespace detail {
template <typename T, typename Key>
T ReadIndex(const Object& value, Key&& key, bool raw) {
    lua_State* state = value.lua_state();
    detail::AccessScope execution(state);
    if (state == nullptr || !execution.active()) {
        throw std::runtime_error("Lua state is unavailable or stopping");
    }
    StackGuard stack(state);
    if (!lua_checkstack(state, 1)) {
        throw std::runtime_error("Lua index stack cannot grow");
    }
    if (Push(state, std::forward<Key>(key)) != 1) {
        throw std::invalid_argument("A single Lua value is required");
    }
    PushIndex(value, -1, raw);
    return Read<T>(state, -1);
}

template <typename Key, typename Value>
void WriteIndex(const Object& table, Key&& key, Value&& value, bool raw) {
    lua_State* state = table.lua_state();
    detail::AccessScope execution(state);
    if (state == nullptr || !execution.active()) {
        throw std::runtime_error("Lua state is unavailable or stopping");
    }
    StackGuard stack(state);
    if (!lua_checkstack(state, 2)) {
        throw std::runtime_error("Lua index stack cannot grow");
    }
    if (Push(state, std::forward<Key>(key)) != 1 ||
        Push(state, std::forward<Value>(value)) != 1) {
        throw std::invalid_argument("A single Lua value is required");
    }
    AssignIndex(table, -2, -1, raw);
}
}  // namespace detail

template <typename T, typename Key>
T Table::get(Key&& key) const {
    return detail::ReadIndex<T>(*this, std::forward<Key>(key), false);
}

template <typename T, typename Key>
T Table::raw_get(Key&& key) const {
    return detail::ReadIndex<T>(*this, std::forward<Key>(key), true);
}

template <typename Key, typename Value>
void Table::set(Key&& key, Value&& value) const {
    detail::WriteIndex(*this, std::forward<Key>(key),
                       std::forward<Value>(value), false);
}

template <typename Key, typename Value>
void Table::raw_set(Key&& key, Value&& value) const {
    detail::WriteIndex(*this, std::forward<Key>(key),
                       std::forward<Value>(value), true);
}

template <typename T, typename Key>
T Table::get_or_create(Key&& key) const {
    return (*this)[std::forward<Key>(key)].template get_or_create<T>();
}

template <typename T, typename Key>
T Table::get_or(Key&& key, T fallback) const {
    return (*this)[std::forward<Key>(key)].template get_or<T>(
        std::move(fallback));
}

template <typename T>
void Table::add(T&& value) const {
    raw_set(size() + 1, std::forward<T>(value));
}

template <typename Key, typename Callable>
void Table::set_function(Key&& key, Callable&& callable) const {
    set(std::forward<Key>(key), std::forward<Callable>(callable));
}

template <typename T>
FieldRef& FieldRef::operator=(T&& value) {
    SetIndex(parent_, key_, MakeObject(lua_state(), std::forward<T>(value)));
    return *this;
}

template <typename T>
T FieldRef::get() const {
    return object().template as<T>();
}

template <typename T>
T FieldRef::get_or(T fallback) const {
    Object value = object();
    return value.template is<T>() ? value.template as<T>()
                                  : std::move(fallback);
}

template <typename T>
T FieldRef::get_or_create() const {
    Object value = object();
    if (value.valid()) {
        return value.template as<T>();
    }
    static_assert(std::is_same_v<T, Table>,
                  "Only Lua tables can be created implicitly");
    Table created = StateView(lua_state()).create_table();
    SetIndex(parent_, key_, created);
    return created;
}

template <typename Key>
FieldRef FieldRef::operator[](Key&& key) const {
    return object()[std::forward<Key>(key)];
}

template <typename... Args>
CallResult FieldRef::operator()(Args&&... args) const {
    return call(std::forward<Args>(args)...);
}

template <typename... Args>
CallResult FieldRef::call(Args&&... args) const {
    return Function(object()).call(std::forward<Args>(args)...);
}

template <typename T>
T CallResult::get(std::size_t index) const {
    if (!succeeded_) {
        throw std::runtime_error(error_);
    }
    if (index >= values_.size()) {
        if constexpr (std::is_same_v<T, Object> || std::is_same_v<T, Table> ||
                      std::is_same_v<T, Function>) {
            return T{};
        } else {
            throw std::out_of_range("Lua result index out of range");
        }
    }
    return values_[index].template as<T>();
}

template <typename... Args>
CallResult CallResult::call(Args&&... args) const {
    if (!succeeded_) {
        return CallResult(error_);
    }
    return get<Function>().call(std::forward<Args>(args)...);
}

template <typename... Args>
CallResult CallResult::operator()(Args&&... args) const {
    return call(std::forward<Args>(args)...);
}

template <typename... Args>
CallResult Function::call(Args&&... args) const {
    lua_State* state = lua_state();
    if (state == nullptr) {
        return CallResult("Cannot call an empty function or closed Lua state");
    }
    detail::AccessScope execution(state);
    if (!execution.active()) {
        return CallResult("Lua state is stopping");
    }
    StackGuard stack(state);
    try {
        push(state);
        int argumentCount = 0;
        ((argumentCount += Push(state, std::forward<Args>(args))), ...);
        return ProtectedCall(state, argumentCount);
    } catch (const std::exception& error) {
        return CallResult(error.what());
    } catch (...) {
        return CallResult("Unknown C++ exception while calling Lua");
    }
}

template <typename... Args>
CallResult Function::operator()(Args&&... args) const {
    return call(std::forward<Args>(args)...);
}

namespace detail {
inline void SetInitialTableValues(const Table&) {}
template <typename Key, typename Value, typename... Rest>
void SetInitialTableValues(const Table& table, Key&& key, Value&& value,
                           Rest&&... rest) {
    table.set(std::forward<Key>(key), std::forward<Value>(value));
    SetInitialTableValues(table, std::forward<Rest>(rest)...);
}
}  // namespace detail

template <typename... Values>
Table StateView::create_table_with(Values&&... values) const {
    static_assert(sizeof...(Values) % 2 == 0,
                  "Table initialization requires key/value pairs");
    Table table = create_table(0, sizeof...(Values) / 2);
    detail::SetInitialTableValues(table, std::forward<Values>(values)...);
    return table;
}

template <typename Key>
FieldRef StateView::operator[](Key&& key) const {
    return globals()[std::forward<Key>(key)];
}

template <typename T, typename Key>
T StateView::get(Key&& key) const {
    return globals().template get<T>(std::forward<Key>(key));
}

template <typename Key, typename Value>
void StateView::set(Key&& key, Value&& value) const {
    globals().set(std::forward<Key>(key), std::forward<Value>(value));
}

template <typename Key, typename Callable>
void StateView::set_function(Key&& key, Callable&& callable) const {
    globals().set_function(std::forward<Key>(key),
                           std::forward<Callable>(callable));
}

}  // namespace lua_glue
