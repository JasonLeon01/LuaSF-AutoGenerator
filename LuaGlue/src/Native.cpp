#include "NativeImpl.hpp"

#include <LuaGlue/Lifecycle.hpp>
#include <LuaGlue/Binding.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <new>
#include <cstdio>
#include <stdexcept>

namespace {

constexpr const char* typesKey = "LuaGlue.Types.v1";
char nativeTypeKey;
char externalResolversKey;
constexpr const char* recordMetatable = "LuaGlue.TypeRecord.v1";

lua_glue::detail::TypeRecord* closureType(lua_State* state) {
    return static_cast<lua_glue::detail::TypeRecord*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

lua_glue::detail::PropertyRecord* findProperty(
    lua_glue::detail::TypeRecord* type, std::string_view key, bool isStatic) {
    auto& properties = isStatic ? type->staticProperties : type->properties;
    auto found = properties.find(std::string(key));
    if (found != properties.end()) {
        return &found->second;
    }
    for (auto& base : type->bases) {
        if (!base.visible || !base.type) {
            continue;
        }
        if (auto* property = findProperty(base.type, key, isStatic)) {
            return property;
        }
    }
    return nullptr;
}

bool pushMember(lua_State* state, lua_glue::detail::TypeRecord* type,
                int keyIndex) {
    keyIndex = lua_absindex(state, keyIndex);
    lua_getfield(state, LUA_REGISTRYINDEX, type->classRegistryName.c_str());
    lua_pushvalue(state, keyIndex);
    lua_rawget(state, -2);
    lua_remove(state, -2);
    if (!lua_isnil(state, -1)) {
        return true;
    }
    lua_pop(state, 1);
    for (auto& base : type->bases) {
        if (!base.visible || !base.type) {
            continue;
        }
        if (pushMember(state, base.type, keyIndex)) {
            return true;
        }
    }
    return false;
}

int nativeIndex(lua_State* state) {
    auto* type = closureType(state);
    const char* key =
        lua_type(state, 2) == LUA_TSTRING ? lua_tostring(state, 2) : nullptr;
    auto* property = key ? findProperty(type, key, false) : nullptr;
    if (property && property->getter) {
        lua_glue::detail::PushFunction(state, property->getter);
        lua_pushvalue(state, 1);
        lua_call(state, 1, 1);
        return 1;
    }
    if (pushMember(state, type, 2)) {
        return 1;
    }
    // Per-instance dependency storage is separate from bound C++ fields.
    // Unknown reads deliberately remain nil, matching ordinary Lua table
    // lookup.
    lua_pushnil(state);
    return 1;
}

int nativeNewIndex(lua_State* state) {
    auto* type = closureType(state);
    const char* key =
        lua_type(state, 2) == LUA_TSTRING ? lua_tostring(state, 2) : nullptr;
    auto* property = key ? findProperty(type, key, false) : nullptr;
    if (!property || !property->setter) {
        return luaL_error(state, "%s.%s is not a writable instance property",
                          type->luaName.c_str(), key ? key : "?");
    }
    if (lua_glue::NativeIsConst(state, 1)) {
        return luaL_error(state, "cannot modify a const %s",
                          type->luaName.c_str());
    }
    lua_glue::detail::PushFunction(state, property->setter);
    lua_pushvalue(state, 1);
    lua_pushvalue(state, 3);
    lua_call(state, 2, 0);
    return 0;
}

int classIndex(lua_State* state) {
    auto* type = closureType(state);
    const char* key =
        lua_type(state, 2) == LUA_TSTRING ? lua_tostring(state, 2) : nullptr;
    auto* property = key ? findProperty(type, key, true) : nullptr;
    if (property && property->getter) {
        lua_glue::detail::PushFunction(state, property->getter);
        lua_call(state, 0, 1);
        return 1;
    }
    for (auto& base : type->bases) {
        if (!base.visible || !base.type) {
            continue;
        }
        if (pushMember(state, base.type, 2)) {
            return 1;
        }
    }
    if (lua_getmetatable(state, 1)) {
        lua_getfield(state, -1, "__LuaGlueFallbackIndex");
        if (lua_isfunction(state, -1)) {
            lua_pushvalue(state, 1);
            lua_pushvalue(state, 2);
            lua_call(state, 2, 1);
            return 1;
        }
        if (lua_istable(state, -1)) {
            lua_pushvalue(state, 2);
            lua_gettable(state, -2);
            return 1;
        }
    }
    lua_pushnil(state);
    return 1;
}

int classNewIndex(lua_State* state) {
    auto* type = closureType(state);
    const char* key =
        lua_type(state, 2) == LUA_TSTRING ? lua_tostring(state, 2) : nullptr;
    auto* property = key ? findProperty(type, key, true) : nullptr;
    if (!property) {
        if (lua_getmetatable(state, 1)) {
            lua_getfield(state, -1, "__LuaGlueFallbackNewIndex");
            if (lua_isfunction(state, -1)) {
                lua_pushvalue(state, 1);
                lua_pushvalue(state, 2);
                lua_pushvalue(state, 3);
                lua_call(state, 3, 0);
                return 0;
            }
            if (lua_istable(state, -1)) {
                lua_pushvalue(state, 2);
                lua_pushvalue(state, 3);
                lua_settable(state, -3);
                return 0;
            }
        }
        lua_pushvalue(state, 2);
        lua_pushvalue(state, 3);
        lua_rawset(state, 1);
        return 0;
    }
    if (!property->setter) {
        return luaL_error(state, "%s.%s is read-only", type->luaName.c_str(),
                          key);
    }
    lua_glue::detail::PushFunction(state, property->setter);
    lua_pushvalue(state, 3);
    lua_call(state, 1, 0);
    return 0;
}

int nativeGc(lua_State* state) noexcept {
    auto* storage = lua_glue::detail::GetNativeStorage(state, 1);
    if (storage) {
        storage->magic = 0;
        std::destroy_at(storage);
    }
    return 0;
}

int typeGc(lua_State* state) noexcept {
    auto* record = static_cast<lua_glue::detail::TypeRecord*>(
        luaL_testudata(state, 1, recordMetatable));
    if (record) {
        for (auto* properties :
             {&record->properties, &record->staticProperties}) {
            for (auto& [name, property] : *properties) {
                for (const auto& function :
                     {property.getter, property.setter}) {
                    if (function && function->cachedReference >= 0) {
                        luaL_unref(state, LUA_REGISTRYINDEX,
                                   function->cachedReference);
                        function->cachedReference = LUA_NOREF;
                    }
                }
            }
        }
        std::destroy_at(record);
    }
    return 0;
}

int typeIs(lua_State* state) {
    auto* type = closureType(state);
    lua_pushboolean(
        state, lua_glue::NativePointer(state, 1, type->cppName) != nullptr);
    return 1;
}

template <lua_CFunction Function>
int protectedEntry(lua_State* state) noexcept {
    char error[2048]{};
    try {
        return Function(state);
    } catch (const std::exception& exception) {
        std::snprintf(error, sizeof(error), "%s", exception.what());
    } catch (...) {
        std::snprintf(error, sizeof(error),
                      "unknown native type operation error");
    }
    lua_pushstring(state, error);
    return lua_error(state);
}

void pushTypeFunction(lua_State* state, lua_glue::detail::TypeRecord* type,
                      lua_CFunction function) {
    lua_pushlightuserdata(state, type);
    lua_pushcclosure(state, function, 1);
}

struct RegisterContext {
    lua_glue::detail::TypeRecord* record;
    bool transferred = false;
};

int createType(lua_State* state) {
    auto* context = static_cast<RegisterContext*>(lua_touserdata(state, 1));
    lua_getfield(state, LUA_REGISTRYINDEX, typesKey);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushvalue(state, -1);
        lua_setfield(state, LUA_REGISTRYINDEX, typesKey);
    }
    const int types = lua_absindex(state, -1);
    if (luaL_newmetatable(state, recordMetatable)) {
        lua_pushcfunction(state, typeGc);
        lua_setfield(state, -2, "__gc");
    }
    const int recordMeta = lua_absindex(state, -1);
    // All non-trivial initialization occurs before any further allocating Lua
    // operation. The type registry owns the record for the rest of this VM.
    void* memory =
        lua_newuserdatauv(state, sizeof(lua_glue::detail::TypeRecord), 0);
    auto* record =
        std::construct_at(static_cast<lua_glue::detail::TypeRecord*>(memory),
                          std::move(*context->record));
    context->record = record;
    context->transferred = true;
    lua_pushvalue(state, recordMeta);
    lua_setmetatable(state, -2);
    lua_setfield(state, types, record->cppName.c_str());

    lua_newtable(state);
    const int typeTable = lua_absindex(state, -1);
    lua_pushvalue(state, typeTable);
    lua_setfield(state, LUA_REGISTRYINDEX, record->classRegistryName.c_str());
    lua_pushboolean(state, record->valueType);
    lua_setfield(state, typeTable, "__valueType");
    for (const char* name :
         {"__getters", "__setters", "__nativeProperties", "__nativeBases"}) {
        lua_newtable(state);
        lua_setfield(state, typeTable, name);
    }
    lua_newtable(state);
    const int typeInfo = lua_absindex(state, -1);
    lua_pushlstring(state, record->cppName.data(), record->cppName.size());
    lua_setfield(state, typeInfo, "name");
    pushTypeFunction(state, record, protectedEntry<typeIs>);
    lua_setfield(state, typeInfo, "is");
    lua_pushvalue(state, typeInfo);
    lua_setfield(state, typeTable, "__type");

    lua_newtable(state);
    lua_pushlightuserdata(state, record);
    lua_setfield(state, -2, "__LuaGlueType");
    lua_pushlightuserdata(state, record);
    lua_rawsetp(state, -2, &nativeTypeKey);
    lua_pushvalue(state, typeInfo);
    lua_setfield(state, -2, "__type");
    pushTypeFunction(state, record, protectedEntry<classIndex>);
    lua_setfield(state, -2, "__index");
    pushTypeFunction(state, record, protectedEntry<classNewIndex>);
    lua_setfield(state, -2, "__newindex");
    lua_setmetatable(state, typeTable);

    luaL_newmetatable(state, record->metatableName.c_str());
    lua_pushlightuserdata(state, record);
    lua_setfield(state, -2, "__LuaGlueType");
    lua_pushlightuserdata(state, record);
    lua_rawsetp(state, -2, &nativeTypeKey);
    lua_pushboolean(state, true);
    lua_setfield(state, -2, "__LuaGlueNative");
    lua_pushvalue(state, typeInfo);
    lua_setfield(state, -2, "__type");
    lua_pushcfunction(state, nativeGc);
    lua_setfield(state, -2, "__gc");
    pushTypeFunction(state, record, protectedEntry<nativeIndex>);
    lua_setfield(state, -2, "__index");
    pushTypeFunction(state, record, protectedEntry<nativeNewIndex>);
    lua_setfield(state, -2, "__newindex");
    lua_pushlstring(state, record->luaName.data(), record->luaName.size());
    lua_setfield(state, -2, "__name");
    lua_pushvalue(state, typeTable);
    record->ready = true;
    return 1;
}

struct PushContext {
    lua_glue::detail::TypeRecord* type;
    void* pointer;
    std::shared_ptr<void>* owner;
    bool isConst;
};

int createPropertyScope(lua_State* state) {
    auto* context = static_cast<RegisterContext*>(lua_touserdata(state, 1));
    if (luaL_newmetatable(state, recordMetatable)) {
        lua_pushcfunction(state, typeGc);
        lua_setfield(state, -2, "__gc");
    }
    const int recordMeta = lua_absindex(state, -1);
    lua_newtable(state);
    const int metatable = lua_absindex(state, -1);
    if (lua_getmetatable(state, 2)) {
        const int old = lua_absindex(state, -1);
        lua_pushnil(state);
        while (lua_next(state, old)) {
            lua_pushvalue(state, -2);
            lua_pushvalue(state, -2);
            lua_rawset(state, metatable);
            lua_pop(state, 1);
        }
        lua_getfield(state, old, "__index");
        lua_setfield(state, metatable, "__LuaGlueFallbackIndex");
        lua_getfield(state, old, "__newindex");
        lua_setfield(state, metatable, "__LuaGlueFallbackNewIndex");
        lua_pop(state, 1);
    }
    void* memory =
        lua_newuserdatauv(state, sizeof(lua_glue::detail::TypeRecord), 0);
    lua_pushvalue(state, recordMeta);
    lua_setmetatable(state, -2);
    auto* record =
        std::construct_at(static_cast<lua_glue::detail::TypeRecord*>(memory),
                          std::move(*context->record));
    context->record = record;
    context->transferred = true;
    record->ready = true;
    const int recordIndex = lua_absindex(state, -1);
    lua_pushvalue(state, recordIndex);
    lua_setfield(state, metatable, "__LuaGlueScope");
    lua_pushlightuserdata(state, record);
    lua_setfield(state, metatable, "__LuaGlueType");
    lua_pushlightuserdata(state, record);
    lua_rawsetp(state, metatable, &nativeTypeKey);
    lua_pushvalue(state, recordIndex);
    lua_pushcclosure(state, protectedEntry<classIndex>, 1);
    lua_setfield(state, metatable, "__index");
    lua_pushvalue(state, recordIndex);
    lua_pushcclosure(state, protectedEntry<classNewIndex>, 1);
    lua_setfield(state, metatable, "__newindex");
    for (const char* name :
         {"__getters", "__setters", "__nativeProperties", "__nativeBases"}) {
        lua_pushstring(state, name);
        lua_rawget(state, 2);
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            lua_newtable(state);
            lua_pushstring(state, name);
            lua_pushvalue(state, -2);
            lua_rawset(state, 2);
        }
        lua_pop(state, 1);
    }
    lua_pushvalue(state, metatable);
    lua_setmetatable(state, 2);
    return 0;
}

int createNative(lua_State* state) {
    auto* context = static_cast<PushContext*>(lua_touserdata(state, 1));
    lua_getfield(state, LUA_REGISTRYINDEX,
                 context->type->metatableName.c_str());
    const int metatable = lua_absindex(state, -1);
    void* memory =
        lua_newuserdatauv(state, sizeof(lua_glue::detail::NativeStorage), 1);
    lua_pushvalue(state, metatable);
    lua_setmetatable(state, -2);
    auto* storage = std::construct_at(
        static_cast<lua_glue::detail::NativeStorage*>(memory));
    storage->type = context->type;
    storage->pointer = context->pointer;
    storage->isConst = context->isConst;
    storage->owner = *context->owner;
    return 1;
}

void operation(lua_State* state, lua_CFunction function, void* context,
               int results) {
    lua_pushcfunction(state, function);
    lua_pushlightuserdata(state, context);
    if (lua_pcall(state, 1, results, 0) != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        std::string error =
            message ? message : "LuaGlue native operation failed";
        lua_pop(state, 1);
        throw std::runtime_error(error);
    }
}

lua_glue::detail::ExternalResolvers* resolvers(lua_State* state) {
    lua_rawgetp(state, LUA_REGISTRYINDEX, &externalResolversKey);
    auto* result = static_cast<lua_glue::detail::ExternalResolvers*>(
        lua_touserdata(state, -1));
    lua_pop(state, 1);
    return result;
}

struct FindTypeContext {
    std::string_view key;
    lua_glue::detail::TypeRecord* record = nullptr;
};
int findType(lua_State* state) {
    auto* context = static_cast<FindTypeContext*>(lua_touserdata(state, 1));
    lua_getfield(state, LUA_REGISTRYINDEX, typesKey);
    if (lua_istable(state, -1)) {
        lua_pushlstring(state, context->key.data(), context->key.size());
        lua_rawget(state, -2);
        auto* record = static_cast<lua_glue::detail::TypeRecord*>(
            luaL_testudata(state, -1, recordMetatable));
        if (record && record->ready) {
            context->record = record;
        }
    }
    return 0;
}

int createResolver(lua_State* state) {
    auto* context = static_cast<lua_glue::detail::ExternalResolvers*>(
        lua_touserdata(state, 1));
    auto* memory =
        static_cast<lua_glue::detail::ExternalResolvers*>(lua_newuserdatauv(
            state, sizeof(lua_glue::detail::ExternalResolvers), 0));
    std::construct_at(memory, *context);
    lua_rawsetp(state, LUA_REGISTRYINDEX, &externalResolversKey);
    return 0;
}

int retainDependency(lua_State* state) {
    if (lua_getiuservalue(state, 1, 1) != LUA_TTABLE) {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushvalue(state, -1);
        lua_setiuservalue(state, 1, 1);
    }
    lua_getfield(state, -1, "__LuaGlueDependencies");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushvalue(state, -1);
        lua_setfield(state, -3, "__LuaGlueDependencies");
    }
    lua_pushvalue(state, 2);
    lua_rawseti(state, -2, static_cast<lua_Integer>(lua_rawlen(state, -2)) + 1);
    return 0;
}

}  // namespace

namespace lua_glue::detail {

TypeRecord* PreparePropertyScope(const Table& table) {
    lua_State* state = table.lua_state();
    StackGuard stack(state);
    table.push(state);
    if (auto* existing = GetTypeRecord(state, -1)) {
        return existing;
    }
    auto record = std::make_unique<TypeRecord>();
    record->luaName = "module";
    RegisterContext context{record.get()};
    if (!lua_checkstack(state, 3)) {
        throw std::runtime_error(
            "not enough stack to register module properties");
    }
    lua_pushcfunction(state, createPropertyScope);
    lua_pushlightuserdata(state, &context);
    table.push(state);
    if (lua_pcall(state, 2, 0, 0) != LUA_OK) {
        const char* text = lua_tostring(state, -1);
        throw std::runtime_error(text ? text
                                      : "module property registration failed");
    }
    return context.record;
}

TypeRecord* FindType(lua_State* state, std::string_view key) {
    FindTypeContext context{key};
    operation(state, findType, &context, 0);
    return context.record;
}

TypeRecord* GetTypeRecord(lua_State* state, int index) {
    if (!lua_checkstack(state, 2)) {
        throw std::runtime_error("not enough stack for native type lookup");
    }
    const int top = lua_gettop(state);
    TypeRecord* record = nullptr;
    if (lua_getmetatable(state, index)) {
        lua_rawgetp(state, -1, &nativeTypeKey);
        record = static_cast<TypeRecord*>(lua_touserdata(state, -1));
    }
    lua_settop(state, top);
    return record;
}

NativeStorage* GetNativeStorage(lua_State* state, int index) {
    if (lua_type(state, index) != LUA_TUSERDATA ||
        lua_rawlen(state, index) != sizeof(NativeStorage)) {
        return nullptr;
    }
    auto* record = GetTypeRecord(state, index);
    if (!record) {
        return nullptr;
    }
    auto* storage = static_cast<NativeStorage*>(lua_touserdata(state, index));
    if (storage->magic != NativeStorage::expectedMagic ||
        storage->type != record) {
        return nullptr;
    }
    return storage;
}

void* CastNative(TypeRecord* type, void* pointer, std::string_view key) {
    if (!pointer) {
        return nullptr;
    }
    if (key.empty() || type->cppName == key) {
        return pointer;
    }
    for (auto& base : type->bases) {
        void* basePointer = base.cast(pointer);
        if (key == base.cppName) {
            return basePointer;
        }
        if (void* result =
                base.type ? CastNative(base.type, basePointer, key) : nullptr) {
            return result;
        }
    }
    return nullptr;
}

Table RegisterClass(const Table& module, std::string_view name,
                    std::string_view cppName, bool valueType) {
    lua_State* state = module.lua_state();
    if (FindType(state, cppName)) {
        Table result = NativeTypeTable(state, cppName);
        module.raw_set(name, result);
        return result;
    }
    auto record = std::make_unique<TypeRecord>();
    record->cppName = cppName;
    record->luaName = name;
    record->metatableName = "LuaGlue.Native." + std::string(cppName);
    record->classRegistryName = "LuaGlue.Class." + std::string(cppName);
    record->valueType = valueType;
    RegisterContext context{record.get()};
    operation(state, createType, &context, 1);
    Table result(state, -1);
    lua_pop(state, 1);
    module.raw_set(name, result);
    return result;
}

void RegisterBase(const Table& table, std::string_view derivedName,
                  std::string_view baseName, NativeCast cast) {
    auto* derived = FindType(table.lua_state(), derivedName);
    auto* base = FindType(table.lua_state(), baseName);
    if (!derived || !base) {
        throw std::runtime_error(
            "LuaGlue: base type must be registered first: " +
            std::string(baseName));
    }
    for (auto& entry : derived->bases) {
        if (entry.cppName == baseName) {
            if (entry.visible) {
                return;
            }
            entry.type = base;
            entry.visible = true;
            table.raw_get<Table>("__nativeBases")
                .add(NativeTypeTable(table.lua_state(), baseName));
            return;
        }
    }
    derived->bases.push_back({base, cast, std::string(baseName), true});
    table.raw_get<Table>("__nativeBases")
        .add(NativeTypeTable(table.lua_state(), baseName));
}

void RegisterCast(const Table& table, std::string_view derivedName,
                  std::string_view baseName, NativeCast cast) {
    auto* derived = FindType(table.lua_state(), derivedName);
    if (!derived) {
        throw std::runtime_error(
            "LuaGlue: derived type must be registered first: " +
            std::string(derivedName));
    }
    for (const auto& entry : derived->bases) {
        if (entry.cppName == baseName) {
            return;
        }
    }
    derived->bases.push_back({FindType(table.lua_state(), baseName), cast,
                              std::string(baseName), false});
}

int PushNative(lua_State* state, std::string_view name, void* pointer,
               std::shared_ptr<void> owner, bool isConst) {
    if (!pointer) {
        lua_pushnil(state);
        return 1;
    }
    auto* type = FindType(state, name);
    if (!type) {
        throw std::runtime_error("LuaGlue: unregistered native type " +
                                 std::string(name));
    }
    PushContext context{type, pointer, &owner, isConst};
    operation(state, createNative, &context, 1);
    return 1;
}

}  // namespace lua_glue::detail

namespace lua_glue {

void* NativePointer(lua_State* state, int index, std::string_view name) {
    if (auto* storage = detail::GetNativeStorage(state, index)) {
        return detail::CastNative(storage->type, storage->pointer, name);
    }
    if (auto* external = resolvers(state); external && external->pointer) {
        return external->pointer(state, index, name);
    }
    return nullptr;
}

std::shared_ptr<void> NativeSharedOwner(lua_State* state, int index,
                                        std::string_view name) {
    if (auto* storage = detail::GetNativeStorage(state, index)) {
        void* pointer =
            detail::CastNative(storage->type, storage->pointer, name);
        if (pointer && storage->owner.use_count()) {
            return {storage->owner, pointer};
        }
        return {};
    }
    if (auto* external = resolvers(state); external && external->owner) {
        return external->owner(state, index, name);
    }
    return {};
}

bool NativeIsConst(lua_State* state, int index) {
    auto* storage = detail::GetNativeStorage(state, index);
    return storage && storage->isConst;
}

Table NativeTypeTable(lua_State* state, std::string_view key) {
    auto* record = detail::FindType(state, key);
    if (!record) {
        return {};
    }
    lua_getfield(state, LUA_REGISTRYINDEX, record->classRegistryName.c_str());
    Table result(state, -1);
    lua_pop(state, 1);
    return result;
}

Table NativeTypeTable(lua_State* state, int index) {
    auto* storage = detail::GetNativeStorage(state, index);
    return storage ? NativeTypeTable(state, storage->type->cppName) : Table{};
}

void RegisterExternalResolver(lua_State* state, NativeResolver pointer,
                              SharedOwnerResolver owner) {
    if (auto* external = resolvers(state)) {
        *external = {pointer, owner};
        return;
    }
    detail::ExternalResolvers context{pointer, owner};
    operation(state, createResolver, &context, 0);
}

void RetainDependency(lua_State* state, int dependent, int owner) {
    dependent = lua_absindex(state, dependent);
    owner = lua_absindex(state, owner);
    if (lua_type(state, dependent) != LUA_TUSERDATA ||
        lua_isnil(state, owner)) {
        return;
    }
    auto* child = detail::GetNativeStorage(state, dependent);
    auto* parent = detail::GetNativeStorage(state, owner);
    if (child && parent && !child->owner.use_count() &&
        parent->owner.use_count()) {
        child->owner = {parent->owner, child->pointer};
    }
    if (!lua_checkstack(state, 3)) {
        throw std::runtime_error("not enough Lua stack for object dependency");
    }
    lua_pushcfunction(state, retainDependency);
    lua_pushvalue(state, dependent);
    lua_pushvalue(state, owner);
    if (lua_pcall(state, 2, 0, 0) != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        std::string error =
            message ? message : "failed to retain object dependency";
        lua_pop(state, 1);
        throw std::runtime_error(error);
    }
}

void AttachSharedOwner(lua_State* state, int index,
                       const std::shared_ptr<void>& owner) {
    auto* storage = detail::GetNativeStorage(state, index);
    if (storage && owner.use_count()) {
        storage->owner = {owner, storage->pointer};
    }
}

}  // namespace lua_glue
