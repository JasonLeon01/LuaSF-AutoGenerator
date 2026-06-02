#pragma once

#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace lua_sf::stub {

inline std::ofstream& output()
{
    static std::ofstream stream;
    return stream;
}

inline std::string& pending_class()
{
    static std::string name;
    return name;
}

inline std::set<std::string>& declared_tables()
{
    static std::set<std::string> tables;
    return tables;
}

struct PendingFunction
{
    std::string owner;
    std::string name;
    std::string type;
    std::vector<std::string> overloads;
};

inline PendingFunction& pending_function()
{
    static PendingFunction pf;
    return pf;
}

inline bool enabled()
{
    return output().is_open();
}

inline void declare_table(const std::string& name)
{
    if (!enabled() || declared_tables().contains(name))
        return;

    const auto dot = name.rfind('.');
    if (dot != std::string::npos)
        declare_table(name.substr(0, dot));

    output() << name << " = " << name << " or {}\n";
    declared_tables().insert(name);
}

inline void close_pending_class();

inline void flush_pending_function()
{
    auto& pf = pending_function();
    if (!enabled() || pf.name.empty())
        return;

    // Capture and clear before any call that may re-enter flush_pending_function.
    const PendingFunction current = std::move(pf);
    pf = PendingFunction{};

    close_pending_class();
    declare_table(current.owner);
    output() << "---@type " << current.type << "\n";
    for (const auto& ol : current.overloads)
        output() << "---@overload " << ol << "\n";
    output() << current.owner << "." << current.name << " = function() end\n";
}

inline void close_pending_class()
{
    flush_pending_function();
    if (!enabled() || pending_class().empty())
        return;

    declare_table(pending_class());
    pending_class().clear();
}

inline bool begin(const char* path)
{
    output().open(path, std::ios::binary);
    if (!enabled())
        return false;

    declared_tables().clear();
    output() << "---@meta\n\n";
    output() << "sf = sf or {}\n";
    declared_tables().insert("sf");
    return true;
}

inline void end()
{
    flush_pending_function();
    close_pending_class();
    declared_tables().clear();
    if (enabled())
        output().close();
}

inline void class_(const char* name)
{
    if (!enabled())
        return;

    flush_pending_function();
    close_pending_class();
    pending_class() = name;
    output() << "\n---@class " << name << "\n";
}

inline void field(const char* name, const char* type)
{
    if (!enabled())
        return;

    if (!pending_class().empty())
    {
        output() << "---@field " << name << " " << type << "\n";
        return;
    }

    flush_pending_function();
    output() << "---@type " << type << "\n";
    output() << name << " = nil\n";
}

inline void value(const char* owner, const char* name, const char* type)
{
    if (!enabled())
        return;

    flush_pending_function();
    close_pending_class();
    declare_table(owner);
    output() << "---@type " << type << "\n";
    output() << owner << "." << name << " = nil\n";
}

inline void function(const char* owner, const char* name, const char* type)
{
    if (!enabled())
        return;

    flush_pending_function();
    pending_function() = {owner, name, type, {}};
}

inline void overload(const char* owner, const char* name, const char* type)
{
    if (!enabled())
        return;

    auto& pf = pending_function();
    if (pf.owner == owner && pf.name == name)
        pf.overloads.push_back(type);
}

inline void operator_(const char* owner, const char* annotation)
{
    if (!enabled())
        return;

    flush_pending_function();
    close_pending_class();
    output() << "\n---@class " << owner << "\n";
    output() << "---@operator " << annotation << "\n";
    declare_table(owner);
}

inline void indexed_field(const char* owner, const char* key_type, const char* value_type)
{
    if (!enabled())
        return;

    flush_pending_function();
    close_pending_class();
    output() << "\n---@class " << owner << "\n";
    output() << "---@field [" << key_type << "] " << value_type << "\n";
    declare_table(owner);
}

} // namespace lua_sf::stub

#define LUASF_STUB_CLASS(name) ::lua_sf::stub::class_(name)
#define LUASF_STUB_FIELD(name, type_str) ::lua_sf::stub::field(name, type_str)
#define LUASF_STUB_VALUE(owner, name, type_str) ::lua_sf::stub::value(owner, name, type_str)
#define LUASF_STUB_FUNCTION(owner, name, type_str) ::lua_sf::stub::function(owner, name, type_str)
#define LUASF_STUB_OVERLOAD(owner, name, type_str) ::lua_sf::stub::overload(owner, name, type_str)
#define LUASF_STUB_OPERATOR(owner, annotation) ::lua_sf::stub::operator_(owner, annotation)
#define LUASF_STUB_INDEX_FIELD(owner, key_type, value_type) ::lua_sf::stub::indexed_field(owner, key_type, value_type)
