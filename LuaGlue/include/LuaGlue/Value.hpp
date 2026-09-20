#pragma once

#include <LuaGlue/Api.hpp>
#include <LuaGlue/Lifecycle.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lua_glue {

template <typename T>
T Read(lua_State* state, int index);
template <typename T>
bool Check(lua_State* state, int index);
template <typename T>
int Push(lua_State* state, T&& value);

enum class Type : int {
    None = LUA_TNONE,
    Nil = LUA_TNIL,
    Boolean = LUA_TBOOLEAN,
    LightUserdata = LUA_TLIGHTUSERDATA,
    Number = LUA_TNUMBER,
    String = LUA_TSTRING,
    Table = LUA_TTABLE,
    Function = LUA_TFUNCTION,
    Userdata = LUA_TUSERDATA,
    Thread = LUA_TTHREAD
};
LUAGLUE_API const char* TypeName(Type type) noexcept;
inline const char* TypeName(lua_State*, Type type) noexcept {
    return TypeName(type);
}
namespace detail {
LUAGLUE_API void ProtectedCallOperation(lua_State* state,
                                        lua_CFunction operation, void* context,
                                        int resultCount = 0);
}
struct Nil {};
inline constexpr Nil nil{};
struct LightUserdata {
    void* value{};
    explicit LightUserdata(void* pointer = nullptr) noexcept : value(pointer) {}
    operator void*() const noexcept {
        return value;
    }
};
struct ThisState {
    lua_State* value{};
    ThisState(lua_State* state) noexcept : value(state) {}
    operator lua_State*() const noexcept {
        return value;
    }
};

class Object;
class Table;
class Function;
class FieldRef;
class CallResult;
class StateView;

class LUAGLUE_API StackGuard {
public:
    explicit StackGuard(lua_State* state) noexcept;
    ~StackGuard();
    StackGuard(const StackGuard&) = delete;
    StackGuard& operator=(const StackGuard&) = delete;
    int top() const noexcept {
        return top_;
    }
    void dismiss() noexcept {
        state_ = nullptr;
    }

private:
    lua_State* state_{};
    int top_{};
};

class LUAGLUE_API Object {
public:
    Object() noexcept = default;
    Object(Nil) noexcept {}
    Object(lua_State* state, int index);
    Object(const FieldRef& field);
    lua_State* lua_state() const noexcept {
        return reference_.originState();
    }
    bool valid() const noexcept;
    explicit operator bool() const noexcept {
        return valid();
    }
    Type get_type() const noexcept;
    int push(lua_State* target = nullptr) const;
    template <typename T>
    T as() const;
    template <typename T>
    T get() const {
        return as<T>();
    }
    template <typename T>
    bool is() const;
    template <typename Key>
    FieldRef operator[](Key&& key) const;
    Table metatable() const;
    void set_metatable(const Table& table) const;
    const RegistryReference& reference() const noexcept {
        return reference_;
    }
    friend LUAGLUE_API bool operator==(const Object& left, const Object& right);
    friend bool operator==(const Object& left, Nil) noexcept {
        return left.get_type() == Type::Nil || left.get_type() == Type::None;
    }
    friend bool operator==(Nil, const Object& right) noexcept {
        return right == nil;
    }

private:
    RegistryReference reference_;
    // A registry reference retains a value, whose Lua type cannot change.
    Type type_{Type::None};
};

class LUAGLUE_API Table : public Object {
public:
    Table() noexcept = default;
    Table(Nil) noexcept {}
    Table(lua_State* state, int index);
    Table(const Object& value);
    Table(Object&& value);
    Table(const FieldRef& field);
    template <typename T = Object, typename Key>
    T get(Key&& key) const;
    template <typename T = Object, typename Key>
    T raw_get(Key&& key) const;
    template <typename Key, typename Value>
    void set(Key&& key, Value&& value) const;
    template <typename Key, typename Value>
    void raw_set(Key&& key, Value&& value) const;
    template <typename T = Table, typename Key>
    T get_or_create(Key&& key) const;
    template <typename T, typename Key>
    T get_or(Key&& key, T fallback) const;
    template <typename T>
    void add(T&& value) const;
    template <typename Key, typename Callable>
    void set_function(Key&& key, Callable&& callable) const;
    std::size_t size() const;
    bool empty() const;
    class LUAGLUE_API Iterator {
    public:
        using value_type = std::pair<Object, Object>;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using pointer = const value_type*;
        using reference = const value_type&;
        Iterator() = default;
        explicit Iterator(const Table& table);
        reference operator*() const noexcept {
            return value_;
        }
        pointer operator->() const noexcept {
            return &value_;
        }
        Iterator& operator++();
        Iterator operator++(int) {
            Iterator previous(*this);
            ++*this;
            return previous;
        }
        friend bool operator==(const Iterator& left, const Iterator& right) {
            return left.done_ == right.done_ &&
                   (left.done_ || (left.table_ == right.table_ &&
                                   left.value_.first == right.value_.first));
        }

    private:
        Object table_;
        value_type value_;
        bool done_{true};
    };
    Iterator begin() const;
    Iterator end() const noexcept {
        return {};
    }
};

class LUAGLUE_API FieldRef {
public:
    FieldRef(Object parent, Object key)
        : parent_(std::move(parent)), key_(std::move(key)) {}
    FieldRef(const FieldRef&) = default;
    FieldRef(FieldRef&&) = default;
    FieldRef& operator=(const FieldRef& value);
    FieldRef& operator=(FieldRef&& value);
    template <typename T>
    FieldRef& operator=(T&& value);
    Object object() const;
    template <typename T = Object>
    T get() const;
    template <typename T>
    T get_or(T fallback) const;
    template <typename T = Table>
    T get_or_create() const;
    template <typename T>
    T as() const {
        return get<T>();
    }
    template <typename T>
    bool is() const {
        return object().template is<T>();
    }
    bool valid() const {
        return object().valid();
    }
    Type get_type() const {
        return object().get_type();
    }
    template <typename T>
    operator T() const {
        return get<T>();
    }
    template <typename Key>
    FieldRef operator[](Key&& key) const;
    lua_State* lua_state() const noexcept {
        return parent_.lua_state();
    }
    template <typename... Args>
    CallResult operator()(Args&&... args) const;
    template <typename... Args>
    CallResult call(Args&&... args) const;

private:
    Object parent_;
    Object key_;
};

class LUAGLUE_API StackValue {
public:
    StackValue() noexcept = default;
    StackValue(lua_State* state, int index) noexcept
        : state_(state), index_(lua_absindex(state, index)) {}
    lua_State* lua_state() const noexcept {
        return state_;
    }
    int stack_index() const noexcept {
        return index_;
    }
    int get_index() const noexcept {
        return index_;
    }
    int push(lua_State* target = nullptr) const;
    Type get_type() const noexcept {
        return state_ ? static_cast<Type>(lua_type(state_, index_))
                      : Type::None;
    }
    bool valid() const noexcept {
        return get_type() != Type::None && get_type() != Type::Nil;
    }
    template <typename T>
    T as() const {
        return Read<T>(state_, index_);
    }
    template <typename T = Object>
    T get() const {
        return as<T>();
    }
    template <typename T>
    bool is() const {
        return state_ && Check<T>(state_, index_);
    }
    template <typename T>
    operator T() const {
        return as<T>();
    }

private:
    lua_State* state_{};
    int index_{};
};

class LUAGLUE_API Arguments {
public:
    explicit Arguments(lua_State* state, int first = 1,
                       int count = -1) noexcept;
    lua_State* lua_state() const noexcept {
        return state_;
    }
    std::size_t size() const noexcept {
        return static_cast<std::size_t>(count_);
    }
    bool empty() const noexcept {
        return count_ == 0;
    }
    int stack_index() const noexcept {
        return first_;
    }
    StackValue operator[](std::size_t index) const;
    template <typename T>
    T get(std::size_t index = 0) const {
        return (*this)[index].template as<T>();
    }
    Arguments slice(std::size_t offset) const;
    int push(lua_State* target = nullptr) const;
    class Iterator {
    public:
        using value_type = StackValue;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        Iterator(lua_State* state, int index) : state_(state), index_(index) {}
        StackValue operator*() const {
            return StackValue(state_, index_);
        }
        Iterator& operator++() {
            ++index_;
            return *this;
        }
        Iterator operator++(int) {
            Iterator previous(*this);
            ++*this;
            return previous;
        }
        friend bool operator==(const Iterator&, const Iterator&) = default;

    private:
        lua_State* state_{};
        int index_{};
    };
    Iterator begin() const {
        return Iterator(state_, first_);
    }
    Iterator end() const {
        return Iterator(state_, first_ + count_);
    }

private:
    lua_State* state_{};
    int first_{1};
    int count_{};
};

class LUAGLUE_API MultipleResults : public std::vector<Object> {
public:
    using std::vector<Object>::vector;
    int push(lua_State* target) const;
};

class LUAGLUE_API CallResult {
public:
    CallResult() noexcept = default;
    explicit CallResult(std::string error)
        : error_(std::move(error)), succeeded_(false) {}
    explicit CallResult(std::vector<Object> values)
        : values_(std::move(values)) {}
    bool valid() const noexcept {
        return succeeded_;
    }
    explicit operator bool() const noexcept {
        return valid();
    }
    int return_count() const noexcept {
        return static_cast<int>(values_.size());
    }
    const std::string& error() const noexcept {
        return error_;
    }
    Type get_type(std::size_t index = 0) const noexcept {
        return index < values_.size() ? values_[index].get_type() : Type::None;
    }
    template <typename T = Object>
    T get(std::size_t index = 0) const;
    template <typename T>
    operator T() const {
        return get<T>();
    }
    const std::vector<Object>& values() const noexcept {
        return values_;
    }
    template <typename... Args>
    CallResult call(Args&&... args) const;
    template <typename... Args>
    CallResult operator()(Args&&... args) const;

private:
    std::vector<Object> values_;
    std::string error_;
    bool succeeded_{true};
};

// Function and metamethod calls cross a Lua protected boundary. C++ callers
// receive a result or exception only after the Lua stack has been restored.
LUAGLUE_API void SetErrorHandler(lua_State* state, lua_CFunction handler);
LUAGLUE_API int ProtectedStackCall(lua_State* state, int argumentCount,
                                   int resultCount = LUA_MULTRET);
LUAGLUE_API CallResult ProtectedCall(lua_State* state, int argumentCount,
                                     int resultCount = LUA_MULTRET);

class LUAGLUE_API Function : public Object {
public:
    Function() noexcept = default;
    Function(Nil) noexcept {}
    Function(lua_State* state, int index);
    Function(const Object& object);
    Function(Object&& object);
    Function(const FieldRef& field);
    template <typename... Args>
    CallResult call(Args&&... args) const;
    template <typename... Args>
    CallResult operator()(Args&&... args) const;
};

class LUAGLUE_API StateView {
public:
    explicit StateView(lua_State* state);
    StateView(ThisState state) : StateView(static_cast<lua_State*>(state)) {}
    lua_State* lua_state() const noexcept {
        return state_;
    }
    operator lua_State*() const noexcept {
        return state_;
    }
    Table globals() const;
    Table registry() const;
    Table create_table(int arraySize = 0, int mapSize = 0) const;
    template <typename... Values>
    Table create_table_with(Values&&... values) const;
    template <typename Key>
    FieldRef operator[](Key&& key) const;
    template <typename T = Object, typename Key>
    T get(Key&& key) const;
    template <typename Key, typename Value>
    void set(Key&& key, Value&& value) const;
    template <typename Key, typename Callable>
    void set_function(Key&& key, Callable&& callable) const;
    CallResult load(std::string_view source,
                    std::string_view label = "chunk") const;
    CallResult script(std::string_view source,
                      std::string_view label = "chunk") const;

private:
    lua_State* state_{};
};

template <typename T>
Object MakeObject(lua_State* state, T&& value);
template <typename T>
Object MakeObject(StateView state, T&& value);
LUAGLUE_API Table GetMetatable(const Object& value);
LUAGLUE_API void SetMetatable(const Object& value, const Table& metatable);
LUAGLUE_API Object GetIndex(const Object& value, const Object& key,
                            bool raw = false);
LUAGLUE_API void SetIndex(const Object& value, const Object& key,
                          const Object& entry, bool raw = false);

namespace detail {
// The caller holds an execution scope and owns the argument stack slots.
// Reads append one result; writes leave the existing slots untouched.
LUAGLUE_API void PushIndex(const Object& value, int keyIndex, bool raw);
LUAGLUE_API void AssignIndex(const Object& value, int keyIndex, int entryIndex,
                             bool raw);
}  // namespace detail

class LUAGLUE_API PushGuard {
public:
    explicit PushGuard(const Object& value);
    ~PushGuard();
    PushGuard(const PushGuard&) = delete;
    PushGuard& operator=(const PushGuard&) = delete;
    int index() const noexcept {
        return index_;
    }
    int get_index() const noexcept {
        return index_;
    }

private:
    lua_State* state_{};
    int index_{};
};
class LUAGLUE_API PopGuard {
public:
    PopGuard(lua_State* state, int count) noexcept
        : state_(state), count_(count) {}
    ~PopGuard();
    PopGuard(const PopGuard&) = delete;
    PopGuard& operator=(const PopGuard&) = delete;

private:
    lua_State* state_{};
    int count_{};
};

}  // namespace lua_glue

#include <LuaGlue/Value.inl>
