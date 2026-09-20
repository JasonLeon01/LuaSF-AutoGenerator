#include <LuaGlue/Value.hpp>

#include <algorithm>
#include <limits>

namespace lua_glue {
namespace {

const char errorHandlerKey{};

int Traceback(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    if (message == nullptr) {
        message = "Lua error (non-string error object)";
    }
    luaL_traceback(state, state, message, 1);
    return 1;
}

int IndexedGet(lua_State* state) {
    lua_settop(state, 2);
    lua_gettable(state, 1);
    return 1;
}

int IndexedSet(lua_State* state) {
    lua_settop(state, 3);
    lua_settable(state, 1);
    return 0;
}

int NextEntry(lua_State* state) {
    lua_settop(state, 2);
    return lua_next(state, 1) ? 2 : 0;
}

int AssignMetatable(lua_State* state) {
    lua_settop(state, 2);
    lua_setmetatable(state, 1);
    return 0;
}

int RawSet(lua_State* state) {
    lua_settop(state, 3);
    lua_rawset(state, 1);
    return 0;
}

int CreateTable(lua_State* state) {
    lua_createtable(state, static_cast<int>(lua_tointeger(state, 1)),
                    static_cast<int>(lua_tointeger(state, 2)));
    return 1;
}

std::string ErrorText(lua_State* state) {
    std::size_t length = 0;
    const char* text = lua_tolstring(state, -1, &length);
    return text ? std::string(text, length)
                : "Lua error (non-string error object)";
}

void RequireState(lua_State* state, const detail::AccessScope& scope) {
    if (state == nullptr || !scope.active()) {
        throw std::runtime_error("Lua state is unavailable or stopping");
    }
}

void CheckObjectType(const Object& object, Type expected) {
    const Type actual = object.get_type();
    if (actual != expected && actual != Type::Nil && actual != Type::None) {
        throw std::invalid_argument(std::string("Expected Lua ") +
                                    TypeName(expected) + ", received " +
                                    TypeName(actual));
    }
}

}  // namespace

const char* TypeName(Type type) noexcept {
    switch (type) {
        case Type::None:
            return "no value";
        case Type::Nil:
            return "nil";
        case Type::Boolean:
            return "boolean";
        case Type::Number:
            return "number";
        case Type::String:
            return "string";
        case Type::Table:
            return "table";
        case Type::Function:
            return "function";
        case Type::Userdata:
            return "userdata";
        case Type::LightUserdata:
            return "light userdata";
        case Type::Thread:
            return "thread";
    }
    return "unknown";
}

StackGuard::StackGuard(lua_State* state) noexcept
    : state_(state), top_(state ? lua_gettop(state) : 0) {}
StackGuard::~StackGuard() {
    if (state_) {
        lua_settop(state_, top_);
    }
}

Object::Object(lua_State* state, int index) {
    if (!state) {
        throw std::invalid_argument("Lua value requires a state");
    }
    if (InitializeState(state) != 0) {
        throw std::runtime_error("Lua state is stopping");
    }
    reference_ = RegistryReference(state, index);
    type_ = static_cast<Type>(lua_type(state, index));
    if (type_ == Type::None) {
        type_ = Type::Nil;
    }
}

Object::Object(const FieldRef& field) : Object(field.object()) {}

bool Object::valid() const noexcept {
    Type type = get_type();
    return type != Type::None && type != Type::Nil;
}

Type Object::get_type() const noexcept {
    return lua_state() ? type_ : Type::None;
}

int Object::push(lua_State* target) const {
    if (!target) {
        target = lua_state();
    }
    if (!target) {
        return 0;
    }
    if (!reference_) {
        lua_pushnil(target);
        return 1;
    }
    if (!reference_.push(target)) {
        throw std::runtime_error(
            "Cannot push a Lua value after its state stopped");
    }
    return 1;
}

bool operator==(const Object& left, const Object& right) {
    if (left == nil && right == nil) {
        return true;
    }
    return left.reference_.equals(right.reference_);
}

Table Object::metatable() const {
    return GetMetatable(*this);
}
void Object::set_metatable(const Table& table) const {
    SetMetatable(*this, table);
}

Table::Table(lua_State* state, int index) : Object(state, index) {
    CheckObjectType(*this, Type::Table);
}
Table::Table(const Object& value) : Object(value) {
    CheckObjectType(*this, Type::Table);
}
Table::Table(Object&& value) : Object(std::move(value)) {
    CheckObjectType(*this, Type::Table);
}
Table::Table(const FieldRef& field) : Table(field.object()) {}

std::size_t Table::size() const {
    lua_State* state = lua_state();
    detail::AccessScope execution(state);
    RequireState(state, execution);
    StackGuard stack(state);
    push(state);
    return lua_rawlen(state, -1);
}

bool Table::empty() const {
    return begin() == end();
}
Table::Iterator Table::begin() const {
    return Iterator(*this);
}
Table::Iterator::Iterator(const Table& table) : table_(table), done_(false) {
    ++*this;
}
Table::Iterator& Table::Iterator::operator++() {
    if (done_) {
        return *this;
    }
    lua_State* state = table_.lua_state();
    detail::AccessScope execution(state);
    RequireState(state, execution);
    StackGuard stack(state);
    lua_pushcfunction(state, NextEntry);
    table_.push(state);
    value_.first.push(state);
    if (lua_pcall(state, 2, LUA_MULTRET, 0) != LUA_OK) {
        throw std::runtime_error(ErrorText(state));
    }
    if (lua_gettop(state) - stack.top() == 2) {
        value_ = {Object(state, -2), Object(state, -1)};
    } else {
        value_ = {};
        done_ = true;
    }
    return *this;
}

FieldRef& FieldRef::operator=(const FieldRef& value) {
    SetIndex(parent_, key_, value.object());
    return *this;
}
FieldRef& FieldRef::operator=(FieldRef&& value) {
    return *this = static_cast<const FieldRef&>(value);
}
Object FieldRef::object() const {
    return GetIndex(parent_, key_);
}

int StackValue::push(lua_State* target) const {
    if (!state_) {
        throw std::invalid_argument(
            "Cannot push an empty borrowed stack value");
    }
    if (!target) {
        target = state_;
    }
    if (target == state_) {
        lua_pushvalue(state_, index_);
    } else {
        Object(state_, index_).push(target);
    }
    return 1;
}

Arguments::Arguments(lua_State* state, int first, int count) noexcept
    : state_(state),
      first_(state ? lua_absindex(state, first) : 1),
      count_(state ? std::max(
                         0, count < 0 ? lua_gettop(state) - first_ + 1 : count)
                   : 0) {}
StackValue Arguments::operator[](std::size_t index) const {
    if (index >= size()) {
        throw std::out_of_range("Lua argument index out of range");
    }
    return StackValue(state_, first_ + static_cast<int>(index));
}
Arguments Arguments::slice(std::size_t offset) const {
    if (offset > size()) {
        throw std::out_of_range("Lua argument slice out of range");
    }
    return Arguments(state_, first_ + static_cast<int>(offset),
                     count_ - static_cast<int>(offset));
}
int Arguments::push(lua_State* target) const {
    if (!target) {
        target = state_;
    }
    if (target == nullptr || !lua_checkstack(target, count_)) {
        throw std::runtime_error("Lua argument stack cannot grow");
    }
    for (int index = 0; index < count_; ++index) {
        StackValue(state_, first_ + index).push(target);
    }
    return count_;
}
int MultipleResults::push(lua_State* target) const {
    if (size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !lua_checkstack(target, static_cast<int>(size()))) {
        throw std::runtime_error("Lua result stack cannot grow");
    }
    for (const auto& value : *this) {
        value.push(target);
    }
    return static_cast<int>(size());
}

void SetErrorHandler(lua_State* state, lua_CFunction handler) {
    StateView initialized(state);
    detail::AccessScope execution(state);
    RequireState(state, execution);
    detail::ProtectedCallOperation(
        state,
        [](lua_State* inner) -> int {
            auto* handler =
                static_cast<lua_CFunction*>(lua_touserdata(inner, 1));
            if (*handler) {
                lua_pushcfunction(inner, *handler);
            } else {
                lua_pushnil(inner);
            }
            lua_rawsetp(inner, LUA_REGISTRYINDEX, &errorHandlerKey);
            return 0;
        },
        &handler);
}

int ProtectedStackCall(lua_State* state, int argumentCount, int resultCount) {
    if (!state || argumentCount < 0 || lua_gettop(state) < argumentCount + 1) {
        throw std::invalid_argument("Invalid Lua protected-call stack");
    }
    if (!lua_checkstack(state, 1)) {
        throw std::runtime_error("Lua protected-call stack cannot grow");
    }
    const int functionIndex = lua_gettop(state) - argumentCount;
    lua_rawgetp(state, LUA_REGISTRYINDEX, &errorHandlerKey);
    if (!lua_isfunction(state, -1)) {
        lua_pop(state, 1);
        lua_pushcfunction(state, Traceback);
    }
    lua_insert(state, functionIndex);
    const int status =
        lua_pcall(state, argumentCount, resultCount, functionIndex);
    if (status != LUA_OK) {
        std::string error = ErrorText(state);
        lua_settop(state, functionIndex - 1);
        throw std::runtime_error(error);
    }
    const int count = lua_gettop(state) - functionIndex;
    lua_remove(state, functionIndex);
    return count;
}

CallResult ProtectedCall(lua_State* state, int argumentCount, int resultCount) {
    if (!state || argumentCount < 0 || lua_gettop(state) < argumentCount + 1) {
        return CallResult("Invalid Lua protected-call stack");
    }
    const int originalTop = lua_gettop(state) - argumentCount - 1;
    try {
        const int count = ProtectedStackCall(state, argumentCount, resultCount);
        std::vector<Object> results;
        results.reserve(count);
        for (int index = 1; index <= count; ++index) {
            results.emplace_back(state, originalTop + index);
        }
        lua_settop(state, originalTop);
        return CallResult(std::move(results));
    } catch (const std::exception& error) {
        lua_settop(state, originalTop);
        return CallResult(error.what());
    }
}

namespace detail {
void ProtectedCallOperation(lua_State* state, lua_CFunction operation,
                            void* context, int resultCount) {
    if (!state || !operation || !lua_checkstack(state, 3)) {
        throw std::runtime_error("Lua operation stack cannot grow");
    }
    const int originalTop = lua_gettop(state);
    lua_pushcfunction(state, operation);
    lua_pushlightuserdata(state, context);
    if (lua_pcall(state, 1, resultCount, 0) != LUA_OK) {
        std::string error = ErrorText(state);
        lua_settop(state, originalTop);
        throw std::runtime_error(error);
    }
}
}  // namespace detail

Function::Function(lua_State* state, int index) : Object(state, index) {
    CheckObjectType(*this, Type::Function);
}
Function::Function(const Object& object) : Object(object) {
    CheckObjectType(*this, Type::Function);
}
Function::Function(Object&& object) : Object(std::move(object)) {
    CheckObjectType(*this, Type::Function);
}
Function::Function(const FieldRef& field) : Function(field.object()) {}

StateView::StateView(lua_State* state) : state_(state) {
    if (InitializeState(state_) != 0) {
        throw std::runtime_error("Lua state is unavailable or stopping");
    }
}
Table StateView::globals() const {
    detail::AccessScope execution(state_);
    RequireState(state_, execution);
    StackGuard stack(state_);
    lua_pushglobaltable(state_);
    return Table(state_, -1);
}
Table StateView::registry() const {
    detail::AccessScope execution(state_);
    RequireState(state_, execution);
    return Table(state_, LUA_REGISTRYINDEX);
}
Table StateView::create_table(int arraySize, int mapSize) const {
    if (arraySize < 0 || mapSize < 0) {
        throw std::invalid_argument("Lua table capacity must not be negative");
    }
    detail::AccessScope execution(state_);
    RequireState(state_, execution);
    StackGuard stack(state_);
    lua_pushcfunction(state_, CreateTable);
    lua_pushinteger(state_, arraySize);
    lua_pushinteger(state_, mapSize);
    if (lua_pcall(state_, 2, 1, 0) != LUA_OK) {
        throw std::runtime_error(ErrorText(state_));
    }
    return Table(state_, -1);
}
CallResult StateView::load(std::string_view source,
                           std::string_view label) const {
    detail::AccessScope execution(state_);
    if (!execution.active()) {
        return CallResult("Lua state is stopping");
    }
    StackGuard stack(state_);
    std::string name(label);
    if (luaL_loadbuffer(state_, source.data(), source.size(), name.c_str()) !=
        LUA_OK) {
        return CallResult(ErrorText(state_));
    }
    return CallResult(std::vector<Object>{Object(state_, -1)});
}
CallResult StateView::script(std::string_view source,
                             std::string_view label) const {
    CallResult loaded = load(source, label);
    if (!loaded.valid()) {
        return loaded;
    }
    return Function(loaded.values().front()).call();
}

Table GetMetatable(const Object& value) {
    lua_State* state = value.lua_state();
    detail::AccessScope execution(state);
    RequireState(state, execution);
    StackGuard stack(state);
    value.push(state);
    if (!lua_getmetatable(state, -1)) {
        lua_pushnil(state);
    }
    return Table(state, -1);
}
void SetMetatable(const Object& value, const Table& metatable) {
    lua_State* state = value.lua_state();
    detail::AccessScope execution(state);
    RequireState(state, execution);
    StackGuard stack(state);
    lua_pushcfunction(state, AssignMetatable);
    value.push(state);
    metatable.push(state);
    if (lua_pcall(state, 2, 0, 0) != LUA_OK) {
        throw std::runtime_error(ErrorText(state));
    }
}
Object GetIndex(const Object& value, const Object& key, bool raw) {
    lua_State* state = value.lua_state();
    detail::AccessScope execution(state);
    RequireState(state, execution);
    StackGuard stack(state);
    key.push(state);
    detail::PushIndex(value, -1, raw);
    return Object(state, -1);
}
void SetIndex(const Object& value, const Object& key, const Object& entry,
              bool raw) {
    lua_State* state = value.lua_state();
    detail::AccessScope execution(state);
    RequireState(state, execution);
    StackGuard stack(state);
    key.push(state);
    entry.push(state);
    detail::AssignIndex(value, -2, -1, raw);
}

namespace detail {
void PushIndex(const Object& value, int keyIndex, bool raw) {
    lua_State* state = value.lua_state();
    if (!state || !lua_checkstack(state, 3)) {
        throw std::runtime_error("Lua index stack cannot grow");
    }
    keyIndex = lua_absindex(state, keyIndex);
    if (raw) {
        value.push(state);
        if (!lua_istable(state, -1)) {
            throw std::invalid_argument("Raw Lua indexing requires a table");
        }
        lua_pushvalue(state, keyIndex);
        lua_rawget(state, -2);
        lua_remove(state, -2);
    } else {
        lua_pushcfunction(state, IndexedGet);
        value.push(state);
        lua_pushvalue(state, keyIndex);
        if (lua_pcall(state, 2, 1, 0) != LUA_OK) {
            throw std::runtime_error(ErrorText(state));
        }
    }
}
void AssignIndex(const Object& value, int keyIndex, int entryIndex, bool raw) {
    lua_State* state = value.lua_state();
    if (!state || !lua_checkstack(state, 4)) {
        throw std::runtime_error("Lua index stack cannot grow");
    }
    keyIndex = lua_absindex(state, keyIndex);
    entryIndex = lua_absindex(state, entryIndex);
    lua_pushcfunction(state, raw ? RawSet : IndexedSet);
    value.push(state);
    if (raw && !lua_istable(state, -1)) {
        throw std::invalid_argument("Raw Lua assignment requires a table");
    }
    lua_pushvalue(state, keyIndex);
    lua_pushvalue(state, entryIndex);
    if (lua_pcall(state, 3, 0, 0) != LUA_OK) {
        throw std::runtime_error(ErrorText(state));
    }
}
}  // namespace detail

PushGuard::PushGuard(const Object& value) : state_(value.lua_state()) {
    if (!state_) {
        throw std::runtime_error(
            "Cannot borrow a value from a closed Lua state");
    }
    index_ = lua_gettop(state_) + 1;
    value.push(state_);
}
PushGuard::~PushGuard() {
    if (state_) {
        lua_settop(state_, index_ - 1);
    }
}
PopGuard::~PopGuard() {
    if (state_ && count_ > 0) {
        lua_pop(state_, count_);
    }
}

}  // namespace lua_glue
