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
5. MethodOverrides     — exact native method helper descriptors
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


@dataclass(frozen=True)
class ConfiguredBinding:
    """One declarative extension rendered for a template specialization."""

    kind: str
    template: str
    name: str = ""
    stub_signature: str = ""
    values: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True)
class ConfiguredMethodVariant:
    """One typed Lua entry exposed by a configured native method override."""

    lua_name: str
    cpp_type: str
    lua_type: str


@dataclass(frozen=True)
class ConfiguredMethodOverride:
    """Exact native method replacement rendered as one C++ helper call."""

    qualified_function: str
    lua_name: str
    helper: str
    variant_factory: str
    native_parameter_types: tuple[str, ...]
    native_return_type: str
    variants: tuple[ConfiguredMethodVariant, ...]


@dataclass(frozen=True)
class OutputReferenceParameter:
    """One native out parameter selected by its index in an exact overload."""

    index: int
    expected_name: str
    count_for_array_parameter: int | None = None


@dataclass(frozen=True)
class OutputReferencePolicy:
    """Output semantics for one exact qualified native overload."""

    qualified_function: str
    parameter_types: tuple[str, ...]
    outputs: tuple[OutputReferenceParameter, ...]


@dataclass(frozen=True)
class TemplateProfile:
    """Binding policy shared by every specialization of a C++ template."""

    cpp_template: str
    value_type: bool = True
    allowed_operators: tuple[str, ...] = ()
    configured_bindings: tuple[ConfiguredBinding, ...] = ()
    replaced_fields: tuple[str, ...] = ()
    field_lua_types: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True)
class TemplateSpecializationOverride:
    """Exceptions that cannot be inferred from a public C++ alias alone."""

    cpp_type: str
    lua_path: str = ""
    disabled_members: tuple[str, ...] = ()
    disabled_constructors: tuple[str, ...] = ()
    allowed_operators: tuple[str, ...] | None = None
    configured_bindings: tuple[ConfiguredBinding, ...] = ()
    aliases: tuple[str, ...] = ()
    dependencies: tuple[str, ...] = ()


@dataclass(frozen=True)
class CallbackSelector:
    """Semantic selector for one callback conversion policy."""

    semantic_alias: str = ""
    qualified_function: str = ""
    parameter_name: str = ""
    callable_signature: str = ""


@dataclass(frozen=True)
class CallbackParameter:
    name: str
    role: str
    access: str
    nullable: bool = False
    unit: str = ""


@dataclass(frozen=True)
class CallbackReturn:
    name: str
    role: str
    nullable: bool = False
    unit: str = ""


@dataclass(frozen=True)
class CallbackCodec:
    """Declarative Lua callback conversion and documentation policy."""

    name: str
    selector: CallbackSelector
    canonical_type: str
    native_callable: str
    codec: str
    lua_type: str
    lua_signature: str
    allow_nil: bool = False
    thread_policy: str = "blockingEnter"
    directions: tuple[str, ...] = ("fromLua",)
    parameters: tuple[CallbackParameter, ...] = ()
    returns: tuple[CallbackReturn, ...] = ()
    clear_setter_on_quiesce: bool = False


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
    memory_open_method: str = ""
    stream_open_method: str = ""


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
    memory_open_method="openFromMemory",
    stream_open_method="openFromStream",
))
_register(TypeLifecycle(
    "sf::InputSoundFile",
    LifecycleCategory.BOTH,
    constructor_patterns=("from_file", "from_memory", "from_stream"),
    reset_methods=("close", "openFromFile", "openFromStream"),
    memory_constructor_via_openfrommemory=True,
    memory_open_method="openFromMemory",
    stream_open_method="openFromStream",
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
    memory_open_method="openFromMemory",
    stream_open_method="openFromStream",
))


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
    "bind_Event": {"bind_Vector2", "bind_Joystick", "bind_Keyboard", "bind_Mouse", "bind_Sensor"},
    "bind_Handle": set(),
    "bind_Drawable": set(),
    "bind_ClassSupport": {"bind_Sprite"},
}

MANUAL_HEADER_OWNERS: dict[str, str] = {
    "Drawable": "bind_Drawable",
    "Event": "bind_Event",
    "WindowHandle": "bind_Handle",
}

MANUAL_HEADER_DECLARATION_PREFIX_OWNERS: dict[str, dict[str, str]] = {}


# ===========================================================================
# 3a. Template Specialization Bindings
# ===========================================================================

LUA_NAMESPACE_PROJECTIONS: dict[str, str] = {
    "sf::Glsl": "sf",
}

_VECTOR_OPERATORS = (
    "operator-:unary",
    "operator+:binary",
    "operator-:binary",
    "operator*:binary",
    "operator/:binary",
    "operator==:binary",
)

_VECTOR_FLOAT_ONLY = (
    "length",
    "normalized",
    "angleTo",
    "angle",
    "rotatedBy",
    "projectedOnto",
)

TEMPLATE_PROFILES: dict[str, TemplateProfile] = {
    "sf::Vector2": TemplateProfile(
        cpp_template="sf::Vector2",
        allowed_operators=_VECTOR_OPERATORS,
        configured_bindings=(
            ConfiguredBinding("member", "template_unpack", name="unpack", values=(("fields", "x,y"),)),
            ConfiguredBinding("member", "template_components_tostring", values=(("fields", "x,y"),)),
        ),
    ),
    "sf::Vector3": TemplateProfile(
        cpp_template="sf::Vector3",
        allowed_operators=_VECTOR_OPERATORS,
        configured_bindings=(
            ConfiguredBinding("member", "template_unpack", name="unpack", values=(("fields", "x,y,z"),)),
            ConfiguredBinding("member", "template_components_tostring", values=(("fields", "x,y,z"),)),
        ),
    ),
    "sf::priv::Vector4": TemplateProfile(
        cpp_template="sf::priv::Vector4",
        configured_bindings=(
            ConfiguredBinding("member", "template_unpack", name="unpack", values=(("fields", "x,y,z,w"),)),
            ConfiguredBinding("member", "template_components_tostring", values=(("fields", "x,y,z,w"),)),
        ),
    ),
    "sf::Rect": TemplateProfile(
        cpp_template="sf::Rect",
        allowed_operators=("operator==:binary",),
        configured_bindings=(ConfiguredBinding("member", "template_rect_tostring"),),
    ),
    "sf::priv::Matrix": TemplateProfile(
        cpp_template="sf::priv::Matrix",
        configured_bindings=(
            ConfiguredBinding("member", "template_matrix_array_field"),
            ConfiguredBinding("member", "template_matrix_copy"),
            ConfiguredBinding("member", "template_matrix_tostring"),
        ),
        replaced_fields=("array",),
        field_lua_types=(("array", "number[]"),),
    ),
    "sf::Music::Span": TemplateProfile(cpp_template="sf::Music::Span"),
}


def _rect_scalar_constructor(scalar_param: str, scalar_expr_suffix: str, scalar_lua: str) -> ConfiguredBinding:
    return ConfiguredBinding(
        "constructor",
        "template_rect_scalar_constructor",
        stub_signature=(
            f"fun(x: {scalar_lua}, y: {scalar_lua}, width: {scalar_lua}, "
            f"height: {scalar_lua}): {{lua_path}}"
        ),
        values=(("scalar_param", scalar_param), ("scalar_expr_suffix", scalar_expr_suffix)),
    )


def _matrix_array_constructor(element_count: int) -> ConfiguredBinding:
    return ConfiguredBinding(
        "constructor",
        "template_matrix_array_constructor",
        stub_signature="fun(values: number[]): {lua_path}",
        values=(("element_count", str(element_count)),),
    )


TEMPLATE_SPECIALIZATION_OVERRIDES: dict[str, TemplateSpecializationOverride] = {
    "sf::Vector2<int>": TemplateSpecializationOverride(
        cpp_type="sf::Vector2<int>",
        disabled_members=_VECTOR_FLOAT_ONLY,
        disabled_constructors=("r,phi",),
    ),
    "sf::Vector2<unsigned int>": TemplateSpecializationOverride(
        cpp_type="sf::Vector2<unsigned int>",
        disabled_members=_VECTOR_FLOAT_ONLY,
        disabled_constructors=("r,phi",),
    ),
    "sf::Vector2<bool>": TemplateSpecializationOverride(
        cpp_type="sf::Vector2<bool>",
        lua_path="sf.Vector2b",
        disabled_members=(
            "length", "lengthSquared", "normalized", "angleTo", "angle",
            "rotatedBy", "projectedOnto", "perpendicular", "dot", "cross",
            "componentWiseMul", "componentWiseDiv",
        ),
        disabled_constructors=("r,phi",),
        allowed_operators=("operator==:binary",),
    ),
    "sf::Vector3<int>": TemplateSpecializationOverride(
        cpp_type="sf::Vector3<int>", disabled_members=("length", "normalized")
    ),
    "sf::Vector3<unsigned int>": TemplateSpecializationOverride(
        cpp_type="sf::Vector3<unsigned int>", disabled_members=("length", "normalized")
    ),
    "sf::Vector3<bool>": TemplateSpecializationOverride(
        cpp_type="sf::Vector3<bool>",
        lua_path="sf.Vector3b",
        disabled_members=(
            "length", "lengthSquared", "normalized", "dot", "cross",
            "componentWiseMul", "componentWiseDiv",
        ),
        allowed_operators=("operator==:binary",),
    ),
    "sf::priv::Vector4<int>": TemplateSpecializationOverride(
        cpp_type="sf::priv::Vector4<int>", lua_path="sf.Vector4i", dependencies=("sf::Color",)
    ),
    "sf::priv::Vector4<float>": TemplateSpecializationOverride(
        cpp_type="sf::priv::Vector4<float>", lua_path="sf.Vector4f", dependencies=("sf::Color",)
    ),
    "sf::priv::Vector4<bool>": TemplateSpecializationOverride(
        cpp_type="sf::priv::Vector4<bool>",
        lua_path="sf.Vector4b",
        disabled_constructors=("color",),
    ),
    "sf::Rect<int>": TemplateSpecializationOverride(
        cpp_type="sf::Rect<int>",
        configured_bindings=(_rect_scalar_constructor("lua_sf::LuaIntegral<int>", ".value()", "integer"),),
    ),
    "sf::Rect<float>": TemplateSpecializationOverride(
        cpp_type="sf::Rect<float>",
        configured_bindings=(_rect_scalar_constructor("float", "", "number"),),
    ),
    "sf::priv::Matrix<3, 3>": TemplateSpecializationOverride(
        cpp_type="sf::priv::Matrix<3, 3>",
        lua_path="sf.Mat3",
        disabled_constructors=("pointer",),
        configured_bindings=(_matrix_array_constructor(9),),
        dependencies=("sf::Transform",),
    ),
    "sf::priv::Matrix<4, 4>": TemplateSpecializationOverride(
        cpp_type="sf::priv::Matrix<4, 4>",
        lua_path="sf.Mat4",
        disabled_constructors=("pointer",),
        configured_bindings=(_matrix_array_constructor(16),),
        dependencies=("sf::Transform",),
    ),
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
# 5. Configured Native Method Overrides
# ===========================================================================

CONFIGURED_METHOD_OVERRIDES: tuple[ConfiguredMethodOverride, ...] = (
    ConfiguredMethodOverride(
        qualified_function="sf::Shader::setUniformArray",
        lua_name="setUniformArray",
        helper="lua_sf::detail::bindShaderUniformArrays",
        variant_factory="lua_sf::detail::shaderUniformArrayVariant",
        native_parameter_types=(
            "const std::string&",
            "const {element}*",
            "std::size_t",
        ),
        native_return_type="void",
        variants=(
            ConfiguredMethodVariant("setUniformFloatArray", "float", "number[]"),
            ConfiguredMethodVariant("setUniformVec2Array", "sf::Glsl::Vec2", "sf.Vector2f[]"),
            ConfiguredMethodVariant("setUniformVec3Array", "sf::Glsl::Vec3", "sf.Vector3f[]"),
            ConfiguredMethodVariant("setUniformVec4Array", "sf::Glsl::Vec4", "sf.Vector4f[]"),
            ConfiguredMethodVariant("setUniformMat3Array", "sf::Glsl::Mat3", "sf.Mat3[]"),
            ConfiguredMethodVariant("setUniformMat4Array", "sf::Glsl::Mat4", "sf.Mat4[]"),
        ),
    ),
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
    "return lua_sf::wrapLuaSharedObject(std::move(object));",
)

_t("ll_memory_ctor_via_open",
    "auto {data_name}_buffer = lua_sf::makeLongLivedMemoryBuffer({data_name});",
    "auto object = lua_sf::makeLongLivedMemoryObject<{owner_type}>();",
    "if (!object->openFromMemory({data_name}_buffer->data(), static_cast<{size_type}>({data_name}_buffer->size())))",
    '    throw std::runtime_error("Failed to open {lua_path} from memory");',
    "lua_sf::rememberLongLivedMemory(*object, std::move({data_name}_buffer));",
    "return lua_sf::wrapLuaSharedObject(std::move(object));",
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
    "return lua_sf::wrapLuaSharedObject(std::move(object));",
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

_t("template_unpack",
    'LUASF_STUB_FUNCTION("{lua_path}", "unpack", "fun(self: {lua_path}): {field_lua_returns}");',
    '{var_name}.set_function("unpack", [](const {cpp_type}& self) {{',
    '    return std::make_tuple({field_exprs});',
    '}});',
)

_t("template_components_tostring",
    '{var_name}[sol::meta_function::to_string] = [name = std::string("{lua_leaf}")](const {cpp_type}& self) {{',
    '    std::ostringstream stream;',
    '    stream << name << "(" << {stream_components} << ")";',
    '    return stream.str();',
    '}};',
)

_t("template_rect_tostring",
    '{var_name}[sol::meta_function::to_string] = [name = std::string("{lua_leaf}")](const {cpp_type}& self) {{',
    '    std::ostringstream stream;',
    '    stream << name << "(" << self.position.x << ", " << self.position.y << ", "',
    '           << self.size.x << ", " << self.size.y << ")";',
    '    return stream.str();',
    '}};',
)

_t("template_rect_scalar_constructor",
    '[]({scalar_param} x, {scalar_param} y, {scalar_param} width, {scalar_param} height) {{',
    '    return {cpp_type}{{{{x{scalar_expr_suffix}, y{scalar_expr_suffix}}},',
    '                       {{width{scalar_expr_suffix}, height{scalar_expr_suffix}}}}};',
    '}}',
)

_t("template_matrix_array_constructor",
    '[](sol::table values) {{',
    '    auto buffer = lua_sf::array_from_object<float>(values);',
    '    if (buffer.size() != {element_count})',
    '        throw std::runtime_error("matrix constructor expects exactly {element_count} float values");',
    '    return {cpp_type}{{buffer.data()}};',
    '}}',
)

_t("template_matrix_array_field",
    '{var_name}.set("array", sol::property(',
    '    [](const {cpp_type}& self) {{',
    '        return sol::as_table(std::vector<float>(self.array.begin(), self.array.end()));',
    '    }},',
    '    []({cpp_type}& self, sol::object values) {{',
    '        auto buffer = lua_sf::array_from_object<float>(values);',
    '        if (buffer.size() != self.array.size())',
    '            throw std::runtime_error("matrix array assignment has the wrong number of float values");',
    '        std::copy(buffer.begin(), buffer.end(), self.array.begin());',
    '    }}));',
)

_t("template_matrix_copy",
    'LUASF_STUB_FUNCTION("{lua_path}", "copyMatrix", "fun(source: sf.Transform, dest: {lua_path})");',
    '{var_name}.set_function("copyMatrix", [](const sf::Transform& source, {cpp_type}& dest) {{',
    '    sf::priv::copyMatrix(source, dest);',
    '}});',
)

_t("template_matrix_tostring",
    '{var_name}[sol::meta_function::to_string] = [name = std::string("{lua_leaf}")](const {cpp_type}& self) {{',
    '    std::ostringstream stream;',
    '    stream << name << "(";',
    '    for (std::size_t index = 0; index < self.array.size(); ++index) {{',
    '        if (index != 0)',
    '            stream << ", ";',
    '        stream << self.array[index];',
    '    }}',
    '    stream << ")";',
    '    return stream.str();',
    '}};',
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

OUTPUT_REFERENCE_POLICIES: tuple[OutputReferencePolicy, ...] = (
    OutputReferencePolicy(
        "sf::TcpListener::accept",
        ("sf::TcpSocket&",),
        (OutputReferenceParameter(0, "socket"),),
    ),
    OutputReferencePolicy(
        "sf::TcpSocket::send",
        ("const void*", "std::size_t", "std::size_t&"),
        (OutputReferenceParameter(2, "sent"),),
    ),
    OutputReferencePolicy(
        "sf::TcpSocket::receive",
        ("void*", "std::size_t", "std::size_t&"),
        (OutputReferenceParameter(2, "received", count_for_array_parameter=0),),
    ),
    OutputReferencePolicy(
        "sf::TcpSocket::receive",
        ("sf::Packet&",),
        (OutputReferenceParameter(0, "packet"),),
    ),
    OutputReferencePolicy(
        "sf::UdpSocket::receive",
        (
            "void*",
            "std::size_t",
            "std::size_t&",
            "std::optional<sf::IpAddress>&",
            "unsigned short&",
        ),
        (
            OutputReferenceParameter(2, "received", count_for_array_parameter=0),
            OutputReferenceParameter(3, "remoteAddress"),
            OutputReferenceParameter(4, "remotePort"),
        ),
    ),
    OutputReferencePolicy(
        "sf::UdpSocket::receive",
        ("sf::Packet&", "std::optional<sf::IpAddress>&", "unsigned short&"),
        (
            OutputReferenceParameter(0, "packet"),
            OutputReferenceParameter(1, "remoteAddress"),
            OutputReferenceParameter(2, "remotePort"),
        ),
    ),
)

SKIPPED_CLASS_BINDINGS: dict[str, str] = {
    "sf::String": "converted through lua_sf string utilities",
}

SPECIAL_POINTER_RETURNS: dict[str, tuple[str, str]] = {
    "sf::Transform::getMatrix": ("float", "16"),
    "sf::Image::getPixelsPtr": ("std::uint8_t", "self.getSize().x * self.getSize().y * 4"),
    "sf::SoundBuffer::getSamples": ("std::int16_t", "self.getSampleCount()"),
}

CALLBACK_CODEC_SCHEMA_VERSION: int = 1

CALLBACK_CODEC_REGISTRY: tuple[CallbackCodec, ...] = (
    CallbackCodec(
        name="interleavedFloatTransform",
        selector=CallbackSelector(semantic_alias="sf::SoundSource::EffectProcessor"),
        canonical_type="std::function<void(const float*, unsigned int&, float*, unsigned int&, unsigned int)>",
        native_callable="sf::SoundSource::EffectProcessor",
        codec="lua_sf::callback::InterleavedFloatTransformCodec",
        lua_type="sf.SoundSource.EffectProcessor",
        lua_signature=(
            "fun(inputFrames: number[]|nil, inputFrameCount: integer, "
            "outputFrames: number[], outputFrameCount: integer, "
            "frameChannelCount: integer): {inputFrameCount: integer, "
            "outputFrameCount: integer, outputFrames: number[]?}"
        ),
        allow_nil=True,
        thread_policy="nativeTryEnter",
        directions=("fromLua", "toLua"),
        parameters=(
            CallbackParameter("inputFrames", "copiedInputBuffer", "read", nullable=True, unit="sample"),
            CallbackParameter("inputFrameCount", "inputCapacity", "read", unit="frame"),
            CallbackParameter("outputFrames", "zeroInitializedOutputBuffer", "readWrite", unit="sample"),
            CallbackParameter("outputFrameCount", "outputCapacity", "read", unit="frame"),
            CallbackParameter("frameChannelCount", "channelCount", "read", unit="channel"),
        ),
        returns=(
            CallbackReturn("inputFrameCount", "consumedInputCount", unit="frame"),
            CallbackReturn("outputFrameCount", "producedOutputCount", unit="frame"),
            CallbackReturn("outputFrames", "replacementOutputBuffer", nullable=True, unit="sample"),
        ),
    ),
    CallbackCodec(
        name="glyphPreProcessor",
        selector=CallbackSelector(semantic_alias="sf::Text::GlyphPreProcessor"),
        canonical_type=(
            "std::function<void(const sf::Text::ShapedGlyph&, std::uint32_t&, "
            "sf::Color&, sf::Color&, float&)>"
        ),
        native_callable="sf::Text::GlyphPreProcessor",
        codec="lua_sf::callback::GlyphPreProcessorCodec",
        lua_type="sf.Text.GlyphPreProcessor",
        lua_signature=(
            "fun(shapedGlyph: sf.Text.ShapedGlyph, style: integer, fillColor: sf.Color, "
            "outlineColor: sf.Color, outlineThickness: number): "
            "{style: integer?, fillColor: sf.Color?, outlineColor: sf.Color?, outlineThickness: number?}|nil"
        ),
        allow_nil=True,
        parameters=(
            CallbackParameter("shapedGlyph", "value", "read"),
            CallbackParameter("style", "value", "readWrite"),
            CallbackParameter("fillColor", "value", "readWrite"),
            CallbackParameter("outlineColor", "value", "readWrite"),
            CallbackParameter("outlineThickness", "value", "readWrite"),
        ),
        returns=(CallbackReturn("updates", "inoutPatch", nullable=True),),
    ),
    CallbackCodec(
        name="sftpDownloadBuffer",
        selector=CallbackSelector(
            qualified_function="sf::Sftp::download",
            parameter_name="callback",
            callable_signature="bool(const void*, std::size_t)",
        ),
        canonical_type="std::function<bool(const void*, std::size_t)>",
        native_callable="std::function<bool(const void*, std::size_t)>",
        codec="lua_sf::callback::SftpDownloadBufferCodec",
        lua_type="fun(data: string, size: integer): boolean",
        lua_signature="fun(data: string, size: integer): boolean",
        parameters=(
            CallbackParameter("data", "inputBuffer", "read", unit="byte"),
            CallbackParameter("size", "inputCount", "read", unit="byte"),
        ),
        returns=(CallbackReturn("keepGoing", "continueFlag"),),
    ),
    CallbackCodec(
        name="sftpUploadBuffer",
        selector=CallbackSelector(
            qualified_function="sf::Sftp::upload",
            parameter_name="callback",
            callable_signature="bool(void*, std::size_t&)",
        ),
        canonical_type="std::function<bool(void*, std::size_t&)>",
        native_callable="std::function<bool(void*, std::size_t&)>",
        codec="lua_sf::callback::SftpUploadBufferCodec",
        lua_type=(
            "fun(capacity: integer): string|integer[]|"
            "{keepGoing: boolean?, data: string|integer[]?}|boolean|nil"
        ),
        lua_signature=(
            "fun(capacity: integer): string|integer[]|"
            "{keepGoing: boolean?, data: string|integer[]?}|boolean|nil"
        ),
        parameters=(
            CallbackParameter("capacity", "outputCapacity", "read", unit="byte"),
        ),
        returns=(CallbackReturn("result", "bufferFillResult", nullable=True, unit="byte"),),
    ),
    CallbackCodec(
        name="playbackDeviceNotification",
        selector=CallbackSelector(
            semantic_alias="sf::PlaybackDevice::NotificationCallback",
        ),
        canonical_type="std::function<void(sf::PlaybackDevice::Notification)>",
        native_callable="sf::PlaybackDevice::NotificationCallback",
        codec="lua_sf::callback::NativeThreadBoundaryCodec",
        lua_type="sf.PlaybackDevice.NotificationCallback",
        lua_signature="fun(notification: sf.PlaybackDevice.Notification)",
        allow_nil=True,
        thread_policy="nativeThreadBoundary",
        parameters=(CallbackParameter("notification", "value", "read"),),
        returns=(),
        clear_setter_on_quiesce=True,
    ),
)

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
    "char32_t",
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
