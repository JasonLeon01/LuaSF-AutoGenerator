#include "BindingImpl.hpp"
#include "NativeImpl.hpp"

#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {

constexpr const char* overloadMetatable = "LuaGlue.Overloads.v1";

int overloadGc(lua_State* state) noexcept {
    auto* set = static_cast<lua_glue::detail::OverloadSet*>(
        luaL_testudata(state, 1, overloadMetatable));
    if (set) {
        std::destroy_at(set);
    }
    return 0;
}

bool acceptsArity(lua_State* state,
                  const lua_glue::detail::FunctionRecord& record) {
    const int provided = lua_gettop(state);
    const int full = static_cast<int>(record.parameters.size());
    if (!record.variadic && provided > full) {
        return false;
    }
    const int required = full - static_cast<int>(record.defaults.size());
    if (provided >= required) {
        return true;
    }
    // Lua supplies nil for omitted nullable arguments, including const
    // optional references. Explicit binding defaults take precedence below.
    if (!lua_checkstack(state, required - provided)) {
        throw std::runtime_error("Lua argument stack cannot grow");
    }
    lua_glue::StackGuard guard(state);
    for (int index = provided; index < required; ++index) {
        lua_pushnil(state);
        if (!record.parameters[static_cast<std::size_t>(index)].check(
                state, index + 1)) {
            return false;
        }
    }
    return true;
}

int invoke(lua_State* state, lua_glue::detail::FunctionRecord& record) {
    const int provided = lua_gettop(state);
    const int full = static_cast<int>(record.parameters.size());
    const int required = full - static_cast<int>(record.defaults.size());
    if (!acceptsArity(state, record)) {
        throw std::runtime_error("wrong number of arguments for " +
                                 record.name + record.signature);
    }
    for (int index = provided; index < full; ++index) {
        if (index < required) {
            lua_pushnil(state);
            continue;
        }
        const auto& value =
            record.defaults[static_cast<std::size_t>(index - required)];
        if (value.push(state, value.storage.get()) != 1) {
            throw std::runtime_error(
                "a binding default must produce one Lua value");
        }
    }
    const int before = lua_gettop(state);
    const int results = record.invoke(state, record.storage.get(),
                                      record.options.return_policy);
    if (results < 0 || lua_gettop(state) < before + results) {
        throw std::runtime_error("binding returned an invalid result count");
    }
    if (record.options.return_policy ==
            lua_glue::ReturnPolicy::ReferenceInternal &&
        provided > 0) {
        for (int index = 0; index < results; ++index) {
            lua_glue::RetainDependency(state, -results + index, 1);
        }
    }
    for (const auto& [dependent, owner] : record.options.keep_alive) {
        if (owner < 1 || owner > provided) {
            continue;
        }
        if (dependent == 0) {
            for (int index = 0; index < results; ++index) {
                lua_glue::RetainDependency(state, -results + index, owner);
            }
        } else if (dependent > 0 && dependent <= provided) {
            lua_glue::RetainDependency(state, dependent, owner);
        }
    }
    return results;
}

int dispatchImpl(lua_State* state, lua_glue::detail::OverloadSet& set) {
    // Lua invokes unary operators with the operand repeated in slot two.
    // Fixed C++ signatures receive only the meaningful operand.
    if (set.unaryMetamethod && lua_gettop(state) == 2 &&
        lua_rawequal(state, 1, 2)) {
        lua_settop(state, 1);
    }
    const int count = lua_gettop(state);
    lua_glue::detail::FunctionRecord* selected = nullptr;
    int best = std::numeric_limits<int>::max();
    for (const auto& candidate : set.functions) {
        const int full = static_cast<int>(candidate->parameters.size());
        if (!acceptsArity(state, *candidate)) {
            continue;
        }
        bool matches = true;
        int rank = candidate->variadic ? 1000 : 0;
        for (int i = 0; i < count && i < full; ++i) {
            const auto& parameter =
                candidate->parameters[static_cast<std::size_t>(i)];
            if (!parameter.check(state, i + 1)) {
                matches = false;
                break;
            }
            rank += parameter.rank(state, i + 1);
        }
        if (matches && rank < best) {
            best = rank;
            selected = candidate.get();
        }
    }
    if (selected) {
        return invoke(state, *selected);
    }
    std::string message = "no matching overload for ";
    message += set.functions.empty() ? "function" : set.functions.front()->name;
    message += "(";
    for (int i = 1; i <= count; ++i) {
        if (i > 1) {
            message += ", ";
        }
        message += lua_typename(state, lua_type(state, i));
    }
    message += "). Candidates:";
    for (const auto& candidate : set.functions) {
        message += "\n  " + candidate->name + candidate->signature;
    }
    throw std::runtime_error(message);
}

int dispatch(lua_State* state) noexcept {
    char error[4096]{};
    try {
        auto* set = static_cast<lua_glue::detail::OverloadSet*>(
            lua_touserdata(state, lua_upvalueindex(1)));
        if (!set) {
            throw std::runtime_error("invalid LuaGlue function closure");
        }
        return dispatchImpl(state, *set);
    } catch (const std::exception& exception) {
        std::snprintf(error, sizeof(error), "%s", exception.what());
    } catch (...) {
        std::snprintf(error, sizeof(error),
                      "unknown C++ exception in LuaGlue binding");
    }
    // Only trivial storage remains when Lua performs its non-local error jump.
    lua_pushstring(state, error);
    return lua_error(state);
}

struct FunctionContext {
    lua_glue::detail::OverloadSet* set;
};

int createFunction(lua_State* state) {
    auto* context = static_cast<FunctionContext*>(lua_touserdata(state, 1));
    if (luaL_newmetatable(state, overloadMetatable)) {
        lua_pushcfunction(state, overloadGc);
        lua_setfield(state, -2, "__gc");
    }
    lua_pop(state, 1);
    void* memory =
        lua_newuserdatauv(state, sizeof(lua_glue::detail::OverloadSet), 0);
    std::construct_at(static_cast<lua_glue::detail::OverloadSet*>(memory),
                      std::move(*context->set));
    luaL_setmetatable(state, overloadMetatable);
    lua_pushcclosure(state, dispatch, 1);
    return 1;
}

lua_glue::detail::OverloadSet* getOverloads(lua_State* state, int index) {
    if (!lua_iscfunction(state, index) ||
        lua_tocfunction(state, index) != dispatch) {
        return nullptr;
    }
    if (!lua_getupvalue(state, index, 1)) {
        return nullptr;
    }
    auto* result = static_cast<lua_glue::detail::OverloadSet*>(
        luaL_testudata(state, -1, overloadMetatable));
    lua_pop(state, 1);
    return result;
}

void addOverload(lua_glue::detail::OverloadSet& set,
                 std::shared_ptr<lua_glue::detail::FunctionRecord> record) {
    for (auto& existing : set.functions) {
        if (existing->signature == record->signature) {
            existing = std::move(record);
            return;
        }
    }
    set.functions.push_back(std::move(record));
}

void installFunction(const lua_glue::Table& table, std::string_view name,
                     std::shared_ptr<lua_glue::detail::FunctionRecord> record,
                     bool unaryMetamethod = false) {
    lua_State* state = table.lua_state();
    lua_glue::StackGuard guard(state);
    table.raw_get<lua_glue::Object>(name).push(state);
    if (auto* set = getOverloads(state, -1)) {
        addOverload(*set, std::move(record));
        set->unaryMetamethod = unaryMetamethod;
        return;
    }
    lua_pop(state, 1);
    lua_glue::detail::PushFunctionValue(state, std::move(record));
    getOverloads(state, -1)->unaryMetamethod = unaryMetamethod;
    table.raw_set(name, lua_glue::Object(state, -1));
}

int pushString(lua_State* state) {
    auto* value = static_cast<std::string_view*>(lua_touserdata(state, 1));
    lua_pushlstring(state, value->data(), value->size());
    return 1;
}

int referenceFunction(lua_State* state) {
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_pushinteger(state, reference);
    return 1;
}

int protectedReference(lua_State* state) {
    lua_pushcfunction(state, referenceFunction);
    lua_insert(state, -2);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        std::string error =
            message ? message : "could not retain native property";
        lua_pop(state, 1);
        throw std::runtime_error(error);
    }
    const int reference = static_cast<int>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    return reference;
}

}  // namespace

namespace lua_glue::detail {

int PushString(lua_State* state, std::string_view value) {
    ProtectedCallOperation(state, pushString, &value, 1);
    return 1;
}

int PushFunctionValue(lua_State* state,
                      std::shared_ptr<FunctionRecord> record) {
    OverloadSet set;
    set.functions.push_back(std::move(record));
    FunctionContext context{&set};
    ProtectedCallOperation(state, createFunction, &context, 1);
    return 1;
}

void PushFunction(lua_State* state,
                  const std::shared_ptr<FunctionRecord>& record) {
    if (record->cachedReference >= 0) {
        lua_rawgeti(state, LUA_REGISTRYINDEX, record->cachedReference);
    } else {
        PushFunctionValue(state, record);
    }
}

int InvokeFunction(lua_State* state,
                   const std::shared_ptr<FunctionRecord>& record) {
    return invoke(state, *record);
}

void RegisterFunction(const Table& table, std::string_view name,
                      std::shared_ptr<FunctionRecord> record) {
    record->name = name;
    installFunction(table, name, std::move(record));
}

void RegisterProperty(const Table& table, std::string_view name,
                      std::shared_ptr<FunctionRecord> getter,
                      std::shared_ptr<FunctionRecord> setter, bool isStatic) {
    lua_State* state = table.lua_state();
    StackGuard guard(state);
    table.push(state);
    auto* type = GetTypeRecord(state, -1);
    if (!type && isStatic) {
        type = PreparePropertyScope(table);
    }
    if (!type) {
        throw std::runtime_error(
            "properties require a registered LuaGlue type");
    }
    getter->name = type->luaName + "." + std::string(name);
    if (setter) {
        setter->name = getter->name;
    }
    auto& properties = isStatic ? type->staticProperties : type->properties;
    const std::string key(name);
    auto found = properties.find(key);
    const bool isNew = found == properties.end();
    PropertyRecord previous = isNew ? PropertyRecord{} : found->second;
    bool mapChanged = false;
    auto release = [state](const std::shared_ptr<FunctionRecord>& function) {
        if (function && function->cachedReference >= 0) {
            luaL_unref(state, LUA_REGISTRYINDEX, function->cachedReference);
            function->cachedReference = LUA_NOREF;
        }
    };
    try {
        auto clone = [state](const Table& original) {
            Table result = StateView(state).create_table();
            for (const auto& [entryKey, value] : original) {
                result.raw_set(entryKey, value);
            }
            return result;
        };
        Table getters = clone(table.raw_get<Table>("__getters"));
        Table setters = clone(table.raw_get<Table>("__setters"));
        Table names = clone(table.raw_get<Table>("__nativeProperties"));
        PushFunctionValue(state, getter);
        getters.raw_set(name, Object(state, -1));
        getter->cachedReference = protectedReference(state);
        if (setter) {
            PushFunctionValue(state, setter);
            setters.raw_set(name, Object(state, -1));
            setter->cachedReference = protectedReference(state);
        } else {
            setters.raw_set(name, nil);
        }
        if (isNew && !isStatic) {
            names.add(name);
        }
        Object nameObject = MakeObject(state, name);
        if (!lua_checkstack(state, 7)) {
            throw std::runtime_error("not enough stack to register property");
        }
        // Stage every allocating operation before replacing either native or
        // Lua-visible metadata. These existing class-table slots cannot grow.
        lua_pushcfunction(
            state, +[](lua_State* inner) -> int {
                lua_pushvalue(inner, 2);
                lua_setfield(inner, 1, "__getters");
                lua_pushvalue(inner, 3);
                lua_setfield(inner, 1, "__setters");
                lua_pushvalue(inner, 4);
                lua_setfield(inner, 1, "__nativeProperties");
                lua_pushvalue(inner, 5);
                lua_pushnil(inner);
                lua_rawset(inner, 1);
                return 0;
            });
        table.push(state);
        getters.push(state);
        setters.push(state);
        names.push(state);
        nameObject.push(state);
        properties.insert_or_assign(key, PropertyRecord{getter, setter});
        mapChanged = true;
        if (lua_pcall(state, 5, 0, 0) != LUA_OK) {
            const char* text = lua_tostring(state, -1);
            std::string message = text ? text : "could not register property";
            lua_pop(state, 1);
            throw std::runtime_error(message);
        }
        release(previous.getter);
        release(previous.setter);
    } catch (...) {
        if (mapChanged) {
            if (isNew) {
                properties.erase(key);
            } else {
                properties.find(key)->second = std::move(previous);
            }
        }
        release(getter);
        release(setter);
        throw;
    }
}

void RegisterMetamethod(const Table& table, std::string_view name,
                        std::shared_ptr<FunctionRecord> record) {
    lua_State* state = table.lua_state();
    StackGuard guard(state);
    table.push(state);
    auto* type = GetTypeRecord(state, -1);
    if (!type) {
        throw std::runtime_error("metamethod requires a registered type");
    }
    lua_getfield(state, LUA_REGISTRYINDEX, type->metatableName.c_str());
    Table metatable(state, -1);
    record->name = type->luaName + "." + std::string(name);
    if (name == "__gc") {
        throw std::invalid_argument(
            "native userdata destruction is owned by LuaGlue; use an owning "
            "pointer deleter");
    }
    installFunction(metatable, name, std::move(record),
                    name == "__len" || name == "__unm" || name == "__bnot");
}

}  // namespace lua_glue::detail
