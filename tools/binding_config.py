"""
Binding configuration data for LuaSF code generation.

Pure data — no query functions or code generation logic.
Imported by ``replace_model.py`` which provides the query/render API.

Sections
--------
0. Shared utilities    — TypeRef, type-checking helpers
1. TypeLifecycle       — which types need long-lived memory/stream tracking
2. TypeConversion      — C++ ↔ Lua type conversion rules
3. BindingOwnership    — which header/binding unit owns which type
4. OperatorMapping     — C++ operator → sol meta_function
5. ShaderUniformArray  — sf::Shader uniform-array dispatch table
6. Binding Templates   — C++ code templates with placeholder substitution
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

SCHEMA_VERSION = 1


# ===========================================================================
# 0. Shared utilities
# ===========================================================================


def clean_cpp_type(value: str) -> str:
    value = (value or "").strip()
    value = value.replace("std::basic_string<char>", "std::string")
    value = value.replace("std::basic_string<wchar_t>", "std::wstring")
    value = value.replace("std::basic_string_view<char>", "std::string_view")
    value = value.replace("std::basic_string_view<wchar_t>", "std::wstring_view")
    value = value.replace("std::__cxx11::basic_string<char>", "std::string")
    value = value.replace("std::__cxx11::basic_string<wchar_t>", "std::wstring")
    value = value.replace("std::__cxx11::basic_string_view<char>", "std::string_view")
    value = value.replace("std::__cxx11::basic_string_view<wchar_t>", "std::wstring_view")
    value = re.sub(r"\bstd::filesystem::path\b", "std::filesystem::path", value)
    value = re.sub(r"\s+", " ", value)
    value = value.replace(" &", "&").replace("& ", "&")
    value = value.replace(" *", "*").replace("* ", "*")
    value = value.replace("< ", "<").replace(" >", ">")
    value = value.replace(" ,", ",")
    return value


def sanitize_identifier(value: str) -> str:
    value = re.sub(r"[^0-9A-Za-z_]", "_", value)
    if value and value[0].isdigit():
        value = "_" + value
    return value or "unnamed"


def remove_cvref(value: str) -> str:
    value = value.strip()
    while value.endswith("&") or value.endswith("*"):
        value = value.rstrip("&*").strip()
    for prefix in ("const ", "volatile "):
        if value.startswith(prefix):
            value = value[len(prefix):].strip()
    return clean_cpp_type(value)


def remove_pointer(value: str) -> str:
    value = clean_cpp_type(value)
    while value.endswith("*"):
        value = value[:-1].strip()
    if value.startswith("const "):
        value = value[len("const "):].strip()
    return value


def is_const_type(value: str) -> bool:
    return "const" in clean_cpp_type(value)


def is_pointer(value: str) -> bool:
    return "*" in clean_cpp_type(value)


def is_reference(value: str) -> bool:
    return "&" in clean_cpp_type(value) and "&&" not in clean_cpp_type(value)


def is_output_reference(value: str) -> bool:
    return is_reference(value) and not is_const_type(value)


def normalize_array_element(value: str) -> str:
    value = clean_cpp_type(value)
    while value.endswith("*"):
        value = value[:-1].strip()
    if value.startswith("const "):
        value = value[len("const "):].strip()
    return value


SIZE_TYPE_NAMES: frozenset[str] = frozenset({
    "std::size_t", "size_t", "std::uint64_t", "uint64_t",
    "unsigned long long", "unsigned long", "unsigned int",
    "std::uint32_t", "uint32_t", "int",
})

BYTE_TYPES: frozenset[str] = frozenset({
    "void", "std::byte", "std::uint8_t", "uint8_t", "unsigned char", "char",
})


def is_size_type(type_ref: "TypeRef | None" = None, *, cpp: str = "") -> bool:
    if type_ref is not None:
        cpp = clean_cpp_type(type_ref.canonical or type_ref.spelling)
    return cpp in SIZE_TYPE_NAMES


# --- TypeRef dataclass ---


@dataclass(frozen=True)
class TypeRef:
    spelling: str = ""
    canonical: str = ""
    kind: str = ""

    @classmethod
    def from_json(cls, value: dict[str, Any] | None) -> "TypeRef":
        if not value:
            return cls()
        return cls(
            spelling=value.get("spelling", "") or "",
            canonical=value.get("canonical", "") or value.get("spelling", "") or "",
            kind=value.get("kind", "") or "",
        )

    @property
    def cpp(self) -> str:
        return semantic_cpp_type(self.spelling, self.canonical)

    @property
    def canonical_cpp(self) -> str:
        return clean_cpp_type(self.canonical or self.spelling)

    @property
    def source(self) -> str:
        return clean_cpp_type(self.spelling or self.canonical)


# --- Lua name helpers ---


CPP_BUILTIN_TYPES = {
    "void", "bool", "char", "signed char", "unsigned char",
    "short", "unsigned short", "int", "unsigned", "unsigned int",
    "long", "unsigned long", "long long", "unsigned long long",
    "float", "double", "long double", "wchar_t", "char8_t",
    "char16_t", "char32_t",
}

PUBLIC_TYPE_ALIASES: dict[str, str] = {}


def core_cpp_type(value: str) -> str:
    value = clean_cpp_type(value)
    while value.endswith("&") or value.endswith("*"):
        value = value[:-1].strip()
    for prefix in ("const ", "volatile "):
        if value.startswith(prefix):
            value = value[len(prefix):].strip()
    return clean_cpp_type(value)


def is_anonymous_cpp_name(value: str) -> bool:
    return "(unnamed" in value or "(anonymous" in value


def set_public_type_aliases(api: dict[str, Any]) -> None:
    aliases: dict[str, set[str]] = {}
    for file_item in api.get("files", []):
        for item in walk_declarations(file_item.get("declarations", [])):
            if item.get("kind") not in TYPE_DECL_KINDS:
                continue
            qualified_name = clean_cpp_type(item.get("qualified_name") or item.get("name") or "")
            if not qualified_name or is_anonymous_cpp_name(qualified_name):
                continue
            aliases.setdefault(qualified_name.rsplit("::", 1)[-1], set()).add(qualified_name)

    PUBLIC_TYPE_ALIASES.clear()
    PUBLIC_TYPE_ALIASES.update({
        name: next(iter(values))
        for name, values in aliases.items()
        if len(values) == 1
    })


def walk_declarations(items: list[dict[str, Any]]):
    for item in items:
        yield item
        yield from walk_declarations(item.get("children", []))


def qualified_name_for_token_from_canonical(token: str, canonical: str) -> str | None:
    match = re.search(
        rf"(?<![A-Za-z0-9_:])sf(?:::[A-Za-z_][A-Za-z0-9_]*)*::{re.escape(token)}(?![A-Za-z0-9_:])",
        canonical,
    )
    if match:
        return match.group(0)
    return None


def qualify_known_public_type_tokens(value: str, canonical: str) -> str:
    def replace(match: re.Match[str]) -> str:
        token = match.group(0)
        if token in CPP_BUILTIN_TYPES or token.startswith(("sf::", "std::", "sol::")):
            return token
        alias = PUBLIC_TYPE_ALIASES.get(token)
        if alias:
            return alias
        return qualified_name_for_token_from_canonical(token, canonical) or token

    return re.sub(r"(?<![A-Za-z0-9_:])(?:[A-Z][A-Za-z0-9_]*)(?:::[A-Z][A-Za-z0-9_]*)*(?![A-Za-z0-9_:])", replace, value)


def qualify_public_spelling(source: str, canonical: str) -> str:
    source = qualify_known_public_type_tokens(source, canonical)
    if "sf::" in canonical and "<" in source:
        source = qualify_sfml_template_aliases(source, canonical)
    source_core = core_cpp_type(source)
    canonical_core = core_cpp_type(canonical)
    if not source_core or source_core in CPP_BUILTIN_TYPES:
        return source
    if source_core.startswith(("std::", "sol::", "lua_")):
        return source
    if canonical_core.startswith("sf::") and not source_core.startswith("sf::"):
        replacement = f"sf::{source_core}"
        if source_core.count("::") == 0 and "::" in canonical_core and "<" not in canonical_core:
            replacement = canonical_core.rsplit("::", 1)[0] + f"::{source_core}"
        return re.sub(rf"(?<![A-Za-z0-9_:]){re.escape(source_core)}(?![A-Za-z0-9_:])", replacement, source, count=1)
    return source


def qualify_sfml_template_aliases(value: str, canonical: str) -> str:
    def replace(match: re.Match[str]) -> str:
        token = match.group(0)
        if token in CPP_BUILTIN_TYPES or token.startswith(("sf::", "std::", "sol::")):
            return token
        alias = PUBLIC_TYPE_ALIASES.get(token)
        if alias:
            return alias
        qualified_name = qualified_name_for_token_from_canonical(token, canonical)
        if qualified_name:
            return qualified_name
        return f"sf::{token}"

    return re.sub(r"(?<![A-Za-z0-9_:])(?:[A-Z][A-Za-z0-9_]*)(?:::[A-Z][A-Za-z0-9_]*)*(?![A-Za-z0-9_:])", replace, value)


def semantic_cpp_type(spelling: str, canonical: str) -> str:
    source = clean_cpp_type(spelling or canonical)
    canonical = clean_cpp_type(canonical or spelling)
    if source:
        return qualify_public_spelling(source, canonical)
    return canonical


def lua_name_for_type(qualified_name: str) -> str:
    parts = (qualified_name or "").split("::")
    return ".".join(p for p in parts if p != "sf")


def lua_leaf_for_type(qualified_name: str) -> str:
    return (qualified_name or "").rsplit("::", 1)[-1]


def lua_path_for_type(qualified_name: str) -> str:
    return (qualified_name or "").replace("::", ".")


# ===========================================================================
# 1. Type Lifecycle
# ===========================================================================

class LifecycleCategory:
    NONE = "none"
    MEMORY = "memory"
    STREAM = "stream"
    BOTH = "both"


@dataclass(frozen=True)
class TypeLifecycle:
    qualified_name: str
    category: str
    constructor_patterns: tuple[str, ...] = ()
    reset_methods: tuple[str, ...] = ()
    memory_constructor_via_openfrommemory: bool = False


LIFECYCLE_REGISTRY: dict[str, TypeLifecycle] = {}


def _register(lc: TypeLifecycle) -> TypeLifecycle:
    LIFECYCLE_REGISTRY[lc.qualified_name] = lc
    return lc


_register(TypeLifecycle(
    "sf::Font",
    LifecycleCategory.BOTH,
    constructor_patterns=("default", "from_file", "from_memory", "from_stream"),
    reset_methods=("close", "openFromFile", "openFromStream"),
    memory_constructor_via_openfrommemory=True,
))
_register(TypeLifecycle(
    "sf::InputSoundFile",
    LifecycleCategory.BOTH,
    constructor_patterns=("from_file", "from_memory", "from_stream"),
    reset_methods=("close", "openFromFile", "openFromStream"),
    memory_constructor_via_openfrommemory=True,
))
_register(TypeLifecycle(
    "sf::MemoryInputStream",
    LifecycleCategory.MEMORY,
    constructor_patterns=("from_memory",),
    memory_constructor_via_openfrommemory=False,
))
_register(TypeLifecycle(
    "sf::Music",
    LifecycleCategory.BOTH,
    constructor_patterns=("from_file", "from_memory", "from_stream"),
    reset_methods=("close", "openFromFile", "openFromStream"),
    memory_constructor_via_openfrommemory=True,
))


LONG_LIVED_RESOURCE_RESET_METHODS: frozenset[str] = frozenset({
    "close",
    "openFromFile",
    "openFromStream",
})


# ===========================================================================
# 2. Type Conversion (C++ ↔ Lua)
# ===========================================================================


@dataclass(frozen=True)
class TypeConversion:
    cpp_types: tuple[str, ...] = ()
    lua_type: str = ""
    from_lua_template: str | None = None
    to_lua_template: str | None = None
    is_bypass: bool = False
    is_return_wrapper: bool = False
    wrapper_uses_lua: bool = False


@dataclass(frozen=True)
class PacketIoType:
    cpp_types: tuple[str, ...] = ()
    suffix: str = ""
    packet_aliases: tuple[str, ...] = ()
    packet_lua_type: str = ""
    packet_result_expr: str = "value"


CONVERSION_REGISTRY: dict[str, TypeConversion] = {}
PACKET_IO_REGISTRY: dict[str, PacketIoType] = {}


def _register_conv(tc: TypeConversion) -> TypeConversion:
    for cpp in tc.cpp_types:
        CONVERSION_REGISTRY[clean_cpp_type(cpp)] = tc
    return tc


def _register_packet_io(packet_type: PacketIoType) -> PacketIoType:
    for cpp in packet_type.cpp_types:
        PACKET_IO_REGISTRY[clean_cpp_type(cpp)] = packet_type
    return packet_type


# --- "special" types that need manual conversion ---

_register_conv(TypeConversion(
    cpp_types=("std::filesystem::path",),
    lua_type="string",
    from_lua_template="std::filesystem::path({name})",
    to_lua_template="{expr}.string()",
))

_register_conv(TypeConversion(
    cpp_types=("sf::WindowHandle",),
    lua_type="integer",
    from_lua_template="lua_sf::window_handle_from_integer({name})",
    to_lua_template="lua_sf::window_handle_to_integer({expr})",
    is_return_wrapper=True,
))

_register_conv(TypeConversion(
    cpp_types=("char*", "const char*", "const char *", "char *"),
    lua_type="string",
    from_lua_template="{name}.c_str()",
))

_register_conv(TypeConversion(
    cpp_types=("wchar_t*", "const wchar_t*", "const wchar_t *", "wchar_t *"),
    lua_type="string",
    from_lua_template=None,
))

_register_conv(TypeConversion(
    cpp_types=("std::function",),
    lua_type="function",
    is_return_wrapper=True,
    wrapper_uses_lua=True,
))

_register_conv(TypeConversion(
    cpp_types=(
        "VkInstance_T*",
        "VkAllocationCallbacks*",
        "std::locale",
        "char32_t*",
    ),
    is_bypass=True,
))

_register_conv(TypeConversion(
    cpp_types=("GlFunctionPointer",),
    is_bypass=True,
))


# --- Packet I/O table ---

_register_packet_io(PacketIoType(
    cpp_types=("char*", "const char*", "const char *", "char *"),
    suffix="String",
    packet_aliases=("string", "std::string"),
    packet_lua_type="string",
    packet_result_expr="value",
))

_register_packet_io(PacketIoType(
    cpp_types=("wchar_t*", "const wchar_t*", "const wchar_t *", "wchar_t *"),
    suffix="WideString",
    packet_aliases=("wstring", "wideString"),
    packet_lua_type="string",
    packet_result_expr="lua_sf::to_utf8_string(sf::String(value))",
))

_register_packet_io(PacketIoType(
    cpp_types=("bool",),
    suffix="Bool",
    packet_aliases=("bool", "boolean"),
    packet_lua_type="boolean",
    packet_result_expr="value",
))

_register_packet_io(PacketIoType(
    cpp_types=("signed char", "std::int8_t"),
    suffix="Int8",
    packet_aliases=("int8",),
    packet_lua_type="integer",
    packet_result_expr="static_cast<int>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("unsigned char", "std::uint8_t"),
    suffix="UInt8",
    packet_aliases=("uint8",),
    packet_lua_type="integer",
    packet_result_expr="static_cast<unsigned int>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("short", "std::int16_t"),
    suffix="Int16",
    packet_aliases=("int16",),
    packet_lua_type="integer",
    packet_result_expr="static_cast<int>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("unsigned short", "std::uint16_t"),
    suffix="UInt16",
    packet_aliases=("uint16",),
    packet_lua_type="integer",
    packet_result_expr="static_cast<unsigned int>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("int", "std::int32_t"),
    suffix="Int32",
    packet_aliases=("int32", "int", "integer"),
    packet_lua_type="integer",
    packet_result_expr="static_cast<int>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("unsigned int", "std::uint32_t"),
    suffix="UInt32",
    packet_aliases=("uint32", "uint"),
    packet_lua_type="integer",
    packet_result_expr="static_cast<unsigned int>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("long long", "std::int64_t"),
    suffix="Int64",
    packet_aliases=("int64",),
    packet_lua_type="integer",
    packet_result_expr="static_cast<std::int64_t>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("unsigned long long", "std::uint64_t"),
    suffix="UInt64",
    packet_aliases=("uint64",),
    packet_lua_type="integer",
    packet_result_expr="static_cast<std::uint64_t>(value)",
))

_register_packet_io(PacketIoType(
    cpp_types=("float",),
    suffix="Float",
    packet_aliases=("float",),
    packet_lua_type="number",
    packet_result_expr="value",
))

_register_packet_io(PacketIoType(
    cpp_types=("double",),
    suffix="Double",
    packet_aliases=("double", "number"),
    packet_lua_type="number",
    packet_result_expr="value",
))

_register_packet_io(PacketIoType(
    cpp_types=("std::string", "std::basic_string<char>"),
    suffix="String",
    packet_aliases=("string", "std::string"),
    packet_lua_type="string",
    packet_result_expr="value",
))

_register_packet_io(PacketIoType(
    cpp_types=("std::wstring", "std::basic_string<wchar_t>"),
    suffix="WideString",
    packet_aliases=("wstring", "wideString"),
    packet_lua_type="string",
    packet_result_expr="lua_sf::to_utf8_string(sf::String(value))",
))

_register_packet_io(PacketIoType(
    cpp_types=("sf::String",),
    suffix="SfString",
    packet_aliases=("sfString", "sf::String"),
    packet_lua_type="string",
    packet_result_expr="lua_sf::to_utf8_string(value)",
))


# ===========================================================================
# 3. Binding Ownership
# ===========================================================================

MANUAL_DEPENDENCIES: dict[str, set[str]] = {
    "bind_Vector": {"bind_Angle", "bind_Color"},
    "bind_Rect": {"bind_Vector"},
    "bind_Matrix": {"bind_Transform"},
    "bind_Event": {"bind_Vector", "bind_Joystick", "bind_Keyboard", "bind_Mouse", "bind_Sensor"},
    "bind_Handle": set(),
    "bind_Drawable": set(),
}

MANUAL_HEADER_OWNERS: dict[str, str] = {
    "Drawable": "bind_Drawable",
    "Event": "bind_Event",
    "Rect": "bind_Rect",
    "Vector2": "bind_Vector",
    "Vector3": "bind_Vector",
    "WindowHandle": "bind_Handle",
}

MANUAL_HEADER_DECLARATION_PREFIX_OWNERS: dict[str, dict[str, str]] = {
    "Glsl": {
        "Vector": "bind_Vector",
        "Matrix": "bind_Matrix",
    },
}


# ===========================================================================
# 4. Operator → sol meta_function Mapping
# ===========================================================================

OPERATOR_META_FUNCTIONS: dict[str, str] = {
    "operator+": "addition",
    "operator-": "subtraction",
    "operator*": "multiplication",
    "operator/": "division",
    "operator%": "modulus",
    "operator==": "equal_to",
    "operator<": "less_than",
    "operator<=": "less_than_or_equal_to",
    "operator<<": "bitwise_left_shift",
    "operator>>": "bitwise_right_shift",
    "operator&": "bitwise_and",
    "operator|": "bitwise_or",
    "operator^": "bitwise_xor",
    "operator~": "bitwise_not",
    "operator()": "call",
}


# ===========================================================================
# 5. Shader Uniform Array Dispatch
# ===========================================================================

SHADER_UNIFORM_ARRAY_BINDINGS: tuple[dict[str, str], ...] = (
    {
        "method": "setUniformFloatArray",
        "local": "setUniformFloatArray",
        "cpp": "float",
        "lua": "number[]",
        "param": "scalarArray",
        "check": "first.get_type() == sol::type::number",
    },
    {
        "method": "setUniformVec2Array",
        "local": "setUniformVec2Array",
        "cpp": "sf::Vector2f",
        "lua": "sf.Vector2f[]",
        "param": "vectorArray",
        "check": "first.is<sf::Vector2f>()",
    },
    {
        "method": "setUniformVec3Array",
        "local": "setUniformVec3Array",
        "cpp": "sf::Vector3f",
        "lua": "sf.Vector3f[]",
        "param": "vectorArray",
        "check": "first.is<sf::Vector3f>()",
    },
    {
        "method": "setUniformVec4Array",
        "local": "setUniformVec4Array",
        "cpp": "sf::priv::Vector4<float>",
        "lua": "sf.Vector4f[]",
        "param": "vectorArray",
        "check": "first.is<sf::priv::Vector4<float>>()",
    },
    {
        "method": "setUniformMat3Array",
        "local": "setUniformMat3Array",
        "cpp": "sf::priv::Matrix<3, 3>",
        "lua": "sf.Mat3[]",
        "param": "matrixArray",
        "check": "first.is<sf::priv::Matrix<3, 3>>()",
    },
    {
        "method": "setUniformMat4Array",
        "local": "setUniformMat4Array",
        "cpp": "sf::priv::Matrix<4, 4>",
        "lua": "sf.Mat4[]",
        "param": "matrixArray",
        "check": "first.is<sf::priv::Matrix<4, 4>>()",
    },
)


# ===========================================================================
# 6. Binding Code Templates
# ===========================================================================

BINDING_TEMPLATES: dict[str, list[str]] = {}


def _t(name: str, *lines: str) -> None:
    """Register a named template."""
    BINDING_TEMPLATES[name] = list(lines)


_t("ll_memory_ctor_direct",
    "auto {data_name}_buffer = lua_sf::makeLongLivedMemoryBuffer({data_name});",
    "auto object = lua_sf::makeLongLivedMemoryObject<{owner_type}>(",
    "    {data_name}_buffer->data(),",
    "    static_cast<{size_type}>({data_name}_buffer->size()));",
    "lua_sf::rememberLongLivedMemory(*object, std::move({data_name}_buffer));",
    "return object;",
)

_t("ll_memory_ctor_via_open",
    "auto {data_name}_buffer = lua_sf::makeLongLivedMemoryBuffer({data_name});",
    "auto object = lua_sf::makeLongLivedMemoryObject<{owner_type}>();",
    "if (!object->openFromMemory({data_name}_buffer->data(), static_cast<{size_type}>({data_name}_buffer->size())))",
    '    throw std::runtime_error("Failed to open {lua_path} from memory");',
    "lua_sf::rememberLongLivedMemory(*object, std::move({data_name}_buffer));",
    "return object;",
)

_t("ll_memory_open",
    "auto {data_name}_buffer = lua_sf::makeLongLivedMemoryBuffer({data_name});",
    "const bool result = {call_target}({data_name}_buffer->data(), static_cast<{size_type}>({data_name}_buffer->size()));",
    "if (result)",
    "{{",
    "    lua_sf::releaseLongLivedStream(self);",
    "    lua_sf::rememberLongLivedMemory(self, std::move({data_name}_buffer));",
    "}}",
    "return result;",
)

_t("ll_stream_ctor",
    "auto& {stream_name}_ref = {stream_name}.as<sf::InputStream&>();",
    "auto object = lua_sf::makeLongLivedMemoryObject<{owner_type}>({stream_name}_ref);",
    "lua_sf::rememberLongLivedStream(*object, std::move({stream_name}));",
    "return object;",
)

_t("ll_stream_open",
    "auto& {stream_name}_ref = {stream_name}.as<sf::InputStream&>();",
    "const bool result = {call_target}({stream_name}_ref);",
    "if (result)",
    "{{",
    "    lua_sf::releaseLongLivedMemory(self);",
    "    lua_sf::rememberLongLivedStream(self, std::move({stream_name}));",
    "}}",
    "return result;",
)

_t("ll_reset_void",
    "{call_expr};",
    "lua_sf::releaseLongLivedResources(self);",
)

_t("ll_reset_nonvoid",
    "auto result = {call_expr};",
    "if (result)",
    "    lua_sf::releaseLongLivedResources(self);",
    "return result;",
)

_t("shader_uniform_array",
    "auto {param}_buffer = lua_sf::array_from_object<{cpp_type}>({param});",
    "self.setUniformArray(name, {param}_buffer.data(), static_cast<std::size_t>({param}_buffer.size()));",
)


# ===========================================================================
# 7. Generator Configuration
#
# Constants used by ``generate_sol2_bindings.py`` and
# ``generate_build_files.py`` for filtering, type classification, and
# special-case handling.
# ===========================================================================

IGNORE_NAMES: frozenset[str] = frozenset({
    "operator=",
    "operator++",
    "operator--",
    "operator&&",
    "operator||",
    'operator""_deg',
    'operator""_rad',
})

IGNORE_PARAM_TYPES: frozenset[str] = frozenset({
    "VkInstance",
    "VkSurfaceKHR",
    "VkInstance_T*",
    "VkAllocationCallbacks*",
    "std::locale",
    "char32_t*",
})

IGNORE_RETURN_TYPES: frozenset[str] = frozenset({
    "GlFunctionPointer",
})

IGNORED_NAMESPACES: frozenset[str] = frozenset({"priv"})

NUMERIC_ARRAY_TYPES: frozenset[str] = frozenset({
    "short",
    "std::int16_t",
    "int",
    "float",
    "double",
    "std::uint8_t",
    "unsigned char",
    "std::byte",
    "sf::Vertex",
    "sf::Vector2<float>",
    "sf::Vector3<float>",
    "sf::priv::Vector4<float>",
    "sf::priv::Matrix<3, 3>",
    "sf::priv::Matrix<4, 4>",
})

OUTPUT_REF_NAMES: frozenset[str] = frozenset({
    "received",
    "sent",
    "remoteAddress",
    "remotePort",
    "socket",
    "packet",
})

OUTPUT_REF_FUNCTIONS: frozenset[str] = frozenset({
    "accept",
    "receive",
})

OUTPUT_ARRAY_COUNT_REF_NAMES: frozenset[str] = frozenset({
    "received",
})

SPECIAL_POINTER_RETURNS: dict[str, tuple[str, str]] = {
    "sf::Transform::getMatrix": ("float", "16"),
    "sf::Image::getPixelsPtr": ("std::uint8_t", "self.getSize().x * self.getSize().y * 4"),
    "sf::SoundBuffer::getSamples": ("std::int16_t", "self.getSampleCount()"),
}

AUDIO_EFFECT_PROCESSOR_SIGNATURE: str = (
    "void(const float*, unsigned int&, float*, unsigned int&, unsigned int)"
)

AUDIO_EFFECT_PROCESSOR_LUA_TYPE: str = (
    "fun(inputFrames: number[][], inputFrameCount: integer, "
    "outputFrames: number[][], outputFrameCount: integer, frameChannelCount: integer): integer|table|nil"
)

SPECIAL_CALLBACK_LUA_TYPES: dict[str, str] = {
    AUDIO_EFFECT_PROCESSOR_SIGNATURE: AUDIO_EFFECT_PROCESSOR_LUA_TYPE,
    "void(const sf::Text::ShapedGlyph&, std::uint32_t&, sf::Color&, sf::Color&, float&)": (
        "fun(shapedGlyph: sf.Text.ShapedGlyph, style: integer, fillColor: sf.Color, "
        "outlineColor: sf.Color, outlineThickness: number): "
        "{style: integer?, fillColor: sf.Color?, outlineColor: sf.Color?, outlineThickness: number?}|nil"
    ),
    "bool(const void*, std::size_t)": "fun(data: string, size: integer): boolean",
    "bool(void*, std::size_t&)": (
        "fun(capacity: integer): string|integer[]|"
        "{keepGoing: boolean?, data: string|integer[]?}|boolean|nil"
    ),
}

# TYPE_DECL_KINDS (used by both generate_sol2_bindings and generate_build_files;
# includes CLASS_TEMPLATE for the build-file scanner)
TYPE_DECL_KINDS: frozenset[str] = frozenset({
    "CLASS_DECL", "STRUCT_DECL", "CLASS_TEMPLATE",
    "ENUM_DECL", "TYPEDEF_DECL", "TYPE_ALIAS_DECL",
})

LUA_KEYWORDS: frozenset[str] = frozenset({
    "and", "break", "do", "else", "elseif", "end", "false",
    "for", "function", "goto", "if", "in", "local", "nil",
    "not", "or", "repeat", "return", "then", "true", "until", "while",
})

INTEGER_TYPES: frozenset[str] = frozenset({
    "int", "short", "long", "long long",
    "unsigned", "unsigned int", "unsigned short", "unsigned char", "char",
    "unsigned long", "unsigned long long",
    "std::int8_t", "std::int16_t", "std::int32_t", "std::int64_t",
    "std::uint8_t", "std::uint16_t", "std::uint32_t", "std::uint64_t",
    "std::size_t", "size_t", "std::uintptr_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
})

NUMBER_TYPES: frozenset[str] = frozenset({"float", "double", "long double"})

STRING_TYPES: frozenset[str] = frozenset({
    "std::string", "std::string_view", "string", "string_view", "std::filesystem::path",
})

MODULE_ORDER: dict[str, int] = {
    "System": 0, "Window": 1, "Graphics": 2, "Audio": 3, "Network": 4,
}
