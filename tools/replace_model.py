"""
Query and rendering API for LuaSF binding code generation.

Imports pure data from ``binding_config`` and exposes functions that
``generate_sol2_bindings.py`` and ``generate_build_files.py`` call.

Sections
--------
7. Lambda Generators — param-info helpers and render_* functions for special bindings
"""

from __future__ import annotations

import re
from typing import Any

try:
    from .binding_config import (
        BINDING_TEMPLATES,
        BYTE_TYPES,
        CALLBACK_CODEC_REGISTRY,
        CALLBACK_CODEC_SCHEMA_VERSION,
        CONFIGURED_METHOD_OVERRIDES,
        CallbackCodec,
        CallbackParameter,
        CallbackReturn,
        CallbackSelector,
        ConfiguredBinding,
        ConfiguredMethodOverride,
        ConfiguredMethodVariant,
        CONVERSION_REGISTRY,
        CPP_BUILTIN_TYPES,
        INTEGER_TYPES,
        LIFECYCLE_REGISTRY,
        LUA_NAMESPACE_PROJECTIONS,
        LifecycleCategory,
        NUMBER_TYPES,
        OUTPUT_REFERENCE_POLICIES,
        OutputReferenceParameter,
        OutputReferencePolicy,
        PACKET_IO_REGISTRY,
        PUBLIC_TYPE_ALIASES,
        SIZE_TYPE_NAMES,
        SKIPPED_CLASS_BINDINGS,
        STRING_TYPES,
        TEMPLATE_PROFILES,
        TEMPLATE_SPECIALIZATION_OVERRIDES,
        TemplateProfile,
        TemplateSpecializationOverride,
        TypeRef,
        clean_cpp_type,
        core_cpp_type,
        is_anonymous_cpp_name,
        is_const_type,
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
        CALLBACK_CODEC_REGISTRY,
        CALLBACK_CODEC_SCHEMA_VERSION,
        CONFIGURED_METHOD_OVERRIDES,
        CallbackCodec,
        CallbackParameter,
        CallbackReturn,
        CallbackSelector,
        ConfiguredBinding,
        ConfiguredMethodOverride,
        ConfiguredMethodVariant,
        CONVERSION_REGISTRY,
        CPP_BUILTIN_TYPES,
        INTEGER_TYPES,
        LIFECYCLE_REGISTRY,
        LUA_NAMESPACE_PROJECTIONS,
        LifecycleCategory,
        NUMBER_TYPES,
        OUTPUT_REFERENCE_POLICIES,
        OutputReferenceParameter,
        OutputReferencePolicy,
        PACKET_IO_REGISTRY,
        PUBLIC_TYPE_ALIASES,
        SIZE_TYPE_NAMES,
        SKIPPED_CLASS_BINDINGS,
        STRING_TYPES,
        TEMPLATE_PROFILES,
        TEMPLATE_SPECIALIZATION_OVERRIDES,
        TemplateProfile,
        TemplateSpecializationOverride,
        TypeRef,
        clean_cpp_type,
        core_cpp_type,
        is_anonymous_cpp_name,
        is_const_type,
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
    "BINDING_TEMPLATES",
    "BYTE_TYPES",
    "CALLBACK_CODEC_REGISTRY",
    "CALLBACK_CODEC_SCHEMA_VERSION",
    "CONFIGURED_METHOD_OVERRIDES",
    "CallbackCodec",
    "CallbackParameter",
    "CallbackReturn",
    "CallbackSelector",
    "ConfiguredBinding",
    "ConfiguredMethodOverride",
    "ConfiguredMethodVariant",
    "CONVERSION_REGISTRY",
    "CPP_BUILTIN_TYPES",
    "IGNORE_NAMES",
    "IGNORE_PARAM_TYPES",
    "IGNORE_RETURN_TYPES",
    "IGNORED_NAMESPACES",
    "INTEGER_TYPES",
    "LIFECYCLE_REGISTRY",
    "LifecycleCategory",
    "LUA_NAMESPACE_PROJECTIONS",
    "LUA_KEYWORDS",
    "MANUAL_DEPENDENCIES",
    "MANUAL_HEADER_DECLARATION_PREFIX_OWNERS",
    "MANUAL_HEADER_OWNERS",
    "MODULE_ORDER",
    "NUMBER_TYPES",
    "NUMERIC_ARRAY_TYPES",
    "OPERATOR_META_FUNCTIONS",
    "OUTPUT_REFERENCE_POLICIES",
    "OutputReferenceParameter",
    "OutputReferencePolicy",
    "PACKET_IO_REGISTRY",
    "PUBLIC_TYPE_ALIASES",
    "PacketIoType",
    "SIZE_TYPE_NAMES",
    "SKIPPED_CLASS_BINDINGS",
    "SPECIAL_POINTER_RETURNS",
    "STRING_TYPES",
    "TEMPLATE_PROFILES",
    "TEMPLATE_SPECIALIZATION_OVERRIDES",
    "TemplateProfile",
    "TemplateSpecializationOverride",
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
    "get_callback_codec",
    "get_configured_method_override",
    "get_lifecycle",
    "get_output_reference_policy",
    "skipped_class_binding_reason",
    "is_long_lived_memory_type",
    "is_long_lived_stream_type",
    "packet_io_info",
    "callback_codec_manifest",
    "validate_callback_codec_registry",
    "validate_configured_method_override_registry",
    "validate_output_reference_policy_registry",
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
]

# Re-export data from binding_config
try:
    from .binding_config import (  # noqa: E402
        BYTE_TYPES,
        CALLBACK_CODEC_REGISTRY,
        CALLBACK_CODEC_SCHEMA_VERSION,
        CONFIGURED_METHOD_OVERRIDES,
        CallbackCodec,
        CallbackParameter,
        CallbackReturn,
        CallbackSelector,
        ConfiguredBinding,
        ConfiguredMethodOverride,
        ConfiguredMethodVariant,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LUA_NAMESPACE_PROJECTIONS,
        LUA_KEYWORDS,
        MANUAL_DEPENDENCIES,
        MANUAL_HEADER_DECLARATION_PREFIX_OWNERS,
        MANUAL_HEADER_OWNERS,
        MODULE_ORDER,
        NUMBER_TYPES,
        NUMERIC_ARRAY_TYPES,
        OPERATOR_META_FUNCTIONS,
        OUTPUT_REFERENCE_POLICIES,
        OutputReferenceParameter,
        OutputReferencePolicy,
        PACKET_IO_REGISTRY,
        PacketIoType,
        SIZE_TYPE_NAMES,
        SKIPPED_CLASS_BINDINGS,
        SPECIAL_POINTER_RETURNS,
        STRING_TYPES,
        TEMPLATE_PROFILES,
        TEMPLATE_SPECIALIZATION_OVERRIDES,
        TemplateProfile,
        TemplateSpecializationOverride,
        TYPE_DECL_KINDS,
        TypeConversion,
        TypeLifecycle,
    )
except ImportError:
    from binding_config import (  # noqa: E402
        BYTE_TYPES,
        CALLBACK_CODEC_REGISTRY,
        CALLBACK_CODEC_SCHEMA_VERSION,
        CONFIGURED_METHOD_OVERRIDES,
        CallbackCodec,
        CallbackParameter,
        CallbackReturn,
        CallbackSelector,
        ConfiguredBinding,
        ConfiguredMethodOverride,
        ConfiguredMethodVariant,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LUA_NAMESPACE_PROJECTIONS,
        LUA_KEYWORDS,
        MANUAL_DEPENDENCIES,
        MANUAL_HEADER_DECLARATION_PREFIX_OWNERS,
        MANUAL_HEADER_OWNERS,
        MODULE_ORDER,
        NUMBER_TYPES,
        NUMERIC_ARRAY_TYPES,
        OPERATOR_META_FUNCTIONS,
        OUTPUT_REFERENCE_POLICIES,
        OutputReferenceParameter,
        OutputReferencePolicy,
        PACKET_IO_REGISTRY,
        PacketIoType,
        SIZE_TYPE_NAMES,
        SKIPPED_CLASS_BINDINGS,
        SPECIAL_POINTER_RETURNS,
        STRING_TYPES,
        TEMPLATE_PROFILES,
        TEMPLATE_SPECIALIZATION_OVERRIDES,
        TemplateProfile,
        TemplateSpecializationOverride,
        TYPE_DECL_KINDS,
        TypeConversion,
        TypeLifecycle,
    )


# ---------------------------------------------------------------------------
# Query functions
# ---------------------------------------------------------------------------


_CPP_QUALIFIED_IDENTIFIER = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*$"
)


def get_configured_method_override(
    qualified_function: str,
) -> ConfiguredMethodOverride | None:
    qualified_function = clean_cpp_type(qualified_function)
    matches = [
        override
        for override in CONFIGURED_METHOD_OVERRIDES
        if clean_cpp_type(override.qualified_function) == qualified_function
    ]
    if len(matches) > 1:
        raise ValueError(
            f"ambiguous configured method override for {qualified_function!r}"
        )
    return matches[0] if matches else None


def validate_configured_method_override_registry() -> None:
    selectors: set[str] = set()
    for override in CONFIGURED_METHOD_OVERRIDES:
        selector = clean_cpp_type(override.qualified_function)
        if not selector or selector in selectors:
            raise ValueError(
                f"duplicate or empty configured method override selector: {selector!r}"
            )
        selectors.add(selector)
        if (
            not override.lua_name
            or not _CPP_QUALIFIED_IDENTIFIER.fullmatch(override.helper)
            or not _CPP_QUALIFIED_IDENTIFIER.fullmatch(override.variant_factory)
            or not override.native_parameter_types
            or not clean_cpp_type(override.native_return_type)
            or not override.variants
        ):
            raise ValueError(
                f"configured method override {selector!r} has an incomplete policy"
            )
        element_placeholders = sum(
            parameter_type.count("{element}")
            for parameter_type in override.native_parameter_types
        )
        if (
            element_placeholders != 1
            or any(
                "{" in parameter_type.replace("{element}", "")
                or "}" in parameter_type.replace("{element}", "")
                for parameter_type in override.native_parameter_types
            )
        ):
            raise ValueError(
                f"configured method override {selector!r} must declare exactly one "
                "{element} native parameter placeholder"
            )

        lua_names: set[str] = set()
        cpp_types: set[str] = set()
        for variant in override.variants:
            cpp_type = clean_cpp_type(variant.cpp_type)
            if (
                not variant.lua_name
                or variant.lua_name == override.lua_name
                or variant.lua_name in lua_names
                or not cpp_type
                or cpp_type in cpp_types
                or not variant.lua_type
            ):
                raise ValueError(
                    f"configured method override {selector!r} has an invalid variant"
                )
            lua_names.add(variant.lua_name)
            cpp_types.add(cpp_type)


def get_output_reference_policy(
    qualified_function: str,
    parameter_types: tuple[str, ...] | list[str],
) -> OutputReferencePolicy | None:
    selector = (
        clean_cpp_type(qualified_function),
        tuple(clean_cpp_type(value) for value in parameter_types),
    )
    matches = [
        policy
        for policy in OUTPUT_REFERENCE_POLICIES
        if (
            clean_cpp_type(policy.qualified_function),
            tuple(clean_cpp_type(value) for value in policy.parameter_types),
        )
        == selector
    ]
    if len(matches) > 1:
        raise ValueError(
            f"ambiguous output-reference policy for {selector[0]}{selector[1]}"
        )
    return matches[0] if matches else None


def validate_output_reference_policy_registry() -> None:
    selectors: set[tuple[str, tuple[str, ...]]] = set()
    for policy in OUTPUT_REFERENCE_POLICIES:
        selector = (
            clean_cpp_type(policy.qualified_function),
            tuple(clean_cpp_type(value) for value in policy.parameter_types),
        )
        if not selector[0] or not selector[1] or selector in selectors:
            raise ValueError(
                f"duplicate or incomplete output-reference policy selector: {selector!r}"
            )
        selectors.add(selector)
        if not policy.outputs:
            raise ValueError(
                f"output-reference policy {selector!r} has no output parameters"
            )

        indices: set[int] = set()
        counted_arrays: set[int] = set()
        previous_index = -1
        for output in policy.outputs:
            output_type = selector[1][output.index] if 0 <= output.index < len(selector[1]) else ""
            if (
                output.index < 0
                or output.index >= len(selector[1])
                or output.index in indices
                or output.index <= previous_index
                or not output.expected_name
                or not is_reference(output_type)
                or is_const_type(output_type)
            ):
                raise ValueError(
                    f"output-reference policy {selector!r} has invalid output metadata"
                )
            if (
                output.count_for_array_parameter is not None
                and (
                    output.count_for_array_parameter < 0
                    or output.count_for_array_parameter >= len(selector[1])
                    or output.count_for_array_parameter >= output.index
                    or output.count_for_array_parameter in counted_arrays
                )
            ):
                raise ValueError(
                    f"output-reference policy {selector!r} has an invalid array-count relationship"
                )
            if output.count_for_array_parameter is not None:
                buffer_index = output.count_for_array_parameter
                buffer_type = selector[1][buffer_index]
                size_index = buffer_index + 1
                if (
                    not is_pointer(buffer_type)
                    or is_const_type(buffer_type)
                    or size_index >= len(selector[1])
                    or not is_size_type(cpp=selector[1][size_index])
                    or remove_cvref(output_type) not in INTEGER_TYPES
                ):
                    raise ValueError(
                        f"output-reference policy {selector!r} has an invalid array-count type relationship"
                    )
            indices.add(output.index)
            if output.count_for_array_parameter is not None:
                counted_arrays.add(output.count_for_array_parameter)
            previous_index = output.index


def skipped_class_binding_reason(qualified_name: str) -> str | None:
    return SKIPPED_CLASS_BINDINGS.get(clean_cpp_type(qualified_name))


def get_callback_codec(
    semantic_type: str,
    qualified_function: str = "",
    parameter_name: str = "",
    callable_signature: str = "",
) -> CallbackCodec | None:
    semantic_type = remove_cvref(clean_cpp_type(semantic_type))
    callable_signature = clean_cpp_type(callable_signature)

    semantic_matches: list[CallbackCodec] = []
    use_site_matches: list[CallbackCodec] = []
    for callback_codec in CALLBACK_CODEC_REGISTRY:
        selector = callback_codec.selector
        if selector.semantic_alias:
            if semantic_type != clean_cpp_type(selector.semantic_alias):
                continue
            if selector.qualified_function and qualified_function != selector.qualified_function:
                continue
            if selector.parameter_name and parameter_name != selector.parameter_name:
                continue
            if (
                selector.callable_signature
                and callable_signature != clean_cpp_type(selector.callable_signature)
            ):
                continue
            semantic_matches.append(callback_codec)
            continue

        if selector.qualified_function != qualified_function:
            continue
        if selector.parameter_name != parameter_name:
            continue
        if clean_cpp_type(selector.callable_signature) != callable_signature:
            continue
        use_site_matches.append(callback_codec)

    # Semantic aliases are matched by identity rather than by their expanded
    # signature.  If an exact use-site policy also selects the same parameter,
    # that is conflicting configuration and must not depend on registry order.
    matches = semantic_matches + use_site_matches
    if len(matches) > 1:
        names = ", ".join(repr(callback_codec.name) for callback_codec in matches)
        raise ValueError(
            f"ambiguous callback codec for {semantic_type or callable_signature!r} "
            f"at {qualified_function}.{parameter_name}: {names}"
        )
    return matches[0] if matches else None


def validate_callback_codec_registry() -> None:
    names: set[str] = set()
    selectors: set[tuple[str, str, str, str]] = set()
    allowed_directions = {"fromLua", "toLua"}
    allowed_access = {"read", "write", "readWrite"}

    for callback_codec in CALLBACK_CODEC_REGISTRY:
        if not callback_codec.name or callback_codec.name in names:
            raise ValueError(f"duplicate or empty callback codec name: {callback_codec.name!r}")
        names.add(callback_codec.name)

        selector = callback_codec.selector
        selector_key = (
            clean_cpp_type(selector.semantic_alias),
            selector.qualified_function,
            selector.parameter_name,
            clean_cpp_type(selector.callable_signature),
        )
        if not any(selector_key) or selector_key in selectors:
            raise ValueError(f"duplicate or empty callback codec selector: {selector_key!r}")
        selectors.add(selector_key)
        use_site_fields = (
            selector.qualified_function,
            selector.parameter_name,
            selector.callable_signature,
        )
        if selector.semantic_alias and any(use_site_fields):
            raise ValueError(
                f"callback codec {callback_codec.name!r} cannot combine a semantic alias "
                "with a function-parameter selector"
            )
        if not selector.semantic_alias and not all(use_site_fields):
            raise ValueError(
                f"callback codec {callback_codec.name!r} must select either a semantic alias "
                "or an exact function/parameter/signature use site"
            )

        if (
            not callback_codec.canonical_type.startswith("std::function<")
            or not callback_codec.native_callable
            or not callback_codec.codec
            or not callback_codec.lua_type
            or not callback_codec.lua_signature
        ):
            raise ValueError(f"callback codec {callback_codec.name!r} has an incomplete conversion policy")
        if not callback_codec.directions or len(set(callback_codec.directions)) != len(callback_codec.directions):
            raise ValueError(f"callback codec {callback_codec.name!r} has invalid directions")
        if not set(callback_codec.directions).issubset(allowed_directions):
            raise ValueError(f"callback codec {callback_codec.name!r} has unknown directions")

        parameter_names: set[str] = set()
        for parameter in callback_codec.parameters:
            if not parameter.name or parameter.name in parameter_names:
                raise ValueError(f"callback codec {callback_codec.name!r} has duplicate parameter names")
            parameter_names.add(parameter.name)
            if not parameter.role or parameter.access not in allowed_access:
                raise ValueError(f"callback codec {callback_codec.name!r} has invalid parameter metadata")

        return_names: set[str] = set()
        for result in callback_codec.returns:
            if not result.name or result.name in return_names or not result.role:
                raise ValueError(f"callback codec {callback_codec.name!r} has invalid return metadata")
            return_names.add(result.name)


def callback_codec_manifest() -> dict[str, Any]:
    validate_callback_codec_registry()
    codecs: list[dict[str, Any]] = []
    for callback_codec in CALLBACK_CODEC_REGISTRY:
        selector = callback_codec.selector
        selector_data: dict[str, Any] = {
            "kind": "functionParameter" if selector.qualified_function else "alias",
        }
        if selector.semantic_alias:
            selector_data["cppName"] = selector.semantic_alias
        if selector.qualified_function:
            selector_data["qualifiedFunction"] = selector.qualified_function
        if selector.parameter_name:
            selector_data["parameterName"] = selector.parameter_name
        if selector.callable_signature:
            selector_data["callableSignature"] = selector.callable_signature

        codecs.append({
            "name": callback_codec.name,
            "selector": selector_data,
            "canonicalType": callback_codec.canonical_type,
            "codec": callback_codec.codec,
            "luaType": callback_codec.lua_type,
            "luaSignature": callback_codec.lua_signature,
            "allowNil": callback_codec.allow_nil,
            "threadPolicy": callback_codec.thread_policy,
            "directions": list(callback_codec.directions),
            "parameters": [
                {
                    "name": parameter.name,
                    "role": parameter.role,
                    "access": parameter.access,
                    "nullable": parameter.nullable,
                    "unit": parameter.unit,
                }
                for parameter in callback_codec.parameters
            ],
            "returns": [
                {
                    "name": result.name,
                    "role": result.role,
                    "nullable": result.nullable,
                    "unit": result.unit,
                }
                for result in callback_codec.returns
            ],
            "clearSetterOnQuiesce": callback_codec.clear_setter_on_quiesce,
        })

    return {
        "schemaVersion": CALLBACK_CODEC_SCHEMA_VERSION,
        "callbacks": codecs,
    }


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
    *,
    via_open_from_memory: bool,
) -> str:
    """Render a long-lived memory constructor lambda using the configured strategy."""
    info = param_info_memory(params)
    if info is None:
        raise ValueError("long-lived memory constructor requires a byte pointer and size parameter")
    data_name, size_type = info
    lua_args = ", ".join(lua_params)

    if via_open_from_memory:
        tmpl = "ll_memory_ctor_via_open"
        kwargs = dict(data_name=data_name, owner_type=owner_type, size_type=size_type,
                      lua_path=lua_path_for_type(owner_type))
    else:
        tmpl = "ll_memory_ctor_direct"
        kwargs = dict(data_name=data_name, owner_type=owner_type, size_type=size_type)

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
