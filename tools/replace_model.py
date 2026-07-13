"""
Query and rendering API for LuaSF binding code generation.

Imports pure data from ``binding_config`` and exposes functions that
``generate_sol2_bindings.py`` and ``generate_build_files.py`` call.

Sections
--------
7. Lambda Generators — param-info helpers and render_* functions for special bindings
"""

from __future__ import annotations

from typing import Any

try:
    from .binding_config import (
        BINDING_TEMPLATES,
        BYTE_TYPES,
        CONVERSION_REGISTRY,
        CPP_BUILTIN_TYPES,
        INTEGER_TYPES,
        LIFECYCLE_REGISTRY,
        LifecycleCategory,
        NUMBER_TYPES,
        PACKET_IO_REGISTRY,
        PUBLIC_TYPE_ALIASES,
        SIZE_TYPE_NAMES,
        STRING_TYPES,
        TypeRef,
        clean_cpp_type,
        core_cpp_type,
        is_anonymous_cpp_name,
        is_pointer,
        is_reference,
        is_size_type,
        lua_path_for_type,
        normalize_array_element,
        qualified_name_for_token_from_canonical,
        qualify_known_public_type_tokens,
        qualify_public_spelling,
        qualify_sfml_template_aliases,
        remove_cvref,
        sanitize_identifier,
        semantic_cpp_type,
        set_public_type_aliases,
        walk_declarations,
    )
except ImportError:
    from binding_config import (
        BINDING_TEMPLATES,
        BYTE_TYPES,
        CONVERSION_REGISTRY,
        CPP_BUILTIN_TYPES,
        INTEGER_TYPES,
        LIFECYCLE_REGISTRY,
        LifecycleCategory,
        NUMBER_TYPES,
        PACKET_IO_REGISTRY,
        PUBLIC_TYPE_ALIASES,
        SIZE_TYPE_NAMES,
        STRING_TYPES,
        TypeRef,
        clean_cpp_type,
        core_cpp_type,
        is_anonymous_cpp_name,
        is_pointer,
        is_reference,
        is_size_type,
        lua_path_for_type,
        normalize_array_element,
        qualified_name_for_token_from_canonical,
        qualify_known_public_type_tokens,
        qualify_public_spelling,
        qualify_sfml_template_aliases,
        remove_cvref,
        sanitize_identifier,
        semantic_cpp_type,
        set_public_type_aliases,
        walk_declarations,
    )

# Re-export everything that external callers need
__all__ = [
    # From binding_config (re-exported for convenience)
    "AUDIO_EFFECT_PROCESSOR_LUA_TYPE",
    "AUDIO_EFFECT_PROCESSOR_SIGNATURE",
    "SPECIAL_CALLBACK_LUA_TYPES",
    "BINDING_TEMPLATES",
    "BYTE_TYPES",
    "CONVERSION_REGISTRY",
    "CPP_BUILTIN_TYPES",
    "IGNORE_NAMES",
    "IGNORE_PARAM_TYPES",
    "IGNORE_RETURN_TYPES",
    "IGNORED_NAMESPACES",
    "INTEGER_TYPES",
    "LIFECYCLE_REGISTRY",
    "LifecycleCategory",
    "LONG_LIVED_RESOURCE_RESET_METHODS",
    "LUA_KEYWORDS",
    "MANUAL_DEPENDENCIES",
    "MANUAL_HEADER_DECLARATION_PREFIX_OWNERS",
    "MANUAL_HEADER_OWNERS",
    "MODULE_ORDER",
    "NUMBER_TYPES",
    "NUMERIC_ARRAY_TYPES",
    "OPERATOR_META_FUNCTIONS",
    "OUTPUT_ARRAY_COUNT_REF_NAMES",
    "OUTPUT_REF_FUNCTIONS",
    "OUTPUT_REF_NAMES",
    "PACKET_IO_REGISTRY",
    "PUBLIC_TYPE_ALIASES",
    "PacketIoType",
    "SHADER_UNIFORM_ARRAY_BINDINGS",
    "SIZE_TYPE_NAMES",
    "SPECIAL_POINTER_RETURNS",
    "STRING_TYPES",
    "TYPE_DECL_KINDS",
    "TypeConversion",
    "TypeLifecycle",
    "TypeRef",
    "clean_cpp_type",
    "core_cpp_type",
    "is_anonymous_cpp_name",
    "qualified_name_for_token_from_canonical",
    "qualify_known_public_type_tokens",
    "qualify_public_spelling",
    "qualify_sfml_template_aliases",
    "sanitize_identifier",
    "semantic_cpp_type",
    "set_public_type_aliases",
    "walk_declarations",
    # Query functions
    "get_conversion",
    "get_lifecycle",
    "is_long_lived_memory_type",
    "is_long_lived_stream_type",
    "packet_io_info",
    # Template rendering
    "render_template",
    # Lambda generators
    "param_info_memory",
    "param_info_stream",
    "render_ll_memory_ctor",
    "render_ll_memory_open",
    "render_ll_reset",
    "render_ll_stream_ctor",
    "render_ll_stream_open",
    "render_shader_uniform_array",
]

# Re-export data from binding_config
try:
    from .binding_config import (  # noqa: E402
        AUDIO_EFFECT_PROCESSOR_LUA_TYPE,
        AUDIO_EFFECT_PROCESSOR_SIGNATURE,
        BYTE_TYPES,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LONG_LIVED_RESOURCE_RESET_METHODS,
        LUA_KEYWORDS,
        MANUAL_DEPENDENCIES,
        MANUAL_HEADER_DECLARATION_PREFIX_OWNERS,
        MANUAL_HEADER_OWNERS,
        MODULE_ORDER,
        NUMBER_TYPES,
        NUMERIC_ARRAY_TYPES,
        OPERATOR_META_FUNCTIONS,
        OUTPUT_ARRAY_COUNT_REF_NAMES,
        OUTPUT_REF_FUNCTIONS,
        OUTPUT_REF_NAMES,
        PACKET_IO_REGISTRY,
        PacketIoType,
        SHADER_UNIFORM_ARRAY_BINDINGS,
        SIZE_TYPE_NAMES,
        SPECIAL_CALLBACK_LUA_TYPES,
        SPECIAL_POINTER_RETURNS,
        STRING_TYPES,
        TYPE_DECL_KINDS,
        TypeConversion,
        TypeLifecycle,
    )
except ImportError:
    from binding_config import (  # noqa: E402
        AUDIO_EFFECT_PROCESSOR_LUA_TYPE,
        AUDIO_EFFECT_PROCESSOR_SIGNATURE,
        BYTE_TYPES,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LONG_LIVED_RESOURCE_RESET_METHODS,
        LUA_KEYWORDS,
        MANUAL_DEPENDENCIES,
        MANUAL_HEADER_DECLARATION_PREFIX_OWNERS,
        MANUAL_HEADER_OWNERS,
        MODULE_ORDER,
        NUMBER_TYPES,
        NUMERIC_ARRAY_TYPES,
        OPERATOR_META_FUNCTIONS,
        OUTPUT_ARRAY_COUNT_REF_NAMES,
        OUTPUT_REF_FUNCTIONS,
        OUTPUT_REF_NAMES,
        PACKET_IO_REGISTRY,
        PacketIoType,
        SHADER_UNIFORM_ARRAY_BINDINGS,
        SIZE_TYPE_NAMES,
        SPECIAL_CALLBACK_LUA_TYPES,
        SPECIAL_POINTER_RETURNS,
        STRING_TYPES,
        TYPE_DECL_KINDS,
        TypeConversion,
        TypeLifecycle,
    )


# ---------------------------------------------------------------------------
# Query functions
# ---------------------------------------------------------------------------


def get_lifecycle(qualified_name: str) -> TypeLifecycle | None:
    return LIFECYCLE_REGISTRY.get(qualified_name)


def is_long_lived_memory_type(qualified_name: str) -> bool:
    lc = LIFECYCLE_REGISTRY.get(qualified_name)
    return lc is not None and lc.category in (LifecycleCategory.MEMORY, LifecycleCategory.BOTH)


def is_long_lived_stream_type(qualified_name: str) -> bool:
    lc = LIFECYCLE_REGISTRY.get(qualified_name)
    return lc is not None and lc.category in (LifecycleCategory.STREAM, LifecycleCategory.BOTH)


def get_conversion(cpp_type: str) -> TypeConversion | None:
    return CONVERSION_REGISTRY.get(clean_cpp_type(cpp_type))


def packet_io_info(cpp_type: str) -> dict[str, Any] | None:
    """Return packet read/write metadata for *cpp_type*, or None."""
    packet_type = PACKET_IO_REGISTRY.get(clean_cpp_type(cpp_type))
    if packet_type is None:
        return None
    return {
        "suffix": packet_type.suffix,
        "aliases": list(packet_type.packet_aliases),
        "lua": packet_type.packet_lua_type,
        "result": packet_type.packet_result_expr,
    }


# ---------------------------------------------------------------------------
# Template rendering
# ---------------------------------------------------------------------------


def render_template(name: str, **kwargs: object) -> str:
    """Render a named template by substituting placeholders.

    Raises ``KeyError`` if the template name is unknown or a placeholder is
    missing from *kwargs*.
    """
    try:
        lines = BINDING_TEMPLATES[name]
    except KeyError:
        available = ", ".join(sorted(BINDING_TEMPLATES))
        raise KeyError(f"unknown template {name!r}; available: {available}") from None
    return "\n".join(line.format(**kwargs) for line in lines)


# ---------------------------------------------------------------------------
# 7. Lambda Generators
# ---------------------------------------------------------------------------


def param_info_memory(params: list[dict[str, Any]]) -> tuple[str, str] | None:
    """If *params* match a long-lived memory signature, return (data_name, size_type)."""
    if len(params) < 2:
        return None
    pointer_param = params[0]
    size_param = params[1]
    pointer_type = TypeRef.from_json(pointer_param.get("type"))
    size_type = TypeRef.from_json(size_param.get("type"))
    if not is_pointer(pointer_type.cpp) or not is_size_type(size_type):
        return None
    if normalize_array_element(pointer_type.cpp) not in BYTE_TYPES:
        return None
    return sanitize_identifier(pointer_param.get("name") or "data"), size_type.cpp


def param_info_stream(params: list[dict[str, Any]]) -> str | None:
    """If *params* match a long-lived stream signature, return the stream parameter name."""
    if len(params) != 1:
        return None
    param = params[0]
    type_ref = TypeRef.from_json(param.get("type"))
    if not is_reference(type_ref.cpp) or remove_cvref(type_ref.cpp) != "sf::InputStream":
        return None
    return sanitize_identifier(param.get("name") or "stream")


def _indent(body: str, prefix: str = "    ") -> str:
    """Indent every line of *body* by *prefix*."""
    return prefix + body.replace("\n", "\n" + prefix)


# -- public entry points called from generate_sol2_bindings.make_lambda --


def render_ll_memory_ctor(
    owner_type: str,
    params: list[dict[str, Any]],
    lua_params: list[str],
) -> str:
    """Render a long-lived memory constructor lambda."""
    info = param_info_memory(params)
    if info is None:
        raise ValueError("long-lived memory constructor requires a byte pointer and size parameter")
    data_name, size_type = info
    lua_args = ", ".join(lua_params)

    if clean_cpp_type(owner_type) == "sf::MemoryInputStream":
        tmpl = "ll_memory_ctor_direct"
        kwargs = dict(data_name=data_name, owner_type=owner_type, size_type=size_type)
    else:
        tmpl = "ll_memory_ctor_via_open"
        kwargs = dict(data_name=data_name, owner_type=owner_type, size_type=size_type,
                      lua_path=lua_path_for_type(owner_type))

    body = render_template(tmpl, **kwargs)
    return f"[]({lua_args}) {{\n{_indent(body)}\n}}"


def render_ll_memory_open(
    owner_type: str,
    params: list[dict[str, Any]],
    lua_params: list[str],
    call_target: str,
) -> str:
    """Render a long-lived memory openFromMemory method lambda."""
    info = param_info_memory(params)
    if info is None:
        raise ValueError("long-lived memory open requires a byte pointer and size parameter")
    data_name, size_type = info
    lua_args = ", ".join(lua_params)
    if owner_type:
        lua_args = f"{owner_type}& self" + (f", {lua_args}" if lua_args else "")
    body = render_template("ll_memory_open", data_name=data_name,
                           size_type=size_type, call_target=call_target)
    return f"[]({lua_args}) -> bool {{\n{_indent(body)}\n}}"


def render_ll_stream_ctor(
    owner_type: str,
    params: list[dict[str, Any]],
) -> str:
    """Render a long-lived stream constructor lambda."""
    stream_name = param_info_stream(params)
    if stream_name is None:
        raise ValueError("long-lived stream constructor requires an sf::InputStream reference")
    body = render_template("ll_stream_ctor", stream_name=stream_name, owner_type=owner_type)
    return f"[](sol::object {stream_name}) {{\n{_indent(body)}\n}}"


def render_ll_stream_open(
    owner_type: str,
    params: list[dict[str, Any]],
    call_target: str,
) -> str:
    """Render a long-lived stream openFromStream method lambda."""
    stream_name = param_info_stream(params)
    if stream_name is None:
        raise ValueError("long-lived stream open requires an sf::InputStream reference")
    body = render_template("ll_stream_open", stream_name=stream_name, call_target=call_target)
    return f"[]({owner_type}& self, sol::object {stream_name}) -> bool {{\n{_indent(body)}\n}}"


def render_ll_reset(
    owner_type: str,
    lua_params: list[str],
    prelude: list[str],
    post_values: list[str],
    call_expr: str,
    return_type_cpp: str,
) -> str | None:
    """Render a long-lived resource-reset lambda (close, openFromFile, etc.)."""
    if post_values:
        return None
    lua_args = ", ".join(lua_params)
    lua_args = f"{owner_type}& self" + (f", {lua_args}" if lua_args else "")
    return_prefix = "" if return_type_cpp in {"", "void"} else f" -> {return_type_cpp}"

    if return_type_cpp in {"", "void"}:
        body = render_template("ll_reset_void", call_expr=call_expr)
    else:
        body = render_template("ll_reset_nonvoid", call_expr=call_expr)

    parts = [f"[]({lua_args}){return_prefix} {{"]
    if prelude:
        parts.append(_indent("\n".join(prelude)))
    parts.append(_indent(body))
    parts.append("}")
    return "\n".join(parts)


def render_shader_uniform_array(
    cpp_type: str,
    param_name: str,
) -> str:
    """Render a single shader setUniform*Array dispatcher lambda."""
    body = render_template("shader_uniform_array", cpp_type=cpp_type, param=param_name)
    return f"[](sf::Shader& self, std::string name, sol::object {param_name}) {{\n{_indent(body)}\n}}"
