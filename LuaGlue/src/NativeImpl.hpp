#pragma once

#include <LuaGlue/Native.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lua_glue::detail {

struct FunctionRecord;

struct PropertyRecord {
    std::shared_ptr<FunctionRecord> getter;
    std::shared_ptr<FunctionRecord> setter;
};

struct TypeRecord {
    struct Base {
        TypeRecord* type;
        NativeCast cast;
        std::string cppName;
        bool visible;
    };
    std::string cppName;
    std::string luaName;
    std::string metatableName;
    std::string classRegistryName;
    bool valueType = false;
    bool ready = false;
    std::vector<Base> bases;
    std::unordered_map<std::string, PropertyRecord> properties;
    std::unordered_map<std::string, PropertyRecord> staticProperties;
};

struct NativeStorage {
    static constexpr std::uint64_t expectedMagic = 0x4c5541474c554531ULL;
    std::uint64_t magic = expectedMagic;
    TypeRecord* type = nullptr;
    void* pointer = nullptr;
    std::shared_ptr<void> owner;
    bool isConst = false;
};

struct ExternalResolvers {
    NativeResolver pointer = nullptr;
    SharedOwnerResolver owner = nullptr;
};

TypeRecord* FindType(lua_State*, std::string_view);
TypeRecord* PreparePropertyScope(const Table&);
NativeStorage* GetNativeStorage(lua_State*, int);
void* CastNative(TypeRecord*, void*, std::string_view);
int InvokeFunction(lua_State*, const std::shared_ptr<FunctionRecord>&);
void PushFunction(lua_State*, const std::shared_ptr<FunctionRecord>&);

}  // namespace lua_glue::detail
