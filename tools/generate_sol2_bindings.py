from __future__ import annotations

import argparse
import copy
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    from .replace_model import (
        BINDING_TEMPLATES,
        BYTE_TYPES,
        CALLBACK_CODEC_REGISTRY,
        CallbackCodec,
        ConfiguredBinding,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LifecycleCategory,
        LUA_NAMESPACE_PROJECTIONS,
        LUA_KEYWORDS,
        NUMBER_TYPES,
        NUMERIC_ARRAY_TYPES,
        OPERATOR_META_FUNCTIONS,
        OUTPUT_ARRAY_COUNT_REF_NAMES,
        OUTPUT_REF_FUNCTIONS,
        OUTPUT_REF_NAMES,
        SHADER_UNIFORM_ARRAY_BINDINGS,
        SIZE_TYPE_NAMES,
        SPECIAL_POINTER_RETURNS,
        STRING_TYPES,
        TEMPLATE_PROFILES,
        TEMPLATE_SPECIALIZATION_OVERRIDES,
        TemplateProfile,
        TemplateSpecializationOverride,
        TYPE_DECL_KINDS,
        TypeRef,
        clean_cpp_type,
        get_callback_codec,
        get_lifecycle,
        is_anonymous_cpp_name,
        packet_io_info,
        param_info_memory,
        param_info_stream,
        render_ll_memory_ctor,
        render_ll_memory_open,
        render_ll_reset,
        render_ll_stream_ctor,
        render_ll_stream_open,
        render_shader_uniform_array,
        render_template,
        sanitize_identifier,
        set_public_type_aliases,
        walk_declarations,
    )
except ImportError:
    from replace_model import (
        BINDING_TEMPLATES,
        BYTE_TYPES,
        CALLBACK_CODEC_REGISTRY,
        CallbackCodec,
        ConfiguredBinding,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LifecycleCategory,
        LUA_NAMESPACE_PROJECTIONS,
        LUA_KEYWORDS,
        NUMBER_TYPES,
        NUMERIC_ARRAY_TYPES,
        OPERATOR_META_FUNCTIONS,
        OUTPUT_ARRAY_COUNT_REF_NAMES,
        OUTPUT_REF_FUNCTIONS,
        OUTPUT_REF_NAMES,
        SHADER_UNIFORM_ARRAY_BINDINGS,
        SIZE_TYPE_NAMES,
        SPECIAL_POINTER_RETURNS,
        STRING_TYPES,
        TEMPLATE_PROFILES,
        TEMPLATE_SPECIALIZATION_OVERRIDES,
        TemplateProfile,
        TemplateSpecializationOverride,
        TYPE_DECL_KINDS,
        TypeRef,
        clean_cpp_type,
        get_callback_codec,
        get_lifecycle,
        is_anonymous_cpp_name,
        packet_io_info,
        param_info_memory,
        param_info_stream,
        render_ll_memory_ctor,
        render_ll_memory_open,
        render_ll_reset,
        render_ll_stream_ctor,
        render_ll_stream_open,
        render_shader_uniform_array,
        render_template,
        sanitize_identifier,
        set_public_type_aliases,
        walk_declarations,
    )


@dataclass(frozen=True)
class StubParam:
    name: str
    lua_type: str


@dataclass(frozen=True)
class StubSignature:
    params: tuple[StubParam, ...]
    returns: tuple[str, ...] = ()


@dataclass
class OutputArray:
    buffer_name: str


@dataclass
class PlannedCall:
    lua_params: list[str] = field(default_factory=list)
    prelude: list[str] = field(default_factory=list)
    call_args: list[str] = field(default_factory=list)
    post_values: list[str] = field(default_factory=list)
    output_arrays: list[OutputArray] = field(default_factory=list)
    output_count_values: list[str] = field(default_factory=list)
    stub_param_types: dict[str, str] = field(default_factory=dict)
    signature_key: tuple[str, ...] = field(default_factory=tuple)
    unsupported: str | None = None


@dataclass
class TemplateSpecialization:
    cpp_type: str
    template_name: str
    args: tuple[str, ...]
    template_decl: dict[str, Any]
    profile: TemplateProfile
    override: TemplateSpecializationOverride | None
    lua_path: str
    primary_alias: str
    alias_paths: dict[str, str]


SPECIALIZATION_LUA_TYPES: dict[str, str] = {}


def sfml_include_for_file(file_item: dict[str, Any]) -> str:
    return "/".join(Path(file_item["path"]).parts[-3:])


def remove_cvref(value: str) -> str:
    value = clean_cpp_type(value)
    value = re.sub(r"\bconst\b|\bvolatile\b", "", value)
    value = value.replace("&", "").strip()
    return clean_cpp_type(value)


def remove_pointer(value: str) -> str:
    value = remove_cvref(value)
    if value.endswith("*"):
        value = value[:-1]
    value = re.sub(r"\bconst\b|\bvolatile\b", "", value).strip()
    return clean_cpp_type(value)


def is_const_type(value: str) -> bool:
    return bool(re.search(r"(^|[<,\s])const(\s|$)", value))


def is_pointer(value: str) -> bool:
    return remove_cvref(value).endswith("*")


def is_reference(value: str) -> bool:
    return "&" in clean_cpp_type(value)


def is_output_reference(value: str) -> bool:
    value = clean_cpp_type(value)
    return value.endswith("&") and not is_const_type(value)


def should_treat_as_output_reference(value: str, name: str, function_name: str) -> bool:
    if not is_output_reference(value):
        return False
    short_function_name = function_name.split("::")[-1]
    if name in {"received", "sent"}:
        return True
    return short_function_name in OUTPUT_REF_FUNCTIONS and name in OUTPUT_REF_NAMES


def is_sf_string(value: str) -> bool:
    value = remove_cvref(value)
    return value in {"sf::String", "String"} or value.endswith("::String")


def is_filesystem_path(value: str) -> bool:
    return "filesystem::path" in clean_cpp_type(value)


def is_string_view(value: str) -> bool:
    return "string_view" in clean_cpp_type(value)


def is_std_string(value: str) -> bool:
    return remove_cvref(value) in {"std::string", "string"}


def is_std_wstring(value: str) -> bool:
    return remove_cvref(value) in {"std::wstring", "wstring"}


def is_char_pointer(value: str) -> bool:
    element = remove_pointer(value)
    return element in {"char", "wchar_t"}


def is_wchar_pointer(value: str) -> bool:
    return remove_pointer(value) == "wchar_t"


def split_template_args(value: str) -> list[str]:
    value = value.strip()
    start = value.find("<")
    end = value.rfind(">")
    if start == -1 or end == -1 or end < start:
        return []
    body = value[start + 1 : end]
    args: list[str] = []
    current: list[str] = []
    depth = 0
    for ch in body:
        if ch == "<":
            depth += 1
            current.append(ch)
        elif ch == ">":
            depth -= 1
            current.append(ch)
        elif ch == "," and depth == 0:
            args.append(clean_cpp_type("".join(current)))
            current = []
        else:
            current.append(ch)
    if current:
        args.append(clean_cpp_type("".join(current)))
    return args


def is_template(value: str, name: str) -> bool:
    value = remove_cvref(value)
    return value.startswith(name + "<")


def vector_element(value: str) -> str | None:
    value = remove_cvref(value)
    if not is_template(value, "std::vector"):
        return None
    args = split_template_args(value)
    return args[0] if args else None


def optional_element(value: str) -> str | None:
    value = remove_cvref(value)
    if not is_template(value, "std::optional"):
        return None
    args = split_template_args(value)
    return args[0] if args else None


def std_function_signature(type_ref: TypeRef) -> str | None:
    for candidate in (type_ref.cpp, type_ref.source, type_ref.canonical_cpp):
        compact = clean_cpp_type(candidate)
        start = compact.find("std::function")
        if start == -1:
            continue
        lt_pos = compact.find("<", start)
        if lt_pos == -1:
            continue
        depth = 0
        for index in range(lt_pos, len(compact)):
            ch = compact[index]
            if ch == "<":
                depth += 1
            elif ch == ">":
                depth -= 1
                if depth == 0:
                    signature = compact[lt_pos + 1 : index].strip()
                    signature = re.sub(r"\s+", " ", signature)
                    signature = signature.replace(" *", "*").replace("* ", "*")
                    signature = signature.replace(" &", "&").replace("& ", "&")
                    signature = re.sub(r"\s+\(", "(", signature, count=1)
                    signature = signature.replace("( ", "(").replace(" )", ")")
                    signature = signature.replace(" ,", ",")
                    if "void*" in signature or "const void*" in signature:
                        signature = signature.replace("unsigned long long", "std::size_t")
                    if "sf::Text::ShapedGlyph" in signature:
                        signature = signature.replace("unsigned int&", "std::uint32_t&")
                    return signature
    return None


def is_std_function(type_ref: TypeRef) -> bool:
    return std_function_signature(type_ref) is not None


def resolve_callback_codec(
    type_ref: TypeRef,
    qualified_function: str = "",
    parameter_name: str = "",
) -> CallbackCodec | None:
    if not is_std_function(type_ref):
        return None

    return get_callback_codec(
        type_ref.cpp,
        qualified_function,
        parameter_name,
        std_function_signature(type_ref) or "",
    )


def callback_codec_for_alias(qualified_alias: str) -> CallbackCodec | None:
    qualified_alias = clean_cpp_type(qualified_alias)
    matches = [
        callback_codec
        for callback_codec in CALLBACK_CODEC_REGISTRY
        if clean_cpp_type(callback_codec.native_callable) == qualified_alias
    ]
    if len(matches) > 1:
        raise ValueError(f"ambiguous callback codec alias selector: {qualified_alias}")
    return matches[0] if matches else None


def canonical_std_function_type(type_ref: TypeRef) -> str:
    signature = std_function_signature(type_ref)
    return clean_cpp_type(f"std::function<{signature}>") if signature else ""


def validate_callback_codecs_against_api(api: dict[str, Any]) -> None:
    declarations = list(
        declaration
        for file_item in api.get("files", [])
        for declaration in walk_declarations(file_item.get("declarations", []))
    )
    declarations_by_name: dict[str, list[dict[str, Any]]] = {}
    for declaration in declarations:
        qualified_name = clean_cpp_type(declaration.get("qualified_name") or "")
        if qualified_name:
            declarations_by_name.setdefault(qualified_name, []).append(declaration)

    for callback_codec in CALLBACK_CODEC_REGISTRY:
        selector = callback_codec.selector
        expected_canonical = clean_cpp_type(callback_codec.canonical_type)

        native_callable = clean_cpp_type(callback_codec.native_callable)
        if native_callable.startswith("sf::"):
            native_items = [
                item
                for item in declarations_by_name.get(native_callable, [])
                if item.get("kind") in {"TYPE_ALIAS_DECL", "TYPEDEF_DECL"}
            ]
            if len(native_items) != 1:
                raise ValueError(
                    f"callback codec {callback_codec.name!r} expected exactly one NativeCallable alias "
                    f"{native_callable!r}, found {len(native_items)}"
                )
            native_canonical = canonical_std_function_type(TypeRef.from_json(native_items[0].get("type")))
            if native_canonical != expected_canonical:
                raise ValueError(
                    f"callback codec {callback_codec.name!r} NativeCallable canonical type mismatch for "
                    f"{native_callable}: expected {expected_canonical}, got {native_canonical}"
                )

        if selector.semantic_alias:
            alias_items = [
                item
                for item in declarations_by_name.get(clean_cpp_type(selector.semantic_alias), [])
                if item.get("kind") in {"TYPE_ALIAS_DECL", "TYPEDEF_DECL"}
            ]
            if len(alias_items) != 1:
                raise ValueError(
                    f"callback codec {callback_codec.name!r} expected exactly one extracted alias "
                    f"{selector.semantic_alias!r}, found {len(alias_items)}"
                )
            actual_canonical = canonical_std_function_type(TypeRef.from_json(alias_items[0].get("type")))
            if actual_canonical != expected_canonical:
                raise ValueError(
                    f"callback codec {callback_codec.name!r} canonical type mismatch for "
                    f"{selector.semantic_alias}: expected {expected_canonical}, got {actual_canonical}"
                )

        if selector.qualified_function:
            function_items = declarations_by_name.get(selector.qualified_function, [])
            matching_parameters: list[TypeRef] = []
            for function_item in function_items:
                for parameter in function_item.get("parameters", []):
                    if parameter.get("name") == selector.parameter_name:
                        type_ref = TypeRef.from_json(parameter.get("type"))
                        if (
                            not selector.callable_signature
                            or std_function_signature(type_ref) == clean_cpp_type(selector.callable_signature)
                        ):
                            matching_parameters.append(type_ref)
            if len(matching_parameters) != 1:
                raise ValueError(
                    f"callback codec {callback_codec.name!r} expected exactly one extracted parameter "
                    f"{selector.qualified_function}.{selector.parameter_name}, found {len(matching_parameters)}"
                )
            actual_canonical = canonical_std_function_type(matching_parameters[0])
            if actual_canonical != expected_canonical:
                raise ValueError(
                    f"callback codec {callback_codec.name!r} canonical type mismatch for "
                    f"{selector.qualified_function}.{selector.parameter_name}: "
                    f"expected {expected_canonical}, got {actual_canonical}"
                )


def callback_from_object_expr(callback_codec: CallbackCodec, name: str, label: str) -> str:
    return (
        f"lua_sf::callback::from_object<{callback_codec.native_callable}, "
        f"{callback_codec.codec}>({name}, {cpp_string_literal(label)})"
    )


def std_function_lua_type(
    type_ref: TypeRef,
    qualified_function: str = "",
    parameter_name: str = "",
) -> str:
    signature = std_function_signature(type_ref)
    if not signature:
        return "fun(...): any"
    callback_codec = resolve_callback_codec(type_ref, qualified_function, parameter_name)
    if callback_codec:
        suffix = "|nil" if callback_codec.allow_nil else ""
        return callback_codec.lua_type + suffix

    match = re.fullmatch(r"(.+?)\((.*)\)", signature)
    if not match:
        return "fun(...): any"
    return_cpp, args_cpp = match.groups()
    args = split_cpp_arguments(args_cpp)
    lua_args = [
        f"arg{index}: {callback_argument_lua_type(arg)}"
        for index, arg in enumerate(args, start=1)
        if arg and arg != "void"
    ]
    lua_return = type_ref_to_lua_type(TypeRef(spelling=return_cpp, canonical=return_cpp))
    suffix = "" if remove_cvref(return_cpp) == "void" else f": {lua_return}"
    return f"fun({', '.join(lua_args)}){suffix}"


def callback_argument_lua_type(cpp_type: str) -> str:
    if "*" in cpp_type:
        return "any"
    return type_ref_to_lua_type(TypeRef(spelling=cpp_type, canonical=cpp_type))


def split_cpp_arguments(value: str) -> list[str]:
    if not value.strip():
        return []
    args: list[str] = []
    current: list[str] = []
    depths = {"<": 0, "(": 0, "[": 0}
    closing = {">": "<", ")": "(", "]": "["}
    for ch in value:
        if ch in depths:
            depths[ch] += 1
        elif ch in closing:
            opener = closing[ch]
            depths[opener] = max(0, depths[opener] - 1)
        if ch == "," and not any(depths.values()):
            args.append(clean_cpp_type("".join(current)))
            current.clear()
        else:
            current.append(ch)
    if current:
        args.append(clean_cpp_type("".join(current)))
    return args


def is_window_handle(type_ref: TypeRef) -> bool:
    source = remove_cvref(type_ref.source)
    return source in {"WindowHandle", "sf::WindowHandle"}


def normalize_array_element(value: str) -> str:
    value = remove_pointer(value)
    replacements = {
        "unsigned char": "std::uint8_t",
        "uint8_t": "std::uint8_t",
        "short": "std::int16_t",
        "std::int16_t": "std::int16_t",
        "std::byte": "std::byte",
        "void": "std::byte",
    }
    return replacements.get(value, value)


def is_size_type(type_ref: TypeRef) -> bool:
    return remove_cvref(type_ref.cpp) in SIZE_TYPE_NAMES


def can_be_array_pointer(type_ref: TypeRef, next_param: dict[str, Any] | None) -> bool:
    cpp = type_ref.cpp
    if not is_pointer(cpp):
        return False
    if is_char_pointer(cpp):
        return False
    element = normalize_array_element(cpp)
    if element in NUMERIC_ARRAY_TYPES or element in BYTE_TYPES:
        return True
    return next_param is not None and is_size_type(TypeRef.from_json(next_param.get("type")))


def should_skip_type(type_ref: TypeRef, is_return: bool = False) -> str | None:
    cpp = clean_cpp_type(type_ref.cpp)
    compact = cpp.replace(" ", "")
    if any(ignored.replace(" ", "") in compact for ignored in IGNORE_PARAM_TYPES):
        return cpp
    if is_return and any(ignored in cpp for ignored in IGNORE_RETURN_TYPES):
        return cpp
    if "std::_" in cpp or "iterator" in cpp.lower():
        return cpp
    if is_return and is_std_function(type_ref):
        return cpp
    if "type-parameter-" in cpp:
        return cpp
    return None


def lua_param_type(type_ref: TypeRef) -> str:
    cpp = type_ref.cpp
    base = remove_cvref(cpp)
    if is_window_handle(type_ref):
        return "const lua_sf::WindowHandle&"
    if base in INTEGER_TYPES:
        return f"lua_sf::LuaIntegral<{base}>"
    if is_std_function(type_ref):
        return "sol::object"
    if is_sf_string(cpp) or is_filesystem_path(cpp) or is_string_view(cpp) or is_std_string(cpp) or is_std_wstring(cpp):
        return "std::string"
    if vector_element(cpp):
        return "sol::table"
    if optional_element(cpp):
        return "sol::object"
    return cpp


def from_lua_expr(
    type_ref: TypeRef,
    name: str,
    qualified_function: str = "",
    parameter_name: str = "",
) -> tuple[list[str], str]:
    cpp = type_ref.cpp
    base = remove_cvref(cpp)
    if is_window_handle(type_ref):
        return [], f"{name}.native()"
    if base in INTEGER_TYPES:
        return [], f"{name}.value()"
    signature = std_function_signature(type_ref)
    if signature:
        callback_codec = resolve_callback_codec(type_ref, qualified_function, parameter_name)
        if callback_codec:
            label = f"{qualified_function}.{parameter_name}".strip(".")
            return [], callback_from_object_expr(callback_codec, name, label)
        return [], f"lua_sf::function_from_object<{signature}>({name})"
    if is_sf_string(cpp):
        return [], f"lua_sf::to_sf_string({name})"
    if is_std_wstring(cpp):
        return [], f"lua_sf::to_sf_string({name}).toWideString()"
    if is_filesystem_path(cpp):
        return [], f"std::filesystem::path({name})"
    if is_string_view(cpp):
        return [], name
    elem = vector_element(cpp)
    if elem:
        storage = f"{name}_vector"
        return [f"auto {storage} = lua_sf::array_from_object<{elem}>({name});"], storage
    opt = optional_element(cpp)
    if opt:
        storage = f"{name}_optional"
        return [f"auto {storage} = lua_sf::optional_from_object<{opt}>({name});"], storage
    return [], name


def return_needs_wrapper(type_ref: TypeRef) -> bool:
    cpp = type_ref.cpp
    return bool(
        is_window_handle(type_ref)
        or is_sf_string(cpp)
        or is_std_string(cpp)
        or is_std_wstring(cpp)
        or is_string_view(cpp)
        or is_reference(cpp)
        or vector_element(cpp)
        or optional_element(cpp)
        or is_filesystem_path(cpp)
        or is_char_pointer(cpp)
    )


def return_wrapper_uses_lua(type_ref: TypeRef) -> bool:
    return optional_element(type_ref.cpp) is not None or vector_element(type_ref.cpp) is not None


def return_expr(type_ref: TypeRef, expr: str, indent: str, function_name: str | None = None) -> list[str]:
    cpp = type_ref.cpp
    if function_name and function_name in SPECIAL_POINTER_RETURNS:
        elem, count_expr = SPECIAL_POINTER_RETURNS[function_name]
        return [
            f"{indent}const auto* result = {expr};",
            f"{indent}if (!result)",
            f"{indent}    return sol::as_table(std::vector<{elem}>{{}});",
            f"{indent}std::vector<{elem}> result_values(result, result + static_cast<std::size_t>({count_expr}));",
            f"{indent}return sol::as_table(std::move(result_values));",
        ]
    if is_window_handle(type_ref):
        return [f"{indent}return lua_sf::WindowHandle::fromNative({expr});"]
    if is_sf_string(cpp):
        return [f"{indent}return lua_sf::to_utf8_string({expr});"]
    if is_std_wstring(cpp):
        return [f"{indent}return lua_sf::to_utf8_string(sf::String({expr}));"]
    if is_std_string(cpp) or is_string_view(cpp):
        return [f"{indent}return std::string({expr});"]
    if is_filesystem_path(cpp):
        return [f"{indent}return ({expr}).string();"]
    if vector_element(cpp):
        return [f"{indent}return lua_sf::vector_to_object(lua, {expr});"]
    if optional_element(cpp):
        return [f"{indent}return lua_sf::optional_to_object(lua, {expr});"]
    if is_char_pointer(cpp):
        return [
            f"{indent}const auto* result = {expr};",
            f"{indent}return result ? std::string(result) : std::string{{}};",
        ]
    if is_reference(cpp):
        wrapper = "std::cref" if is_const_type(cpp) else "std::ref"
        return [f"{indent}return {wrapper}({expr});"]
    return [f"{indent}return {expr};"]


def result_value_expr(type_ref: TypeRef, expr: str, function_name: str | None = None) -> tuple[list[str], str]:
    cpp = type_ref.cpp
    if is_window_handle(type_ref):
        return [], f"lua_sf::WindowHandle::fromNative({expr})"
    if is_sf_string(cpp):
        return [], f"lua_sf::to_utf8_string({expr})"
    if is_std_wstring(cpp):
        return [], f"lua_sf::to_utf8_string(sf::String({expr}))"
    if is_std_string(cpp) or is_string_view(cpp):
        return [], f"std::string({expr})"
    if is_filesystem_path(cpp):
        return [], f"({expr}).string()"
    if vector_element(cpp):
        return [], f"lua_sf::vector_to_object(lua, {expr})"
    if optional_element(cpp):
        return [f"auto result = {expr};"], "lua_sf::optional_to_object(lua, result)"
    if is_char_pointer(cpp):
        return [
            f"const auto* result_ptr = {expr};",
            "auto result = result_ptr ? std::string(result_ptr) : std::string{};",
        ], "result"
    if is_reference(cpp):
        wrapper = "std::cref" if is_const_type(cpp) else "std::ref"
        return [], f"{wrapper}({expr})"
    return [f"auto result = {expr};"], "result"


def is_integer_count_type(value: str) -> bool:
    return remove_cvref(value) in INTEGER_TYPES


def optional_integer_count_element(value: str) -> str | None:
    element = optional_element(value)
    if element and is_integer_count_type(element):
        return element
    return None


def output_array_resize_lines(plan: PlannedCall, return_type: TypeRef) -> list[str]:
    if not plan.output_arrays:
        return []

    if is_integer_count_type(return_type.cpp):
        return output_array_resize_lines_for_count(plan, "result")

    if optional_integer_count_element(return_type.cpp):
        lines = ["if (result)", "{"]
        lines.extend(f"    {line}" for line in output_array_resize_lines_for_count(plan, "*result"))
        lines.append("}")
        lines.append("else")
        lines.append("{")
        for output in plan.output_arrays:
            lines.append(f"    {output.buffer_name}.clear();")
        lines.append("}")
        return lines

    if plan.output_count_values:
        return output_array_resize_lines_for_count(plan, plan.output_count_values[0])

    return []


def output_array_resize_lines_for_count(plan: PlannedCall, count_expr: str) -> list[str]:
    lines: list[str] = []
    for output in plan.output_arrays:
        count_name = f"{output.buffer_name}_written"
        lines.append(f"const auto {count_name} = static_cast<std::size_t>({count_expr});")
        lines.append(f"if ({count_name} < {output.buffer_name}.size())")
        lines.append(f"    {output.buffer_name}.resize({count_name});")
    return lines


def qualify_relative_type(value: str, owner_full_name: str) -> str:
    value = clean_cpp_type(value)
    base = remove_cvref(value)
    if "::" in base or base.startswith("std::") or not owner_full_name or "::" not in owner_full_name:
        return value
    namespace = owner_full_name.rsplit("::", 1)[0]
    return value.replace(base, f"{namespace}::{base}", 1)


def lua_name_for_type(qualified_name: str) -> str:
    if qualified_name.startswith("sf::"):
        qualified_name = qualified_name[len("sf::") :]
    return qualified_name.replace("::", "_")


def lua_path_for_type(qualified_name: str) -> str:
    qualified_name = clean_cpp_type(qualified_name)
    configured = SPECIALIZATION_LUA_TYPES.get(remove_cvref(qualified_name))
    if configured:
        return configured
    if qualified_name.startswith("sf::"):
        qualified_name = qualified_name[len("sf::") :]
        return "sf." + qualified_name.replace("::", ".")
    return qualified_name.replace("::", ".")


def lua_table_expression(qualified_name: str) -> str:
    path = lua_path_for_type(qualified_name)
    return "lua" + "".join(f"[{cpp_string_literal(part)}]" for part in path.split(".")) + ".get<sol::table>()"


def lua_leaf_for_type(qualified_name: str) -> str:
    qualified_name = clean_cpp_type(qualified_name)
    return qualified_name.rsplit("::", 1)[-1]


def sanitize_lua_identifier(value: str) -> str:
    value = sanitize_identifier(value)
    if value in LUA_KEYWORDS:
        return f"{value}_"
    return value


def cpp_type_to_lua_type(value: str) -> str:
    value = clean_cpp_type(value)
    base = remove_cvref(value)
    if base.endswith("*") and not is_char_pointer(base):
        base = remove_pointer(base)

    if not base or base == "void":
        return "nil"
    if is_template(base, "lua_sf::LuaIntegral"):
        return "integer"
    if base in {"bool"}:
        return "boolean"
    if base in INTEGER_TYPES:
        return "integer"
    if base in NUMBER_TYPES:
        return "number"
    if base in STRING_TYPES or base == "sf::String":
        return "string"
    if base in {"sol::object", "sol::variadic_args"}:
        return "any"
    if base == "sol::table":
        return "table"
    if base in {"lua_State*", "lua_State"}:
        return "any"
    if base == "lua_sf::WindowHandle" or is_window_handle(TypeRef(spelling=base, canonical=base)):
        return "sf.WindowHandle"
    if base in SPECIALIZATION_LUA_TYPES:
        return SPECIALIZATION_LUA_TYPES[base]

    type_ref = TypeRef(spelling=value, canonical=value)
    if std_function_signature(type_ref):
        return std_function_lua_type(type_ref)

    vec_elem = vector_element(base)
    if vec_elem:
        return f"{cpp_type_to_lua_type(vec_elem)}[]"

    opt_elem = optional_element(base)
    if opt_elem:
        return f"{cpp_type_to_lua_type(opt_elem)}|nil"

    if base.startswith("sf::"):
        return lua_path_for_type(base)
    return "any"


def special_pointer_return_lua_type(function_name: str | None) -> str | None:
    if not function_name or function_name not in SPECIAL_POINTER_RETURNS:
        return None

    element, _count_expr = SPECIAL_POINTER_RETURNS[function_name]
    return f"{cpp_type_to_lua_type(element)}[]"


def type_ref_to_lua_type(type_ref: TypeRef) -> str:
    return cpp_type_to_lua_type(type_ref.cpp or type_ref.source)


def split_planned_lua_param(value: str) -> StubParam:
    type_text, _, name = value.strip().rpartition(" ")
    if not type_text:
        type_text = "any"
    return StubParam(sanitize_lua_identifier(name), cpp_type_to_lua_type(type_text))


def stub_signature_for_item(
    item: dict[str, Any],
    call_name: str,
    owner_type: str | None = None,
    constructor_return: str | None = None,
) -> StubSignature | None:
    params = item.get("parameters", [])
    plan = plan_parameters(params, item.get("qualified_name") or call_name)
    if plan.unsupported:
        return None

    stub_params_list: list[StubParam] = []
    for param in plan.lua_params:
        stub_param = split_planned_lua_param(param)
        override = plan.stub_param_types.get(stub_param.name)
        if override:
            stub_param = StubParam(stub_param.name, override)
        stub_params_list.append(stub_param)
    stub_params = tuple(stub_params_list)
    if constructor_return:
        return StubSignature(stub_params, (constructor_return,))

    returns: list[str] = []
    return_type = TypeRef.from_json(item.get("return_type"))
    special_pointer_lua_type = special_pointer_return_lua_type(item.get("qualified_name"))
    if special_pointer_lua_type:
        returns.append(special_pointer_lua_type)
    elif return_type.cpp and remove_cvref(return_type.cpp) != "void":
        returns.append(type_ref_to_lua_type(return_type))
    returns.extend("any" for _ in plan.post_values)
    return StubSignature(stub_params, tuple(returns))


def format_overload(signature: StubSignature) -> str:
    params = ", ".join(f"{param.name}: {param.lua_type}" for param in signature.params)
    if signature.returns:
        return f"fun({params}): {', '.join(signature.returns)}"
    return f"fun({params})"


def append_stub_function(
    lines: list[str],
    owner: str,
    name: str,
    signatures: list[StubSignature],
    method: bool = False,
) -> None:
    unique: list[StubSignature] = []
    seen: set[StubSignature] = set()
    for signature in signatures:
        if signature not in seen:
            unique.append(signature)
            seen.add(signature)
    if not unique:
        return

    first = unique[0]
    for signature in unique[1:]:
        lines.append(f"---@overload {format_overload(signature)}")
    for param in first.params:
        lines.append(f"---@param {param.name} {param.lua_type}")
    for return_type in first.returns:
        lines.append(f"---@return {return_type}")
    separator = ":" if method else "."
    params = ", ".join(param.name for param in first.params)
    lines.append(f"function {owner}{separator}{name}({params}) end")


def stub_fun_type(signature: StubSignature, self_type: str | None = None) -> str:
    params = list(signature.params)
    if self_type:
        params.insert(0, StubParam("self", self_type))
    return format_overload(StubSignature(tuple(params), signature.returns))


def cpp_string_literal(value: str) -> str:
    return json.dumps(value)


def stub_doc_lines(item: dict[str, Any] | None) -> list[str]:
    """Return the stub-writer call that emits an extracted API docstring."""
    if not item:
        return []
    doc = item.get("doc")
    if not isinstance(doc, str) or not doc.strip():
        return []
    return [f"    LUASF_STUB_DOC({cpp_string_literal(doc)});"]


def first_stub_doc(items: list[str | None]) -> str | None:
    return next((doc for doc in items if doc), None)


def stub_owner_for_table_var(table_var: str) -> str:
    if table_var == "sf":
        return "sf"
    if table_var.startswith("table_"):
        return table_var[len("table_") :].replace("__", ".")
    if table_var.startswith("sf_"):
        return "sf." + table_var[len("sf_") :].replace("_", ".")
    return table_var.replace("_", ".")


def type_strings_from_item(item: dict[str, Any]) -> list[str]:
    values: list[str] = []

    for base in item.get("base_classes", []):
        if base.get("name"):
            values.append(base["name"])

    for key in ("type", "return_type"):
        type_info = item.get(key)
        if type_info:
            values.extend([type_info.get("spelling", ""), type_info.get("canonical", "")])

    for param in item.get("parameters", []):
        type_info = param.get("type") or {}
        values.extend([type_info.get("spelling", ""), type_info.get("canonical", "")])

    return [clean_cpp_type(value) for value in values if value]


def contains_type(type_text: str, type_name: str) -> bool:
    if not type_text or not type_name:
        return False

    if "<" in type_name:
        return type_name.replace(" ", "") in type_text.replace(" ", "")

    pattern = rf"(?<![A-Za-z0-9_:]){re.escape(type_name)}(?![A-Za-z0-9_:])"
    return re.search(pattern, type_text) is not None


def collect_enum_constants(enum_item: dict[str, Any]) -> list[dict[str, Any]]:
    constants = []
    for child in enum_item.get("children", []):
        if child.get("kind") == "ENUM_CONSTANT_DECL":
            constants.append(child)
    return constants


def direct_children(item: dict[str, Any], kind: str) -> list[dict[str, Any]]:
    return [child for child in item.get("children", []) if child.get("kind") == kind]


def constructor_param_sets(params: list[dict[str, Any]]) -> list[list[dict[str, Any]]]:
    if not params:
        return [[]]
    first_default = None
    for index, param in enumerate(params):
        if param.get("default") is not None:
            first_default = index
            break
    if first_default is None:
        return [params]
    if any(param.get("default") is None for param in params[first_default:]):
        return [params]
    return [params[:count] for count in range(first_default, len(params) + 1)]


def planned_lua_param_type(lua_param: str) -> str:
    type_text, _, _name = lua_param.strip().rpartition(" ")
    return type_text or "any"


def overload_specificity_key(plan: PlannedCall, original_index: int) -> tuple[int, int, int, int]:
    param_types = [planned_lua_param_type(param) for param in plan.lua_params]
    object_params = sum(1 for type_text in param_types if type_text in {"sol::object", "sol::variadic_args"})
    table_params = sum(1 for type_text in param_types if type_text == "sol::table")
    return (object_params, table_params, -len(param_types), original_index)


def plan_parameters(params: list[dict[str, Any]], function_name: str = "") -> PlannedCall:
    plan = PlannedCall()
    skip_next: set[int] = set()

    for index, param in enumerate(params):
        if index in skip_next:
            continue

        name = sanitize_identifier(param.get("name") or f"arg{index}")
        type_ref = TypeRef.from_json(param.get("type"))
        unsupported = should_skip_type(type_ref)
        if unsupported:
            plan.unsupported = f"unsupported parameter type {unsupported}"
            return plan

        cpp = type_ref.cpp
        next_param = params[index + 1] if index + 1 < len(params) else None

        if should_treat_as_output_reference(cpp, name, function_name):
            local_type = remove_cvref(cpp)
            plan.prelude.append(f"{local_type} {name}{{}};")
            plan.call_args.append(name)
            plan.post_values.append(f"std::move({name})")
            if name in OUTPUT_ARRAY_COUNT_REF_NAMES and local_type in INTEGER_TYPES:
                plan.output_count_values.append(name)
            continue

        if is_pointer(cpp) and can_be_array_pointer(type_ref, next_param):
            element = normalize_array_element(cpp)
            has_size_pair = next_param is not None and is_size_type(TypeRef.from_json(next_param.get("type")))
            is_const_pointer = is_const_type(cpp)
            if is_const_pointer:
                plan.lua_params.append(f"sol::object {name}")
                plan.prelude.append(f"auto {name}_buffer = lua_sf::array_from_object<{element}>({name});")
                plan.call_args.append(f"{name}_buffer.data()")
                plan.signature_key += (f"inarray:{element}",)
                if has_size_pair:
                    size_type = TypeRef.from_json(next_param.get("type")).cpp
                    plan.call_args.append(f"static_cast<{size_type}>({name}_buffer.size())")
                    skip_next.add(index + 1)
                continue

            lua_size_name = sanitize_identifier(next_param.get("name") or "") if has_size_pair and next_param else ""
            if not lua_size_name:
                lua_size_name = f"{name}Count"
            plan.lua_params.append(f"std::size_t {lua_size_name}")
            plan.prelude.append(f"std::vector<{element}> {name}_buffer({lua_size_name});")
            plan.call_args.append(f"{name}_buffer.data()")
            plan.post_values.append(f"sol::as_table({name}_buffer)")
            plan.output_arrays.append(OutputArray(f"{name}_buffer"))
            plan.signature_key += (f"outarray:{element}",)
            if has_size_pair:
                size_type = TypeRef.from_json(next_param.get("type")).cpp
                plan.call_args.append(f"static_cast<{size_type}>({name}_buffer.size())")
                skip_next.add(index + 1)
            continue

        if is_char_pointer(cpp):
            plan.lua_params.append(f"std::string {name}")
            if is_wchar_pointer(cpp):
                plan.prelude.append(f"const std::wstring {name}_wide = lua_sf::to_sf_string({name}).toWideString();")
                plan.call_args.append(f"{name}_wide.c_str()")
            else:
                plan.call_args.append(f"{name}.c_str()")
            plan.signature_key += ("std::string",)
            continue

        lua_type = lua_param_type(type_ref)
        parameter_name = param.get("name") or f"arg{index}"
        prelude, expr = from_lua_expr(type_ref, name, function_name, parameter_name)
        plan.lua_params.append(f"{lua_type} {name}")
        plan.prelude.extend(prelude)
        plan.call_args.append(expr)
        callback_codec = resolve_callback_codec(type_ref, function_name, parameter_name)
        canonical_stub_type = cpp_type_to_lua_type(type_ref.canonical_cpp)
        if callback_codec:
            plan.stub_param_types[sanitize_lua_identifier(name)] = std_function_lua_type(
                type_ref,
                function_name,
                parameter_name,
            )
        elif canonical_stub_type != "any":
            plan.stub_param_types[sanitize_lua_identifier(name)] = canonical_stub_type
        plan.signature_key += (lua_type,)

    return plan


def make_lambda(
    item: dict[str, Any],
    owner_type: str | None,
    call_name: str,
    is_constructor: bool = False,
    value_constructor: bool = False,
) -> tuple[str | None, str | None]:
    params = item.get("parameters", [])
    plan = plan_parameters(params, item.get("qualified_name") or call_name)
    if plan.unsupported:
        return None, plan.unsupported

    return_type = TypeRef.from_json(item.get("return_type"))
    if not is_constructor:
        unsupported_return = should_skip_type(return_type, is_return=True)
        if unsupported_return:
            return None, f"unsupported return type {unsupported_return}"

    lua_args = ", ".join(plan.lua_params)
    if owner_type and not item.get("static", False) and not is_constructor:
        lua_args = f"{owner_type}& self" + (f", {lua_args}" if lua_args else "")

    capture = "[lua]" if return_wrapper_uses_lua(return_type) else "[]"
    lines: list[str] = []

    owner_clean = clean_cpp_type(owner_type or "")
    lifecycle = get_lifecycle(owner_clean) if owner_clean else None
    is_memory_lifecycle = lifecycle is not None and lifecycle.category in (LifecycleCategory.MEMORY, LifecycleCategory.BOTH)
    is_stream_lifecycle = lifecycle is not None and lifecycle.category in (LifecycleCategory.STREAM, LifecycleCategory.BOTH)

    if is_constructor:
        if is_memory_lifecycle and param_info_memory(params):
            return render_ll_memory_ctor(owner_clean, params, plan.lua_params), None
        if is_stream_lifecycle and param_info_stream(params):
            return render_ll_stream_ctor(owner_clean, params), None
        lines.append(f"{capture}({lua_args}) {{")
        lines.extend(f"    {line}" for line in plan.prelude)
        if value_constructor:
            arguments = ", ".join(plan.call_args)
            lines.append(f"    return {owner_type}{{{arguments}}};")
        elif is_memory_lifecycle:
            lines.append(
                f"    return lua_sf::wrapLuaSharedObject("
                f"lua_sf::makeLongLivedMemoryObject<{owner_type}>({', '.join(plan.call_args)}));"
            )
        else:
            lines.append(
                f"    return lua_sf::makeLuaSharedObject<{owner_type}>({', '.join(plan.call_args)});"
            )
        lines.append("}")
        return "\n".join(lines), None

    dispatch_type = item.get("dispatch_type")
    call_owner = dispatch_type or owner_type
    if item.get("static", False) and call_owner:
        call_target = f"{call_owner}::{call_name}"
    elif dispatch_type:
        call_target = f"static_cast<{dispatch_type}&>(self).{call_name}"
    else:
        call_target = f"self.{call_name}"
    if owner_type is None:
        call_target = call_name
    call_expr = f"{call_target}({', '.join(plan.call_args)})"

    callback_codec = None
    if len(params) == 1:
        callback_codec = resolve_callback_codec(
            TypeRef.from_json(params[0].get("type")),
            item.get("qualified_name") or call_name,
            params[0].get("name") or "callback",
        )
    if callback_codec and callback_codec.clear_setter_on_quiesce:
        callback_name = sanitize_identifier(params[0].get("name") or "callback")
        callback_label = (
            f"{item.get('qualified_name') or call_name}."
            f"{params[0].get('name') or 'callback'}"
        )
        native_callback_expr = callback_from_object_expr(
            callback_codec,
            callback_name,
            callback_label,
        )
        return (
            "\n".join(
                [
                    f"[]({lua_param_type(TypeRef.from_json(params[0].get('type')))} {callback_name}) {{",
                    "    static const unsigned char callbackOwner{};",
                    f"    lua_State *state = {callback_name}.lua_state();",
                    f"    if (lua_sf::is_nil_object({callback_name})) {{",
                    f"        {call_target}({{}});",
                    "        lua_sf::detail::unregisterStateQuiesceCallback(state, &callbackOwner);",
                    "        return;",
                    "    }",
                    f"    auto nativeCallback = {native_callback_expr};",
                    "    lua_sf::detail::registerStateQuiesceCallback(",
                    "        state, &callbackOwner,",
                    f"        []() noexcept {{ {call_target}({{}}); }});",
                    f"    {call_target}(std::move(nativeCallback));",
                    "}",
                ]
            ),
            None,
        )

    if (
        owner_type
        and is_memory_lifecycle
        and param_info_memory(params)
        and call_name == "openFromMemory"
    ):
        return render_ll_memory_open(owner_clean, params, plan.lua_params, call_target), None

    if (
        owner_type
        and is_stream_lifecycle
        and param_info_stream(params)
        and call_name == "openFromStream"
    ):
        return render_ll_stream_open(owner_clean, params, call_target), None

    if (
        owner_type
        and lifecycle is not None
        and call_name in lifecycle.reset_methods
    ):
        lambda_code = render_ll_reset(owner_clean, plan.lua_params, plan.prelude, plan.post_values, call_expr, return_type.cpp)
        if lambda_code:
            return lambda_code, None

    if plan.post_values:
        trailing_return = ""
    elif item.get("qualified_name") in SPECIAL_POINTER_RETURNS:
        trailing_return = ""
    elif vector_element(return_type.cpp):
        trailing_return = " -> sol::object"
    elif return_type.cpp and return_type.cpp != "void" and not plan.post_values and not return_needs_wrapper(return_type):
        trailing_return = f" -> {return_type.cpp}"
    elif is_window_handle(return_type):
        trailing_return = " -> lua_sf::WindowHandle"
    elif (
        is_sf_string(return_type.cpp)
        or is_std_string(return_type.cpp)
        or is_std_wstring(return_type.cpp)
        or is_string_view(return_type.cpp)
        or is_filesystem_path(return_type.cpp)
        or is_char_pointer(return_type.cpp)
    ):
        trailing_return = " -> std::string"
    elif optional_element(return_type.cpp):
        trailing_return = " -> sol::object"
    else:
        trailing_return = ""

    lines.append(f"{capture}({lua_args}){trailing_return} {{")
    lines.extend(f"    {line}" for line in plan.prelude)

    if return_type.cpp == "void" or not return_type.cpp:
        lines.append(f"    {call_expr};")
        lines.extend(f"    {line}" for line in output_array_resize_lines(plan, return_type))
        if plan.post_values:
            lines.append(f"    return std::make_tuple({', '.join(plan.post_values)});")
    elif plan.post_values:
        result_lines, result_expr = result_value_expr(return_type, call_expr, item.get("qualified_name"))
        lines.extend(f"    {line}" for line in result_lines)
        lines.extend(f"    {line}" for line in output_array_resize_lines(plan, return_type))
        values = [result_expr, *plan.post_values]
        lines.append(f"    return std::make_tuple({', '.join(values)});")
    else:
        lines.extend(return_expr(return_type, call_expr, "    ", item.get("qualified_name")))

    lines.append("}")
    return "\n".join(lines), None


def overload_block(
    name: str,
    lambdas: list[str],
    indent: str,
    target: str = "set_function",
    self_dependency: bool = False,
) -> list[str]:
    if not lambdas:
        return []
    if len(lambdas) == 1:
        lines = [f'{indent}{target}("{name}",']
        if self_dependency:
            lines.append(f"{indent}    sol::policies(")
            append_indented_block(lines, lambdas[0], indent + "        ", ",")
            lines.append(f"{indent}        sol::self_dependency{{}}")
            lines.append(f"{indent}    )")
        else:
            append_indented_block(lines, lambdas[0], indent + "    ")
        lines.append(f"{indent});")
        return lines
    lines = [f'{indent}{target}("{name}",']
    if self_dependency:
        lines.append(f"{indent}    sol::policies(")
        lines.append(f"{indent}        sol::overload(")
        lambda_indent = indent + "            "
    else:
        lines.append(f"{indent}    sol::overload(")
        lambda_indent = indent + "        "
    for index, lambda_code in enumerate(lambdas):
        suffix = "," if index + 1 < len(lambdas) else ""
        append_indented_block(lines, lambda_code, lambda_indent, suffix)
    if self_dependency:
        lines.append(f"{indent}        ),")
        lines.append(f"{indent}        sol::self_dependency{{}}")
        lines.append(f"{indent}    )")
    else:
        lines.append(f"{indent}    )")
    lines.append(f"{indent});")
    return lines


def indent_block(text: str, indent: str) -> str:
    return "\n".join(indent + line if line else line for line in text.splitlines())


def append_indented_block(lines: list[str], text: str, indent: str, suffix: str = "") -> None:
    block = text.splitlines()
    if not block:
        if suffix:
            lines.append(indent + suffix)
        return
    for index, line in enumerate(block):
        line_suffix = suffix if index + 1 == len(block) else ""
        lines.append(f"{indent}{line}{line_suffix}")


def shader_uniform_array_block(var_name: str, lua_owner: str) -> list[str]:
    lines: list[str] = []

    for index, binding in enumerate(SHADER_UNIFORM_ARRAY_BINDINGS):
        macro = "LUASF_STUB_FUNCTION" if index == 0 else "LUASF_STUB_OVERLOAD"
        lines.append(
            f'    {macro}({cpp_string_literal(lua_owner)}, "setUniformArray", '
            f'{cpp_string_literal(f"fun(self: {lua_owner}, name: string, values: {binding["lua"]})")});'
        )

    for binding in SHADER_UNIFORM_ARRAY_BINDINGS:
        lines.append(
            f'    LUASF_STUB_FUNCTION({cpp_string_literal(lua_owner)}, '
            f'{cpp_string_literal(binding["method"])}, '
            f'{cpp_string_literal(f"fun(self: {lua_owner}, name: string, values: {binding["lua"]})")});'
        )

    for binding in SHADER_UNIFORM_ARRAY_BINDINGS:
        lines.append(f'    auto {binding["local"]} =')
        append_indented_block(
            lines,
            render_shader_uniform_array(binding["cpp"], "values"),
            "        ",
            ";",
        )

    for binding in SHADER_UNIFORM_ARRAY_BINDINGS:
        lines.append(f'    {var_name}.set_function("{binding["method"]}", {binding["local"]});')

    capture_list = ", ".join(binding["local"] for binding in SHADER_UNIFORM_ARRAY_BINDINGS)
    lines.append(f'    {var_name}.set_function("setUniformArray",')
    lines.append(f"        [{capture_list}](sf::Shader& self, std::string name, sol::object values) {{")
    lines.append("            if (values.get_type() != sol::type::table)")
    lines.append('                throw std::runtime_error("sf.Shader.setUniformArray expects a Lua array");')
    lines.append("            sol::table table = values.as<sol::table>();")
    lines.append("            sol::object first = table[1];")
    lines.append("            if (lua_sf::is_nil_object(first))")
    lines.append(
        '                throw std::runtime_error("sf.Shader.setUniformArray cannot infer an empty array; '
        'use setUniformFloatArray, setUniformVec2Array, setUniformVec3Array, setUniformVec4Array, '
        'setUniformMat3Array, or setUniformMat4Array");'
    )
    for binding in SHADER_UNIFORM_ARRAY_BINDINGS:
        lines.append(f'            if ({binding["check"]})')
        lines.append("            {")
        lines.append(f'                {binding["local"]}(self, name, values);')
        lines.append("                return;")
        lines.append("            }")
    lines.append('            throw std::runtime_error("sf.Shader.setUniformArray received an unsupported array element type");')
    lines.append("        }")
    lines.append("    );")
    return lines


def meta_assignment_block(
    var_name: str,
    meta_function: str,
    lambdas: list[str],
    indent: str = "    ",
    self_dependency: bool = False,
) -> list[str]:
    if not lambdas:
        return []
    lines = [f"{indent}{var_name}[sol::meta_function::{meta_function}] ="]
    value_indent = indent + "    "
    if self_dependency:
        lines.append(f"{value_indent}sol::policies(")
        value_indent += "    "
    if len(lambdas) == 1:
        append_indented_block(
            lines,
            lambdas[0],
            value_indent,
            "," if self_dependency else "",
        )
    else:
        lines.append(f"{value_indent}sol::overload(")
        for index, lambda_code in enumerate(lambdas):
            suffix = "," if index + 1 < len(lambdas) else ""
            append_indented_block(lines, lambda_code, value_indent + "    ", suffix)
        lines.append(f"{value_indent})" + ("," if self_dependency else ""))
    if self_dependency:
        lines.append(f"{value_indent}sol::self_dependency{{}}")
        lines.append(f"{indent}    )")
    lines.append(f"{indent};")
    return lines


def packet_io_type_info(type_ref: TypeRef) -> dict[str, Any] | None:
    return packet_io_info(remove_cvref(type_ref.cpp)) or packet_io_info(remove_cvref(type_ref.source))


def is_single_output_reference_operator(method: dict[str, Any]) -> bool:
    params = method.get("parameters", [])
    if len(params) != 1:
        return False
    type_ref = TypeRef.from_json(params[0].get("type"))
    return is_output_reference(type_ref.cpp)


class Sol2Generator:
    def __init__(self, api: dict[str, Any], output_root: Path):
        self.api = api
        self.output_root = output_root
        self.include_root = output_root / "include"
        self.src_root = output_root / "src"
        self.skipped: list[str] = []
        set_public_type_aliases(api)
        validate_callback_codecs_against_api(api)
        self.template_map = self._build_template_map()
        self._validate_template_profiles()
        self.specializations, self.alias_specializations = self._build_template_specializations()
        self._validate_template_specializations()
        self.free_template_operators = self._build_free_template_operators()
        self.class_map = self._build_class_map()
        self.type_includes = self._build_type_includes()
        self.sorted_type_includes = sorted(self.type_includes.items(), key=lambda item: len(item[0]), reverse=True)

    @staticmethod
    def _template_root(value: str) -> str:
        value = remove_cvref(clean_cpp_type(value))
        return value.split("<", 1)[0]

    @staticmethod
    def _project_alias_path(qualified_name: str) -> str:
        qualified_name = clean_cpp_type(qualified_name)
        for cpp_namespace, lua_namespace in LUA_NAMESPACE_PROJECTIONS.items():
            prefix = cpp_namespace + "::"
            if qualified_name.startswith(prefix):
                suffix = qualified_name[len(prefix):].replace("::", ".")
                return f"{lua_namespace}.{suffix}" if suffix else lua_namespace
        if qualified_name.startswith("sf::"):
            return "sf." + qualified_name[len("sf::"):].replace("::", ".")
        return qualified_name.replace("::", ".")

    def _build_template_map(self) -> dict[str, dict[str, Any]]:
        templates: dict[str, dict[str, Any]] = {}
        for file_item in self.api.get("files", []):
            for item in walk_declarations(file_item.get("declarations", [])):
                if item.get("kind") != "CLASS_TEMPLATE":
                    continue
                qualified_name = clean_cpp_type(item.get("qualified_name") or "")
                if qualified_name:
                    templates.setdefault(qualified_name, item)
        return templates

    def _validate_template_profiles(self) -> None:
        for template_name, profile in TEMPLATE_PROFILES.items():
            if clean_cpp_type(profile.cpp_template) != clean_cpp_type(template_name):
                raise ValueError(
                    f"template profile key {template_name!r} does not match cpp_template "
                    f"{profile.cpp_template!r}"
                )
            if template_name not in self.template_map:
                raise ValueError(f"configured template profile {template_name!r} was not extracted")
            field_type_names = [name for name, _lua_type in profile.field_lua_types]
            if len(field_type_names) != len(set(field_type_names)):
                raise ValueError(f"template profile {template_name!r} configures a field Lua type more than once")
            for binding in profile.configured_bindings:
                self._validate_configured_binding(template_name, binding)

        for configured_type, override in TEMPLATE_SPECIALIZATION_OVERRIDES.items():
            if clean_cpp_type(override.cpp_type) != clean_cpp_type(configured_type):
                raise ValueError(
                    f"template specialization key {configured_type!r} does not match cpp_type "
                    f"{override.cpp_type!r}"
                )
            template_name = self._template_root(configured_type)
            if template_name not in TEMPLATE_PROFILES:
                raise ValueError(
                    f"template specialization override {configured_type!r} references unknown template "
                    f"{template_name!r}"
                )
            for binding in override.configured_bindings:
                self._validate_configured_binding(configured_type, binding)
            if len(override.aliases) != len(set(override.aliases)):
                raise ValueError(
                    f"template specialization override {configured_type!r} repeats an additional Lua alias"
                )

    @staticmethod
    def _validate_configured_binding(owner: str, binding: ConfiguredBinding) -> None:
        if binding.kind not in {"constructor", "member"}:
            raise ValueError(
                f"configured binding {binding.template!r} for {owner} has invalid kind {binding.kind!r}"
            )
        if binding.template not in BINDING_TEMPLATES:
            raise ValueError(
                f"configured binding for {owner} references missing code template {binding.template!r}"
            )
        value_names = [name for name, _value in binding.values]
        if len(value_names) != len(set(value_names)):
            raise ValueError(
                f"configured binding {binding.template!r} for {owner} repeats a template value"
            )

    def _build_template_specializations(
        self,
    ) -> tuple[dict[str, TemplateSpecialization], dict[str, TemplateSpecialization]]:
        aliases_by_target: dict[str, list[dict[str, Any]]] = {}
        for file_item in self.api.get("files", []):
            for item in walk_declarations(file_item.get("declarations", [])):
                if item.get("kind") not in {"TYPE_ALIAS_DECL", "TYPEDEF_DECL"}:
                    continue
                qualified_name = clean_cpp_type(item.get("qualified_name") or "")
                target = clean_cpp_type(TypeRef.from_json(item.get("type")).canonical_cpp)
                if qualified_name and target.startswith("sf::") and "<" in target:
                    aliases_by_target.setdefault(target, []).append(item)

        specializations: dict[str, TemplateSpecialization] = {}
        by_alias: dict[str, TemplateSpecialization] = {}
        SPECIALIZATION_LUA_TYPES.clear()
        for target, aliases in sorted(aliases_by_target.items()):
            template_name = self._template_root(target)
            template_decl = self.template_map.get(template_name)
            profile = TEMPLATE_PROFILES.get(template_name)
            if template_decl is None:
                raise ValueError(
                    f"public alias specializes unknown or unextracted template {template_name!r}: {target}"
                )
            if profile is None:
                raise ValueError(
                    f"public alias specializes unconfigured template {template_name!r}: {target}"
                )
            args = tuple(split_template_args(target))
            parameters = template_decl.get("template_parameters", [])
            if len(args) != len(parameters):
                raise ValueError(
                    f"template alias {target} has {len(args)} arguments, expected {len(parameters)} for {template_name}"
                )

            override = TEMPLATE_SPECIALIZATION_OVERRIDES.get(target)
            projected = {
                clean_cpp_type(item.get("qualified_name") or ""): self._project_alias_path(
                    item.get("qualified_name") or ""
                )
                for item in aliases
            }
            ranked_aliases = sorted(projected, key=lambda name: (name.count("::"), name))
            if not ranked_aliases:
                raise ValueError(f"template specialization {target} has no public aliases")
            shallowest_depth = ranked_aliases[0].count("::")
            shallowest_paths = {
                projected[name]
                for name in ranked_aliases
                if name.count("::") == shallowest_depth
            }
            if override and override.lua_path:
                lua_path = override.lua_path
            elif len(shallowest_paths) == 1:
                lua_path = next(iter(shallowest_paths))
            else:
                raise ValueError(
                    f"ambiguous Lua primary name for {target}: {sorted(shallowest_paths)}; add a specialization override"
                )
            matching_primary = [name for name in ranked_aliases if projected[name] == lua_path]
            primary_alias = matching_primary[0] if matching_primary else ranked_aliases[0]
            specialization = TemplateSpecialization(
                cpp_type=target,
                template_name=template_name,
                args=args,
                template_decl=template_decl,
                profile=profile,
                override=override,
                lua_path=lua_path,
                primary_alias=primary_alias,
                alias_paths=projected,
            )
            specializations[target] = specialization
            SPECIALIZATION_LUA_TYPES[target] = lua_path
            for alias_name in ranked_aliases:
                by_alias[alias_name] = specialization
                SPECIALIZATION_LUA_TYPES[alias_name] = lua_path

        missing_overrides = sorted(set(TEMPLATE_SPECIALIZATION_OVERRIDES) - set(specializations))
        if missing_overrides:
            raise ValueError(f"template specialization overrides have no extracted public alias: {missing_overrides}")
        return specializations, by_alias

    def _validate_template_specializations(self) -> None:
        lua_name_owners: dict[str, str] = {}
        public_types = {
            clean_cpp_type(item.get("qualified_name") or item.get("name") or "")
            for file_item in self.api.get("files", [])
            for item in walk_declarations(file_item.get("declarations", []))
            if item.get("kind") in TYPE_DECL_KINDS
        }

        for specialization in self.specializations.values():
            additional_aliases = set(
                specialization.override.aliases if specialization.override else ()
            )
            extracted_aliases = set(specialization.alias_paths.values())
            redundant_aliases = sorted(additional_aliases & extracted_aliases)
            if redundant_aliases:
                raise ValueError(
                    f"template specialization {specialization.cpp_type} configures aliases already extracted "
                    f"from C++: {redundant_aliases}"
                )
            primary_parent = specialization.lua_path.rpartition(".")[0]
            invalid_aliases = sorted(
                alias
                for alias in additional_aliases
                if not alias.rpartition(".")[0] or alias.rpartition(".")[0] != primary_parent
            )
            if invalid_aliases:
                raise ValueError(
                    f"template specialization {specialization.cpp_type} has additional aliases outside "
                    f"{primary_parent!r}: {invalid_aliases}"
                )
            for lua_path in {
                specialization.lua_path,
                *specialization.alias_paths.values(),
                *additional_aliases,
            }:
                previous = lua_name_owners.setdefault(lua_path, specialization.cpp_type)
                if previous != specialization.cpp_type:
                    raise ValueError(
                        f"duplicate Lua template specialization name {lua_path!r}: "
                        f"{previous} and {specialization.cpp_type}"
                    )

            profile = specialization.profile
            declaration = specialization.template_decl
            children = declaration.get("children", [])
            fields = {
                child.get("name")
                for child in children
                if child.get("kind") == "FIELD_DECL" and child.get("name")
            }
            configured_fields = set(profile.replaced_fields) | {
                name for name, _lua_type in profile.field_lua_types
            }
            missing_fields = sorted(configured_fields - fields)
            if missing_fields:
                raise ValueError(
                    f"template profile {profile.cpp_template} references unknown fields {missing_fields}"
                )

            override = specialization.override
            if override is None:
                continue
            methods = {
                child.get("name")
                for child in children
                if child.get("kind") == "CXX_METHOD" and child.get("name")
            }
            missing_members = sorted(set(override.disabled_members) - methods)
            if missing_members:
                raise ValueError(
                    f"template specialization {specialization.cpp_type} disables unknown members "
                    f"{missing_members}"
                )
            constructor_keys = {
                ",".join(param.get("name", "") for param in child.get("parameters", []))
                for child in children
                if child.get("kind") == "CONSTRUCTOR"
            }
            missing_constructors = sorted(set(override.disabled_constructors) - constructor_keys)
            if missing_constructors:
                raise ValueError(
                    f"template specialization {specialization.cpp_type} disables unknown constructors "
                    f"{missing_constructors}"
                )
            missing_dependencies = sorted(set(override.dependencies) - public_types)
            if missing_dependencies:
                raise ValueError(
                    f"template specialization {specialization.cpp_type} references unknown dependencies "
                    f"{missing_dependencies}"
                )

    def _build_free_template_operators(self) -> set[tuple[str, str, int]]:
        operators: set[tuple[str, str, int]] = set()
        for file_item in self.api.get("files", []):
            for item in walk_declarations(file_item.get("declarations", [])):
                if item.get("kind") != "FUNCTION_TEMPLATE" or not item.get("name", "").startswith("operator"):
                    continue
                params = item.get("parameters", [])
                semantic_types = [TypeRef.from_json(param.get("type")).cpp for param in params]
                for template_name in TEMPLATE_PROFILES:
                    if any(template_name + "<" in type_text for type_text in semantic_types):
                        operators.add((template_name, item.get("name", ""), len(params)))
        return operators

    def _build_class_map(self) -> dict[str, dict[str, Any]]:
        classes: dict[str, dict[str, Any]] = {}
        for file_item in self.api.get("files", []):
            for item in walk_declarations(file_item.get("declarations", [])):
                if item.get("kind") not in {"CLASS_DECL", "STRUCT_DECL"}:
                    continue
                qualified_name = item.get("qualified_name") or item.get("name")
                if not qualified_name or is_anonymous_cpp_name(qualified_name):
                    continue
                classes.setdefault(clean_cpp_type(qualified_name), item)
        return classes

    def _build_type_includes(self) -> dict[str, str]:
        includes: dict[str, str] = {}
        for file_item in self.api.get("files", []):
            original_include = sfml_include_for_file(file_item)
            for item in walk_declarations(file_item.get("declarations", [])):
                if item.get("kind") not in TYPE_DECL_KINDS:
                    continue
                qualified_name = item.get("qualified_name") or item.get("name")
                if not qualified_name or is_anonymous_cpp_name(qualified_name):
                    continue
                includes.setdefault(clean_cpp_type(qualified_name), original_include)
        return includes

    def generate(self) -> None:
        self.include_root.mkdir(parents=True, exist_ok=True)
        self.src_root.mkdir(parents=True, exist_ok=True)
        self._remove_legacy_generated_utils()
        self._clean_previous_bindings()

        for file_item in self.api.get("files", []):
            self._write_binding_file(file_item)

    def _remove_legacy_generated_utils(self) -> None:
        for path in (self.include_root / "sfml_lua_utils.hpp", self.src_root / "sfml_lua_utils.cpp"):
            if path.exists():
                path.unlink()

    def _clean_previous_bindings(self) -> None:
        for root, suffix in ((self.include_root, ".hpp"), (self.src_root, ".cpp")):
            if not root.exists():
                continue
            for path in root.rglob(f"bind_*{suffix}"):
                path.unlink()

    def _write_binding_file(self, file_item: dict[str, Any]) -> None:
        sfml_path = Path(file_item["path"])
        module = file_item["module"]
        stem = sfml_path.stem
        include_dir = self.include_root / module
        src_dir = self.src_root / module
        include_dir.mkdir(parents=True, exist_ok=True)
        src_dir.mkdir(parents=True, exist_ok=True)

        hpp_path = include_dir / f"bind_{stem}.hpp"
        cpp_path = src_dir / f"bind_{stem}.cpp"
        original_include = sfml_include_for_file(file_item)
        extra_includes = self._extra_includes_for_file(file_item, original_include)

        hpp_path.write_text(
            "\n".join(
                [
                    "#pragma once",
                    "",
                    f'#include <{original_include}>',
                    *[f"#include <{include}>" for include in extra_includes],
                    '#include "utils.hpp"',
                    "",
                    f"void bind_{stem}(sol::state_view lua);",
                    "",
                ]
            ),
            encoding="utf-8",
        )

        body_lines = self._emit_file_body(file_item, stem)
        cpp_lines = [
            f'#include "{module}/bind_{stem}.hpp"',
            "",
            "#include <algorithm>",
            "#include <memory>",
            "#include <sstream>",
            "#include <stdexcept>",
            "#include <string>",
            "#include <tuple>",
            "#include <utility>",
            "#include <vector>",
            "",
            f"void bind_{stem}(sol::state_view lua) {{",
            "    sol::table sf = lua_sf::sf_table(lua);",
            *body_lines,
            "}",
            "",
        ]
        cpp_path.write_text("\n".join(cpp_lines), encoding="utf-8")

    def _extra_includes_for_file(self, file_item: dict[str, Any], original_include: str) -> list[str]:
        includes: set[str] = set()
        for item in walk_declarations(file_item.get("declarations", [])):
            type_texts = type_strings_from_item(item)
            specialization = self.alias_specializations.get(
                clean_cpp_type(item.get("qualified_name") or "")
            )
            if specialization and specialization.override:
                for dependency in specialization.override.dependencies:
                    include = self.type_includes.get(clean_cpp_type(dependency))
                    if include is None:
                        raise ValueError(
                            f"no public header include was found for configured dependency {dependency!r}"
                        )
                    if include != original_include:
                        includes.add(include)
            if item.get("kind") in {"CLASS_DECL", "STRUCT_DECL"}:
                full_name = item.get("qualified_name") or item.get("name")
                if full_name:
                    for method, _dispatch_type in self._inherited_methods_for(item, clean_cpp_type(full_name)):
                        type_texts.extend(type_strings_from_item(method))

            for type_text in type_texts:
                for type_name, include in self.sorted_type_includes:
                    if include == original_include:
                        continue
                    if contains_type(type_text, type_name):
                        includes.add(include)

        return sorted(includes)

    def _emit_file_body(self, file_item: dict[str, Any], stem: str) -> list[str]:
        lines: list[str] = []
        for item in file_item.get("declarations", []):
            lines.extend(self._emit_item(item, "sf", namespace_prefix=""))
        if not lines:
            lines.append(f"    // No bindable public declarations were found in {stem}.")
        return lines

    def _emit_item(self, item: dict[str, Any], table_var: str, namespace_prefix: str) -> list[str]:
        kind = item.get("kind")
        if kind == "NAMESPACE":
            name = item.get("name", "")
            if name in IGNORED_NAMESPACES:
                return [f"    // Skipped namespace {name} by generator policy."]
            if name == "sf":
                return self._emit_children(item, table_var, "sf::")
            full_namespace = f"{namespace_prefix}{name}".rstrip(":")
            if full_namespace in LUA_NAMESPACE_PROJECTIONS:
                return self._emit_children(item, table_var, f"{full_namespace}::")
            child_table = f"{table_var}_{sanitize_identifier(name)}"
            lines = [f'    sol::table {child_table} = {table_var}["{name}"].get_or_create<sol::table>();']
            lines.extend(self._emit_children(item, child_table, f"{namespace_prefix}{name}::"))
            return lines
        if kind in {"CLASS_DECL", "STRUCT_DECL"}:
            return self._emit_class(item, table_var)
        if kind == "ENUM_DECL":
            return self._emit_enum(item, table_var)
        if kind == "FUNCTION_DECL":
            return self._emit_free_function(item, table_var, namespace_prefix)
        if kind == "VAR_DECL":
            return self._emit_var(item, table_var, namespace_prefix)
        if kind in {"TYPE_ALIAS_DECL", "TYPEDEF_DECL"}:
            owner_full = namespace_prefix.rstrip(":")
            owner_lua = lua_path_for_type(owner_full) if owner_full else stub_owner_for_table_var(table_var)
            return self._emit_type_alias(item, table_var, owner_full, owner_lua)
        return []

    def _emit_children(self, item: dict[str, Any], table_var: str, namespace_prefix: str) -> list[str]:
        lines: list[str] = []
        free_functions: list[dict[str, Any]] = []
        for child in item.get("children", []):
            if child.get("kind") == "FUNCTION_DECL":
                free_functions.append(child)
                continue
            lines.extend(self._emit_item(child, table_var, namespace_prefix))
        lines.extend(self._emit_free_functions(free_functions, table_var, namespace_prefix))
        return lines

    def _emit_enum(self, enum_item: dict[str, Any], table_var: str) -> list[str]:
        full_name = enum_item.get("qualified_name") or enum_item.get("name")
        if not full_name or full_name.startswith("sf::priv::"):
            return []
        constants = collect_enum_constants(enum_item)
        if not constants:
            return [f"    // Skipped enum {full_name}: no public constants found."]
        lua_name = lua_leaf_for_type(full_name)
        lua_path = lua_path_for_type(full_name)
        lines = [
            *stub_doc_lines(enum_item),
            f'    LUASF_STUB_CLASS({cpp_string_literal(lua_path)});',
        ]
        field_type = lua_path if enum_item.get("scoped") else "integer"
        for constant in constants:
            lines.extend(stub_doc_lines(constant))
            lines.append(
                f"    LUASF_STUB_FIELD({cpp_string_literal(constant['name'])}, "
                f"{cpp_string_literal(field_type)});"
            )
        lines.append(f'    {table_var}.new_enum("{lua_name}",')
        for index, constant in enumerate(constants):
            suffix = "," if index + 1 < len(constants) else ""
            lines.append(f'        "{constant["name"]}", {full_name}::{constant["name"]}{suffix}')
        lines.append("    );")
        return lines

    def _emit_class(self, cls: dict[str, Any], table_var: str) -> list[str]:
        full_name = cls.get("qualified_name") or cls.get("name")
        if (
            not full_name
            or full_name == "sf::String"
            or (
                full_name.startswith("sf::priv::")
                and not cls.get("_template_specialization")
            )
        ):
            return [f"    // Skipped class {full_name}: converted through lua_sf utilities."]

        lua_name = cls.get("_lua_name") or lua_leaf_for_type(full_name)
        lua_path = cls.get("_lua_path") or lua_path_for_type(full_name)
        var_name = f"type_{sanitize_identifier(full_name)}"
        direct_bases = self._direct_base_type_names(cls, full_name)
        bases = self._base_type_names_for_binding(cls, full_name)
        if bases:
            lines = [
                f'    auto {var_name} = {table_var}.new_usertype<{full_name}>("{lua_name}",',
                "        sol::no_constructor,",
                f"        sol::base_classes, sol::bases<{', '.join(bases)}>()",
                "    );",
            ]
        else:
            lines = [f'    auto {var_name} = {table_var}.new_usertype<{full_name}>("{lua_name}", sol::no_constructor);']

        nested_table_var = f"table_{sanitize_identifier(full_name)}"
        lines.append(f'    sol::table {nested_table_var} = {table_var}["{lua_name}"].get<sol::table>();')
        if not cls.get("_value_type"):
            lines.append(f"    lua_sf::mark_shared_usertype<{full_name}>(lua);")
        if direct_bases:
            native_bases_var = f"native_bases_{sanitize_identifier(full_name)}"
            lines.append(f"    sol::table {native_bases_var} = lua.create_table();")
            for base in direct_bases:
                lines.append(f"    {native_bases_var}.add({lua_table_expression(base)});")
            lines.append(f'    {nested_table_var}.raw_set("__nativeBases", {native_bases_var});')
        lines.extend(stub_doc_lines(cls))
        if bases:
            bases_lua = ", ".join(lua_path_for_type(base) for base in bases)
            lines.append(
                f'    LUASF_STUB_CLASS({cpp_string_literal(lua_path)}, {cpp_string_literal(bases_lua)});'
            )
        else:
            lines.append(f'    LUASF_STUB_CLASS({cpp_string_literal(lua_path)});')
        lines.extend(self._stub_field_lines(cls))
        lines.extend(self._emit_constructors(cls, var_name, full_name))
        lines.extend(self._emit_fields(cls, var_name, full_name))
        lines.extend(self._emit_methods(cls, var_name, full_name))
        lines.extend(self._emit_configured_members(cls, var_name, full_name, lua_path))
        lines.extend(self._emit_free_template_operators(cls, var_name, full_name, lua_path))

        for child in cls.get("children", []):
            if child.get("kind") == "ENUM_DECL":
                lines.extend(self._emit_enum(child, nested_table_var))
            elif child.get("kind") in {"CLASS_DECL", "STRUCT_DECL"}:
                lines.extend(self._emit_class(child, nested_table_var))
            elif child.get("kind") == "VAR_DECL" and child.get("access") in (None, "public"):
                lines.extend(self._emit_var(child, nested_table_var, f"{full_name}::"))
            elif child.get("kind") in {"TYPE_ALIAS_DECL", "TYPEDEF_DECL"}:
                lines.extend(self._emit_type_alias(child, nested_table_var, full_name, lua_path))
        return lines

    @staticmethod
    def _replace_template_text(value: str, parameters: list[dict[str, Any]], args: tuple[str, ...]) -> str:
        result = value
        for index, (parameter, argument) in enumerate(zip(parameters, args)):
            result = re.sub(rf"type-parameter-\d+-{index}(?!\d)", argument, result)
            name = parameter.get("name") or ""
            if name:
                result = re.sub(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", argument, result)
        return clean_cpp_type(result)

    def _instantiate_template(self, specialization: TemplateSpecialization) -> dict[str, Any]:
        clone = copy.deepcopy(specialization.template_decl)
        parameters = clone.get("template_parameters", [])

        def replace_types(value: Any) -> None:
            if isinstance(value, dict):
                for key, child in list(value.items()):
                    if key in {"spelling", "canonical", "qualified_name"} and isinstance(child, str):
                        value[key] = self._replace_template_text(child, parameters, specialization.args)
                    elif key == "name" and "access" in value and isinstance(child, str):
                        value[key] = self._replace_template_text(child, parameters, specialization.args)
                    else:
                        replace_types(child)
            elif isinstance(value, list):
                for child in value:
                    replace_types(child)

        replace_types(clone)
        override = specialization.override
        disabled_members = set(override.disabled_members if override else ())
        disabled_constructors = set(override.disabled_constructors if override else ())
        children: list[dict[str, Any]] = []
        for child in clone.get("children", []):
            kind = child.get("kind")
            if kind == "FUNCTION_TEMPLATE":
                continue
            if kind == "CXX_METHOD" and child.get("name") in disabled_members:
                continue
            if kind == "CONSTRUCTOR":
                parameter_key = ",".join(param.get("name", "") for param in child.get("parameters", []))
                if parameter_key in disabled_constructors:
                    continue
            children.append(child)
        clone["children"] = children
        clone["kind"] = "STRUCT_DECL"
        clone["qualified_name"] = specialization.cpp_type
        clone["name"] = specialization.cpp_type.rsplit("::", 1)[-1]
        clone["_lua_path"] = specialization.lua_path
        clone["_lua_name"] = specialization.lua_path.rsplit(".", 1)[-1]
        clone["_value_type"] = specialization.profile.value_type
        clone["_template_specialization"] = specialization
        clone.pop("template_parameters", None)
        unresolved: list[str] = []
        parameter_names = [parameter.get("name", "") for parameter in parameters if parameter.get("name")]

        def find_unresolved(value: Any) -> None:
            if isinstance(value, dict):
                for key, child in value.items():
                    if key in {"spelling", "canonical", "qualified_name"} and isinstance(child, str):
                        if re.search(r"type-parameter-\d+-\d+", child) or any(
                            re.search(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", child)
                            for name in parameter_names
                        ):
                            unresolved.append(child)
                    else:
                        find_unresolved(child)
            elif isinstance(value, list):
                for child in value:
                    find_unresolved(child)

        find_unresolved(clone)
        if unresolved:
            raise ValueError(
                f"template specialization {specialization.cpp_type} contains unreplaced parameters: "
                f"{sorted(set(unresolved))}"
            )
        return clone

    def _emit_specialization_alias(
        self,
        item: dict[str, Any],
        table_var: str,
        specialization: TemplateSpecialization,
    ) -> list[str]:
        qualified_name = clean_cpp_type(item.get("qualified_name") or "")
        alias_path = specialization.alias_paths[qualified_name]
        lines: list[str] = []
        if qualified_name == specialization.primary_alias:
            lines.extend(self._emit_class(self._instantiate_template(specialization), table_var))
            if specialization.override:
                owner = specialization.lua_path.rpartition(".")[0]
                for configured_alias in specialization.override.aliases:
                    alias_leaf = configured_alias.rsplit(".", 1)[-1]
                    lines.extend(
                        [
                            f'    LUASF_STUB_VALUE({cpp_string_literal(owner)}, '
                            f'{cpp_string_literal(alias_leaf)}, '
                            f'{cpp_string_literal(specialization.lua_path)});',
                            f'    {table_var}[{cpp_string_literal(alias_leaf)}] = '
                            f'{lua_table_expression(specialization.cpp_type)};',
                        ]
                    )
        if alias_path != specialization.lua_path:
            alias_leaf = alias_path.rsplit(".", 1)[-1]
            owner = stub_owner_for_table_var(table_var)
            lines.extend(
                [
                    f'    LUASF_STUB_VALUE({cpp_string_literal(owner)}, {cpp_string_literal(alias_leaf)}, '
                    f'{cpp_string_literal(specialization.lua_path)});',
                    f'    {table_var}[{cpp_string_literal(alias_leaf)}] = {lua_table_expression(specialization.cpp_type)};',
                ]
            )
        return lines

    @staticmethod
    def _configured_bindings(cls: dict[str, Any], kind: str) -> list[ConfiguredBinding]:
        specialization: TemplateSpecialization | None = cls.get("_template_specialization")
        if specialization is None:
            return []
        bindings = list(specialization.profile.configured_bindings)
        if specialization.override:
            bindings.extend(specialization.override.configured_bindings)
        return [binding for binding in bindings if binding.kind == kind]

    def _configured_context(
        self,
        cls: dict[str, Any],
        var_name: str,
        full_name: str,
        lua_path: str,
        binding: ConfiguredBinding,
    ) -> dict[str, str]:
        context = {
            "cpp_type": full_name,
            "lua_path": lua_path,
            "lua_leaf": lua_path.rsplit(".", 1)[-1],
            "var_name": var_name,
        }
        context.update(dict(binding.values))
        field_names = [name.strip() for name in context.get("fields", "").split(",") if name.strip()]
        if field_names:
            field_items = {
                child.get("name"): child
                for child in cls.get("children", [])
                if child.get("kind") == "FIELD_DECL"
            }
            missing = [name for name in field_names if name not in field_items]
            if missing:
                raise ValueError(f"configured fields {missing} do not exist on {full_name}")
            context["field_exprs"] = ", ".join(f"self.{name}" for name in field_names)
            context["field_lua_returns"] = ", ".join(
                type_ref_to_lua_type(TypeRef.from_json(field_items[name].get("type")))
                for name in field_names
            )
            context["stream_components"] = ' << ", " << '.join(f"self.{name}" for name in field_names)
        return context

    def _render_configured_binding(
        self,
        cls: dict[str, Any],
        var_name: str,
        full_name: str,
        lua_path: str,
        binding: ConfiguredBinding,
    ) -> tuple[str, dict[str, str]]:
        context = self._configured_context(cls, var_name, full_name, lua_path, binding)
        try:
            return render_template(binding.template, **context), context
        except KeyError as exc:
            raise ValueError(
                f"invalid configured binding template {binding.template!r} for {full_name}: {exc}"
            ) from exc

    def _emit_configured_members(
        self,
        cls: dict[str, Any],
        var_name: str,
        full_name: str,
        lua_path: str,
    ) -> list[str]:
        lines: list[str] = []
        for binding in self._configured_bindings(cls, "member"):
            rendered, _context = self._render_configured_binding(
                cls, var_name, full_name, lua_path, binding
            )
            lines.extend(f"    {line}" if line else "" for line in rendered.splitlines())
        return lines

    def _emit_free_template_operators(
        self,
        cls: dict[str, Any],
        var_name: str,
        full_name: str,
        lua_path: str,
    ) -> list[str]:
        specialization: TemplateSpecialization | None = cls.get("_template_specialization")
        if specialization is None:
            return []
        allowed = specialization.profile.allowed_operators
        if specialization.override and specialization.override.allowed_operators is not None:
            allowed = specialization.override.allowed_operators
        if not allowed:
            return []

        scalar_cpp = specialization.args[0] if specialization.args else ""
        scalar_ref = TypeRef(spelling=scalar_cpp, canonical=scalar_cpp)
        scalar_param = lua_param_type(scalar_ref) if scalar_cpp else "any"
        scalar_prelude, scalar_expr = from_lua_expr(scalar_ref, "scalar") if scalar_cpp else ([], "scalar")
        if scalar_prelude:
            raise ValueError(f"operator scalar conversion for {full_name} unexpectedly requires a prelude")
        scalar_lua = cpp_type_to_lua_type(scalar_cpp) if scalar_cpp else "any"

        lines: list[str] = []
        for operator_key in allowed:
            name, _, shape = operator_key.partition(":")
            arity = 1 if shape == "unary" else 2
            if (specialization.template_name, name, arity) not in self.free_template_operators:
                raise ValueError(
                    f"configured operator {operator_key} for {full_name} was not found in the extracted API"
                )
            if name == "operator-" and shape == "unary":
                lines.append(
                    f"    {var_name}[sol::meta_function::unary_minus] = "
                    f"[](const {full_name}& value) {{ return -value; }};"
                )
                lines.append(f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_path)}, "unm: {lua_path}");')
            elif name in {"operator+", "operator-", "operator=="}:
                symbol = {"operator+": "+", "operator-": "-", "operator==": "=="}[name]
                meta = {
                    "operator+": "addition",
                    "operator-": "subtraction",
                    "operator==": "equal_to",
                }[name]
                lines.append(
                    f"    {var_name}[sol::meta_function::{meta}] = "
                    f"[](const {full_name}& left, const {full_name}& right) {{ return left {symbol} right; }};"
                )
                result_type = "boolean" if name == "operator==" else lua_path
                annotation = "eq" if name == "operator==" else ("add" if name == "operator+" else "sub")
                lines.append(
                    f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_path)}, '
                    f'{cpp_string_literal(f"{annotation}({lua_path}): {result_type}")});'
                )
            elif name == "operator*":
                lines.append(f"    {var_name}[sol::meta_function::multiplication] = sol::overload(")
                lines.append(
                    f"        []({full_name} value, {scalar_param} scalar) {{ return value * {scalar_expr}; }},"
                )
                left_expr = scalar_expr.replace("scalar", "scalar")
                lines.append(
                    f"        []({scalar_param} scalar, {full_name} value) {{ return {left_expr} * value; }}"
                )
                lines.append("    );")
                lines.append(
                    f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_path)}, '
                    f'{cpp_string_literal(f"mul({scalar_lua}): {lua_path}")});'
                )
            elif name == "operator/":
                lines.append(
                    f"    {var_name}[sol::meta_function::division] = "
                    f"[]({full_name} value, {scalar_param} scalar) {{ return value / {scalar_expr}; }};"
                )
                lines.append(
                    f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_path)}, '
                    f'{cpp_string_literal(f"div({scalar_lua}): {lua_path}")});'
                )
            else:
                raise ValueError(f"unsupported configured free operator {operator_key} for {full_name}")
        return lines

    def _emit_type_alias(
        self,
        item: dict[str, Any],
        table_var: str,
        owner_full_name: str,
        owner_lua_path: str,
    ) -> list[str]:
        name = item.get("name", "")
        if not name or name in IGNORE_NAMES:
            return []
        qualified_name = clean_cpp_type(item.get("qualified_name") or "")
        specialization = self.alias_specializations.get(qualified_name)
        if specialization is not None:
            return self._emit_specialization_alias(item, table_var, specialization)
        type_ref = TypeRef.from_json(item.get("type"))
        target_cpp = clean_cpp_type(type_ref.canonical_cpp or type_ref.cpp or type_ref.source)
        if not target_cpp:
            return []
        if is_std_function(type_ref):
            alias_lua = f"{owner_lua_path}.{name}"
            callback_codec = callback_codec_for_alias(qualified_name)
            alias_signature = (
                callback_codec.lua_signature
                if callback_codec
                else std_function_lua_type(type_ref)
            )
            return [
                *stub_doc_lines(item),
                f'    LUASF_STUB_ALIAS({cpp_string_literal(alias_lua)}, '
                f'{cpp_string_literal(alias_signature)});',
            ]
        if not target_cpp.startswith("sf::"):
            if "::" in target_cpp:
                target_cpp = f"sf::{target_cpp}"
            else:
                target_cpp = f"{owner_full_name}::{target_cpp}"
        target_lua = lua_path_for_type(target_cpp)
        alias_lua = f"{owner_lua_path}.{name}"
        target_leaf = lua_leaf_for_type(target_cpp)
        return [
            *stub_doc_lines(item),
            f'    LUASF_STUB_ALIAS({cpp_string_literal(alias_lua)}, {cpp_string_literal(target_lua)});',
            "    {",
            f'        const sol::object aliasValue = {table_var}.raw_get<sol::object>("{name}");',
            f'        const sol::object aliasTarget = {table_var}.raw_get<sol::object>("{target_leaf}");',
            "        if ((!aliasValue.valid() || aliasValue.get_type() == sol::type::lua_nil) &&",
            "            aliasTarget.valid() && aliasTarget.get_type() != sol::type::lua_nil)",
            f'            {table_var}.raw_set("{name}", aliasTarget);',
            "    }",
        ]

    def _stub_field_lines(self, cls: dict[str, Any]) -> list[str]:
        lines: list[str] = []
        specialization: TemplateSpecialization | None = cls.get("_template_specialization")
        field_type_overrides = (
            dict(specialization.profile.field_lua_types) if specialization else {}
        )
        for field_item in cls.get("children", []):
            if field_item.get("kind") != "FIELD_DECL" or field_item.get("access") not in (None, "public"):
                continue
            field_name = field_item.get("name")
            type_ref = TypeRef.from_json(field_item.get("type"))
            if not field_name:
                continue
            field_lua_type = field_type_overrides.get(field_name)
            if field_lua_type is None and should_skip_type(type_ref):
                continue
            lines.extend(stub_doc_lines(field_item))
            lines.append(
                f'    LUASF_STUB_FIELD({cpp_string_literal(field_name)}, '
                f'{cpp_string_literal(field_lua_type or type_ref_to_lua_type(type_ref))});'
            )
        return lines

    def _direct_base_type_names(self, cls: dict[str, Any], full_name: str) -> list[str]:
        bases: list[str] = []
        for base in cls.get("base_classes", []):
            if base.get("access") not in (None, "public") or not base.get("name"):
                continue
            bases.append(clean_cpp_type(qualify_relative_type(base["name"], full_name)))
        return bases

    def _base_type_names_for_binding(
        self,
        cls: dict[str, Any],
        full_name: str,
        seen: set[str] | None = None,
    ) -> list[str]:
        seen = seen or set()
        bases: list[str] = []
        for base in cls.get("base_classes", []):
            if base.get("access") not in (None, "public") or not base.get("name"):
                continue
            base_name = clean_cpp_type(qualify_relative_type(base["name"], full_name))
            if base_name in seen:
                continue
            seen.add(base_name)
            bases.append(base_name)
            base_cls = self.class_map.get(base_name)
            if base_cls is not None:
                bases.extend(self._base_type_names_for_binding(base_cls, base_name, seen))
        return bases

    def _base_classes_for(self, cls: dict[str, Any], full_name: str) -> list[tuple[str, dict[str, Any]]]:
        bases: list[tuple[str, dict[str, Any]]] = []
        for base in cls.get("base_classes", []):
            if base.get("access") not in (None, "public") or not base.get("name"):
                continue
            base_name = clean_cpp_type(qualify_relative_type(base["name"], full_name))
            base_cls = self.class_map.get(base_name)
            if base_cls is not None:
                bases.append((base_name, base_cls))
        return bases

    def _inherited_methods_for(
        self,
        cls: dict[str, Any],
        full_name: str,
        seen: set[str] | None = None,
    ) -> list[tuple[dict[str, Any], str]]:
        seen = seen or set()
        methods: list[tuple[dict[str, Any], str]] = []
        for base_name, base_cls in self._base_classes_for(cls, full_name):
            if base_name in seen:
                continue
            seen.add(base_name)
            methods.extend(self._inherited_methods_for(base_cls, base_name, seen))
            for child in base_cls.get("children", []):
                if child.get("kind") == "CXX_METHOD":
                    methods.append((child, base_name))
        return methods

    def _emit_constructors(self, cls: dict[str, Any], var_name: str, full_name: str) -> list[str]:
        if cls.get("abstract"):
            return [f"    // {full_name} is abstract; constructor binding is omitted."]

        overloads: list[tuple[tuple[int, int, int, int], str, StubSignature, str | None]] = []
        lua_owner = lua_path_for_type(full_name)
        seen: set[tuple[str, ...]] = set()
        overload_index = 0
        for ctor in cls.get("children", []):
            if ctor.get("kind") != "CONSTRUCTOR" or ctor.get("deleted"):
                continue
            if ctor.get("copy_constructor") or ctor.get("move_constructor"):
                continue
            if ctor.get("access") not in (None, "public"):
                continue
            for params in constructor_param_sets(ctor.get("parameters", [])):
                ctor_item = dict(ctor)
                ctor_item["parameters"] = params
                planned = plan_parameters(params, ctor.get("qualified_name") or full_name)
                if planned.unsupported:
                    self.skipped.append(f"{full_name}::{ctor.get('displayname')}: {planned.unsupported}")
                    continue
                if planned.signature_key in seen:
                    continue
                seen.add(planned.signature_key)
                lambda_code, reason = make_lambda(
                    ctor_item,
                    full_name,
                    full_name,
                    is_constructor=True,
                    value_constructor=bool(cls.get("_value_type")),
                )
                if reason:
                    self.skipped.append(f"{full_name}::{ctor.get('displayname')}: {reason}")
                    continue
                if lambda_code:
                    stub_signature = stub_signature_for_item(ctor_item, "new", constructor_return=lua_owner)
                    if stub_signature:
                        overloads.append((
                            overload_specificity_key(planned, overload_index),
                            lambda_code,
                            stub_signature,
                            ctor.get("doc"),
                        ))
                        overload_index += 1

        configured_constructors: list[tuple[str, str]] = []
        for binding in self._configured_bindings(cls, "constructor"):
            rendered, context = self._render_configured_binding(
                cls, var_name, full_name, lua_owner, binding
            )
            if not binding.stub_signature:
                raise ValueError(f"configured constructor {binding.template} for {full_name} has no stub signature")
            try:
                stub_type = binding.stub_signature.format(**context)
            except KeyError as exc:
                raise ValueError(
                    f"invalid configured constructor signature for {full_name}: {exc}"
                ) from exc
            configured_constructors.append((rendered, stub_type))

        if not overloads:
            # Synthesize a default constructor for aggregate types
            # (types with public fields but no user-declared constructors).
            if self._has_default_constructible_aggregate(cls):
                if cls.get("_value_type"):
                    default_lambda = f"[]() {{\n    return {full_name}{{}};\n}}"
                else:
                    default_lambda = f"[]() {{\n    return lua_sf::makeLuaSharedObject<{full_name}>();\n}}"
                default_stub = StubSignature((), (lua_owner,))
                overloads.append(((0, 0, 0, overload_index), default_lambda, default_stub, None))
            elif self._has_implicit_default_constructor(cls):
                if cls.get("_value_type"):
                    default_lambda = f"[]() {{\n    return {full_name}{{}};\n}}"
                else:
                    default_lambda = f"[]() {{\n    return lua_sf::makeLuaSharedObject<{full_name}>();\n}}"
                default_stub = StubSignature((), (lua_owner,))
                overloads.append(((0, 0, 0, overload_index), default_lambda, default_stub, None))
            elif not configured_constructors:
                return []
        overloads.sort(key=lambda item: item[0])
        # Emit stub annotations for all overloads, longest first.
        sorted_stub_types = [stub_fun_type(item[2]) for item in overloads]
        sorted_stub_types.extend(stub_type for _lambda, stub_type in configured_constructors)
        stub_lines = stub_doc_lines({"doc": first_stub_doc([item[3] for item in overloads])})
        for i, function_type in enumerate(sorted_stub_types):
            macro = "LUASF_STUB_FUNCTION" if i == 0 else "LUASF_STUB_OVERLOAD"
            stub_lines.append(
                f'    {macro}({cpp_string_literal(lua_owner)}, "new", '
                f'{cpp_string_literal(function_type)});'
            )

        lambdas = [item[1] for item in overloads]
        lambdas.extend(lambda_code for lambda_code, _stub_type in configured_constructors)
        if len(lambdas) == 1:
            lines = stub_lines
            lines.append(f'    {var_name}.set_function("new", sol::factories(')
            append_indented_block(lines, lambdas[0], "        ")
            lines.append("    ));")
            return lines

        lines = stub_lines
        lines.append(f'    {var_name}.set_function("new", sol::factories(')
        for index, lambda_code in enumerate(lambdas):
            suffix = "," if index + 1 < len(lambdas) else ""
            append_indented_block(lines, lambda_code, "        ", suffix)
        lines.append("    ));")
        return lines

    @staticmethod
    def _has_default_constructible_aggregate(cls: dict[str, Any]) -> bool:
        """True when the type has public fields and no const/readonly members that would
        prevent default construction (i.e. it's a C++ aggregate without deleted default ctor)."""
        has_field = False
        for child in cls.get("children", []):
            if child.get("kind") != "FIELD_DECL" or child.get("access") not in (None, "public"):
                continue
            if child.get("readonly"):
                return False  # const member → no default ctor
            has_field = True
        return has_field

    @staticmethod
    def _has_implicit_default_constructor(cls: dict[str, Any]) -> bool:
        """True when the API model lists no CONSTRUCTOR children at all, so C++ still
        provides a pure implicit default constructor (e.g. sf::Clock).

        Any declared constructor (including copy/move) means we must not invent new().
        Also skip types with public const/readonly fields (e.g. sf::Version).
        """
        if cls.get("abstract"):
            return False
        for child in cls.get("children", []):
            kind = child.get("kind")
            if kind == "CONSTRUCTOR":
                return False
            if kind == "FIELD_DECL" and child.get("access") in (None, "public") and child.get("readonly"):
                return False
        return True

    def _emit_fields(self, cls: dict[str, Any], var_name: str, full_name: str) -> list[str]:
        lines: list[str] = []
        specialization: TemplateSpecialization | None = cls.get("_template_specialization")
        replaced_fields = set(specialization.profile.replaced_fields if specialization else ())
        for field_item in cls.get("children", []):
            if field_item.get("kind") != "FIELD_DECL" or field_item.get("access") not in (None, "public"):
                continue
            field_name = field_item.get("name")
            if field_name in replaced_fields:
                continue
            type_ref = TypeRef.from_json(field_item.get("type"))
            if should_skip_type(type_ref):
                lines.append(f"    // Skipped field {full_name}::{field_name}: unsupported type {type_ref.cpp}")
                continue
            if return_needs_wrapper(type_ref) or remove_cvref(type_ref.cpp) in INTEGER_TYPES:
                getter_capture = "[lua]" if return_wrapper_uses_lua(type_ref) else "[]"
                if is_window_handle(type_ref):
                    getter_return = " -> lua_sf::WindowHandle"
                elif optional_element(type_ref.cpp):
                    getter_return = " -> sol::object"
                elif is_sf_string(type_ref.cpp) or is_filesystem_path(type_ref.cpp) or is_char_pointer(type_ref.cpp):
                    getter_return = " -> std::string"
                else:
                    getter_return = ""
                getter_lines = [f"{getter_capture}({full_name}& self){getter_return} {{"]
                getter_lines.extend(return_expr(type_ref, f"self.{field_name}", "    "))
                getter_lines.append("}")
                getter = "\n".join(getter_lines)
                if field_item.get("readonly"):
                    lines.append(f'    {var_name}.set("{field_name}", sol::property(')
                    append_indented_block(lines, getter, "        ")
                    lines.append("    ));")
                else:
                    setter_type = lua_param_type(type_ref)
                    prelude, expr = from_lua_expr(type_ref, "value")
                    setter_lines = [f"[]({full_name}& self, {setter_type} value) {{"]
                    setter_lines.extend(f"    {line}" for line in prelude)
                    setter_lines.append(f"    self.{field_name} = {expr};")
                    setter_lines.append("}")
                    setter = "\n".join(setter_lines)
                    lines.append(f'    {var_name}.set("{field_name}", sol::property(')
                    append_indented_block(lines, getter, "        ", ",")
                    append_indented_block(lines, setter, "        ")
                    lines.append("    ));")
            else:
                lines.append(
                    f'    {var_name}["{field_name}"] = sol::policies('
                    f'&{full_name}::{field_name}, sol::self_dependency{{}});'
                )
        return lines

    def _emit_methods(self, cls: dict[str, Any], var_name: str, full_name: str) -> list[str]:
        grouped: dict[
            str,
            list[
                tuple[
                    tuple[int, int, int, int],
                    str,
                    StubSignature,
                    bool,
                    str | None,
                    bool,
                ]
            ],
        ] = {}
        skipped_lines: list[str] = []
        selected: dict[
            tuple[str, tuple[str, ...], bool],
            tuple[
                int,
                int,
                tuple[int, int, int, int],
                str,
                StubSignature,
                bool,
                str | None,
                bool,
            ],
        ] = {}
        selected_order: list[tuple[str, tuple[str, tuple[str, ...], bool]]] = []
        lua_owner = lua_path_for_type(full_name)
        operator_methods: list[tuple[dict[str, Any], str]] = []
        emit_shader_uniform_array = False
        overload_index = 0

        inherited_methods = self._inherited_methods_for(cls, full_name)
        own_methods = [
            (method, full_name)
            for method in cls.get("children", [])
            if method.get("kind") == "CXX_METHOD"
        ]

        for method, dispatch_type in inherited_methods + own_methods:
            if method.get("kind") != "CXX_METHOD" or method.get("access") not in (None, "public"):
                continue
            name = method.get("name", "")
            if method.get("deleted") or name in IGNORE_NAMES:
                continue
            if name.startswith("operator"):
                operator_methods.append((method, dispatch_type))
                continue
            if full_name == "sf::Shader" and name == "setUniformArray":
                emit_shader_uniform_array = True
                continue

            for params in constructor_param_sets(method.get("parameters", [])):
                method_item = dict(method)
                method_item["parameters"] = params
                inherited = dispatch_type != full_name
                if inherited:
                    method_item["dispatch_type"] = dispatch_type
                planned = plan_parameters(params, method.get("qualified_name") or name)
                if planned.unsupported:
                    skipped_lines.append(f"    // Skipped {full_name}::{method.get('displayname')}: {planned.unsupported}.")
                    continue
                key = (name, planned.signature_key, bool(method.get("static")))
                lambda_code, reason = make_lambda(method_item, full_name, name)
                if reason:
                    skipped_lines.append(f"    // Skipped {full_name}::{method.get('displayname')}: {reason}.")
                    continue
                if lambda_code:
                    stub_signature = stub_signature_for_item(method_item, name, owner_type=full_name)
                    if stub_signature is None:
                        continue
                    score = len(planned.post_values)
                    priority = 0 if inherited else 1
                    is_static = bool(method.get("static"))
                    returns_reference = (
                        not is_static
                        and is_reference(
                            TypeRef.from_json(method_item.get("return_type")).cpp
                        )
                    )
                    specificity = overload_specificity_key(planned, overload_index)
                    overload_index += 1
                    current = selected.get(key)
                    if current is None:
                        selected[key] = (
                            score,
                            priority,
                            specificity,
                            lambda_code,
                            stub_signature,
                            is_static,
                            method.get("doc"),
                            returns_reference,
                        )
                        selected_order.append((name, key))
                    elif priority > current[1] or (priority == current[1] and score > current[0]):
                        selected[key] = (
                            score,
                            priority,
                            specificity,
                            lambda_code,
                            stub_signature,
                            is_static,
                            method.get("doc"),
                            returns_reference,
                        )

        for name, key in selected_order:
            (
                _score,
                _priority,
                specificity,
                lambda_code,
                stub_signature,
                is_static,
                doc,
                returns_reference,
            ) = selected[key]
            grouped.setdefault(name, []).append(
                (
                    specificity,
                    lambda_code,
                    stub_signature,
                    is_static,
                    doc,
                    returns_reference,
                )
            )

        lines: list[str] = []
        for name, items in grouped.items():
            sorted_items = sorted(items, key=lambda item: item[0])
            lines.extend(stub_doc_lines({"doc": first_stub_doc([item[4] for item in sorted_items])}))
            for i, (
                _specificity,
                _lambda_code,
                stub_sig,
                is_static,
                _doc,
                _returns_reference,
            ) in enumerate(sorted_items):
                macro = "LUASF_STUB_FUNCTION" if i == 0 else "LUASF_STUB_OVERLOAD"
                function_type = stub_fun_type(stub_sig, None if is_static else lua_owner)
                lines.append(
                    f'    {macro}({cpp_string_literal(lua_owner)}, {cpp_string_literal(name)}, '
                    f'{cpp_string_literal(function_type)});'
                )
            lambdas = [item[1] for item in sorted_items]
            lines.extend(
                overload_block(
                    name,
                    lambdas,
                    "    ",
                    f"{var_name}.set_function",
                    any(item[5] for item in sorted_items),
                )
            )
            if emit_shader_uniform_array and full_name == "sf::Shader" and name == "setUniform":
                lines.extend(shader_uniform_array_block(var_name, lua_owner))
                emit_shader_uniform_array = False
        if emit_shader_uniform_array and full_name == "sf::Shader":
            lines.extend(shader_uniform_array_block(var_name, lua_owner))
        lines.extend(self._emit_operator_methods(var_name, full_name, lua_owner, operator_methods, skipped_lines))
        lines.extend(skipped_lines)
        return lines

    def _emit_operator_methods(
        self,
        var_name: str,
        full_name: str,
        lua_owner: str,
        operator_methods: list[tuple[dict[str, Any], str]],
        skipped_lines: list[str],
    ) -> list[str]:
        lines: list[str] = []
        selected: dict[tuple[str, tuple[str, ...]], tuple[int, str, bool]] = {}
        selected_order: list[tuple[str, tuple[str, ...]]] = []
        write_candidates: dict[str, tuple[str, StubSignature, dict[str, Any]]] = {}
        read_methods: list[dict[str, Any]] = []
        index_methods: list[tuple[dict[str, Any], str]] = []

        for method, dispatch_type in operator_methods:
            name = method.get("name", "")
            if name == "operator[]":
                index_methods.append((method, dispatch_type))
                continue
            if name == "operator>>" and is_single_output_reference_operator(method):
                read_methods.append(method)
                continue
            if name == "operator>>" and any(is_pointer(TypeRef.from_json(param.get("type")).cpp) for param in method.get("parameters", [])):
                skipped_lines.append(
                    f"    // Skipped operator {full_name}::{method.get('displayname')}: output pointer operators require caller-owned buffers."
                )
                continue

            meta_function = OPERATOR_META_FUNCTIONS.get(name)
            if not meta_function:
                skipped_lines.append(f"    // Skipped operator {full_name}::{name}; no sol meta mapping is available.")
                continue

            for params in constructor_param_sets(method.get("parameters", [])):
                method_item = dict(method)
                method_item["parameters"] = params
                if dispatch_type != full_name:
                    method_item["dispatch_type"] = dispatch_type
                planned = plan_parameters(params, method.get("qualified_name") or name)
                if planned.unsupported:
                    skipped_lines.append(f"    // Skipped operator {full_name}::{method.get('displayname')}: {planned.unsupported}.")
                    continue
                lambda_code, reason = make_lambda(method_item, full_name, name)
                if reason or not lambda_code:
                    skipped_lines.append(
                        f"    // Skipped operator {full_name}::{method.get('displayname')}: {reason or 'unsupported signature'}."
                    )
                    continue
                key = (meta_function, planned.signature_key)
                returns_reference = is_reference(
                    TypeRef.from_json(method_item.get("return_type")).cpp
                )
                if key not in selected:
                    selected[key] = (
                        0 if dispatch_type != full_name else 1,
                        lambda_code,
                        returns_reference,
                    )
                    selected_order.append(key)
                elif selected[key][0] == 0 and dispatch_type == full_name:
                    selected[key] = (1, lambda_code, returns_reference)

                if name == "operator<<" and len(params) == 1 and remove_cvref(TypeRef.from_json(method.get("return_type")).cpp) == full_name:
                    param_type = TypeRef.from_json(params[0].get("type"))
                    io_info = packet_io_type_info(param_type)
                    if io_info and io_info["suffix"] not in write_candidates:
                        stub_signature = stub_signature_for_item(method_item, "write" + io_info["suffix"], owner_type=full_name)
                        if stub_signature:
                            write_candidates[io_info["suffix"]] = (lambda_code, stub_signature, io_info)

        grouped_meta: dict[str, list[str]] = {}
        dependent_meta: set[str] = set()
        for meta_function, signature_key in selected_order:
            grouped_meta.setdefault(meta_function, []).append(selected[(meta_function, signature_key)][1])
            if selected[(meta_function, signature_key)][2]:
                dependent_meta.add(meta_function)
        for meta_function, lambdas in grouped_meta.items():
            lines.extend(
                meta_assignment_block(
                    var_name,
                    meta_function,
                    lambdas,
                    self_dependency=meta_function in dependent_meta,
                )
            )
            if meta_function == "bitwise_left_shift":
                lines.append(
                    f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_owner)}, '
                    f'{cpp_string_literal(f"shl(any): {lua_owner}")});'
                )

        for suffix, (lambda_code, stub_signature, io_info) in write_candidates.items():
            method_name = "write" + suffix
            function_type = stub_fun_type(stub_signature, lua_owner)
            lines.append(
                f'    LUASF_STUB_FUNCTION({cpp_string_literal(lua_owner)}, {cpp_string_literal(method_name)}, '
                f'{cpp_string_literal(function_type)});'
            )
            lines.extend(
                overload_block(
                    method_name,
                    [lambda_code],
                    "    ",
                    f"{var_name}.set_function",
                    True,
                )
            )

        lines.extend(self._emit_output_reference_read_operator(var_name, full_name, lua_owner, read_methods, skipped_lines))
        lines.extend(self._emit_index_operator(var_name, full_name, index_methods, skipped_lines))
        return lines

    def _emit_output_reference_read_operator(
        self,
        var_name: str,
        full_name: str,
        lua_owner: str,
        methods: list[dict[str, Any]],
        skipped_lines: list[str],
    ) -> list[str]:
        read_infos: dict[str, tuple[str, dict[str, Any]]] = {}
        for method in methods:
            params = method.get("parameters", [])
            if len(params) != 1:
                continue
            param_type = TypeRef.from_json(params[0].get("type"))
            io_info = packet_io_type_info(param_type)
            if not io_info:
                skipped_lines.append(
                    f"    // Skipped operator {full_name}::{method.get('displayname')}: unsupported output reference type {param_type.cpp}."
                )
                continue
            if io_info["suffix"] not in read_infos:
                read_infos[io_info["suffix"]] = (remove_cvref(param_type.cpp), io_info)

        if not read_infos:
            return []

        dispatch_name = f"read_operator_{sanitize_identifier(full_name)}"
        lines = [
            f"    auto {dispatch_name} = [](sol::this_state state, {full_name}& self, std::string type) -> sol::object {{",
        ]
        for cpp_type, io_info in read_infos.values():
            checks = " || ".join(f'type == "{alias}"' for alias in io_info["aliases"])
            lines.extend(
                [
                    f"        if ({checks})",
                    "        {",
                    f"            {cpp_type} value{{}};",
                    "            self.operator>>(value);",
                    f"            return sol::make_object(state, {io_info['result']});",
                    "        }",
                ]
            )
        lines.extend(
            [
                f'        throw std::runtime_error("{lua_owner}: operator>> unknown read type " + type);',
                "    };",
            ]
        )
        lines.extend(
            meta_assignment_block(
                var_name,
                "bitwise_right_shift",
                [
                    f"""[{dispatch_name}](sol::this_state state, {full_name}& self, std::string type) -> sol::object {{
    return {dispatch_name}(state, self, std::move(type));
}}"""
                ],
            )
        )
        lines.append(
            f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_owner)}, '
            f'{cpp_string_literal("shr(string): any")});'
        )

        for suffix, (cpp_type, io_info) in read_infos.items():
            method_name = "read" + suffix
            lines.append(
                f'    LUASF_STUB_FUNCTION({cpp_string_literal(lua_owner)}, {cpp_string_literal(method_name)}, '
                f'{cpp_string_literal(f"fun(self: {lua_owner}): {io_info["lua"]}")});'
            )
            lines.append(f'    {var_name}.set_function({cpp_string_literal(method_name)},')
            append_indented_block(
                lines,
                f"""[]({full_name}& self) {{
    {cpp_type} value{{}};
    self.operator>>(value);
    return {io_info['result']};
}}""",
                "        ",
            )
            lines.append("    );")
        return lines

    def _emit_index_operator(
        self,
        var_name: str,
        full_name: str,
        methods: list[tuple[dict[str, Any], str]],
        skipped_lines: list[str],
    ) -> list[str]:
        getter: tuple[dict[str, Any], str] | None = None
        for method, dispatch_type in methods:
            params = method.get("parameters", [])
            return_type = TypeRef.from_json(method.get("return_type"))
            if len(params) != 1 or not is_reference(return_type.cpp):
                skipped_lines.append(f"    // Skipped operator {full_name}::{method.get('displayname')}: unsupported index operator signature.")
                continue
            if not is_const_type(return_type.cpp):
                getter = (method, dispatch_type)
                break
            if getter is None:
                getter = (method, dispatch_type)
        if getter is None:
            return []

        method, dispatch_type = getter
        param_type = TypeRef.from_json(method.get("parameters", [])[0].get("type"))
        index_type = remove_cvref(param_type.source or param_type.cpp)
        return_type = TypeRef.from_json(method.get("return_type"))
        value_type = remove_cvref(return_type.cpp)
        value_lua_type = type_ref_to_lua_type(return_type)
        index_lua_type = type_ref_to_lua_type(param_type)
        table_var = f"table_{sanitize_identifier(full_name)}"
        index_fn = f"index_operator_key_{sanitize_identifier(full_name)}"
        call_target = "self"
        if dispatch_type != full_name:
            call_target = f"static_cast<{dispatch_type}&>(self)"

        lines = [
            f"    auto {index_fn} = [](sol::object key) -> {index_type} {{",
            "        if (key.get_type() != sol::type::number || !key.is<lua_Integer>())",
            f'            throw std::invalid_argument("{lua_path_for_type(full_name)} index must be an integer");',
            "        const lua_Integer index = key.as<lua_Integer>();",
        ]
        if index_type.startswith("unsigned") or index_type in SIZE_TYPE_NAMES:
            lines.extend(
                [
                    "        if (index < 0)",
                    f'            throw std::out_of_range("{lua_path_for_type(full_name)} index must be non-negative");',
                ]
            )
        lines.extend(
            [
                f"        return static_cast<{index_type}>(index);",
                "    };",
                f"    {var_name}[sol::meta_function::index] =",
            ]
        )
        append_indented_block(
            lines,
            f"""sol::policies(
    [{table_var}, {index_fn}](sol::this_state state, {full_name}& self, sol::object key) -> sol::object {{
    if (key.get_type() != sol::type::number)
        return {table_var}.get<sol::object>(key);
    const {index_type} index = {index_fn}(key);
    return sol::make_object(state, std::ref({call_target}.operator[](index)));
    }},
    sol::self_dependency{{}}
)""",
            "        ",
        )
        lines.append("    ;")
        lines.append(
            f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_path_for_type(full_name))}, '
            f'{cpp_string_literal(f"get({index_lua_type}): {value_lua_type}")});'
        )
        lines.append(
            f'    LUASF_STUB_INDEX_FIELD({cpp_string_literal(lua_path_for_type(full_name))}, '
            f'{cpp_string_literal(index_lua_type)}, {cpp_string_literal(value_lua_type)});'
        )
        if not is_const_type(return_type.cpp):
            lines.append(f"    {var_name}[sol::meta_function::new_index] =")
            append_indented_block(
                lines,
                f"""[{index_fn}]({full_name}& self, sol::object key, const {value_type}& value) {{
    const {index_type} index = {index_fn}(key);
    {call_target}.operator[](index) = value;
}}""",
                "        ",
            )
            lines.append("    ;")
            lines.append(
                f'    LUASF_STUB_OPERATOR({cpp_string_literal(lua_path_for_type(full_name))}, '
                f'{cpp_string_literal(f"set({index_lua_type}, {value_lua_type})")});'
            )
        return lines

    def _emit_free_function(self, func: dict[str, Any], table_var: str, namespace_prefix: str) -> list[str]:
        name = func.get("name", "")
        if name in IGNORE_NAMES or name.startswith("operator"):
            return []
        full_name = f"{namespace_prefix}{name}" if namespace_prefix else (func.get("qualified_name") or name)
        lambda_code, reason = make_lambda(func, None, full_name)
        if reason or not lambda_code:
            return [f"    // Skipped function {full_name}: {reason or 'unsupported signature'}."]
        signature = stub_signature_for_item(func, full_name)
        lines: list[str] = []
        if signature:
            owner = stub_owner_for_table_var(table_var)
            lines.extend(stub_doc_lines(func))
            lines.append(
                f'    LUASF_STUB_FUNCTION({cpp_string_literal(owner)}, {cpp_string_literal(name)}, '
                f'{cpp_string_literal(stub_fun_type(signature))});'
            )
        lines.extend(overload_block(name, [lambda_code], "    ", f"{table_var}.set_function"))
        return lines

    def _emit_free_functions(
        self,
        functions: list[dict[str, Any]],
        table_var: str,
        namespace_prefix: str,
    ) -> list[str]:
        grouped: dict[str, list[tuple[tuple[int, int, int, int], str, StubSignature, str | None]]] = {}
        skipped_lines: list[str] = []
        selected: dict[tuple[str, tuple[str, ...]], tuple[int, tuple[int, int, int, int], str, StubSignature, str | None]] = {}
        selected_order: list[tuple[str, tuple[str, tuple[str, ...]]]] = []
        overload_index = 0

        for func in functions:
            name = func.get("name", "")
            if name in IGNORE_NAMES or name.startswith("operator"):
                continue
            full_name = f"{namespace_prefix}{name}" if namespace_prefix else (func.get("qualified_name") or name)

            for params in constructor_param_sets(func.get("parameters", [])):
                func_item = dict(func)
                func_item["parameters"] = params
                planned = plan_parameters(params, func.get("qualified_name") or full_name)
                if planned.unsupported:
                    skipped_lines.append(f"    // Skipped function {full_name}: {planned.unsupported}.")
                    continue

                key = (name, planned.signature_key)
                lambda_code, reason = make_lambda(func_item, None, full_name)
                if reason:
                    skipped_lines.append(f"    // Skipped function {full_name}: {reason}.")
                    continue
                if not lambda_code:
                    continue
                stub_signature = stub_signature_for_item(func_item, full_name)
                if stub_signature is None:
                    continue

                score = len(planned.post_values)
                specificity = overload_specificity_key(planned, overload_index)
                overload_index += 1
                current = selected.get(key)
                if current is None:
                    selected[key] = (score, specificity, lambda_code, stub_signature, func.get("doc"))
                    selected_order.append((name, key))
                elif score > current[0]:
                    selected[key] = (score, specificity, lambda_code, stub_signature, func.get("doc"))

        for name, key in selected_order:
            _score, specificity, lambda_code, stub_signature, doc = selected[key]
            grouped.setdefault(name, []).append((specificity, lambda_code, stub_signature, doc))

        lines: list[str] = []
        owner = stub_owner_for_table_var(table_var)
        for name, items in grouped.items():
            sorted_items = sorted(items, key=lambda item: item[0])
            lines.extend(stub_doc_lines({"doc": first_stub_doc([item[3] for item in sorted_items])}))
            for i, (_specificity, _lambda_code, stub_sig, _doc) in enumerate(sorted_items):
                macro = "LUASF_STUB_FUNCTION" if i == 0 else "LUASF_STUB_OVERLOAD"
                lines.append(
                    f'    {macro}({cpp_string_literal(owner)}, {cpp_string_literal(name)}, '
                    f'{cpp_string_literal(stub_fun_type(stub_sig))});'
                )
            lambdas = [item[1] for item in sorted_items]
            lines.extend(overload_block(name, lambdas, "    ", f"{table_var}.set_function"))
        lines.extend(skipped_lines)
        return lines

    def _emit_var(self, item: dict[str, Any], table_var: str, namespace_prefix: str) -> list[str]:
        name = item.get("name", "")
        if not name or name in IGNORE_NAMES:
            return []
        full_name = item.get("qualified_name") or f"{namespace_prefix}{name}"
        if is_anonymous_cpp_name(full_name):
            full_name = f"{namespace_prefix}{name}"
        type_ref = TypeRef.from_json(item.get("type"))
        owner = stub_owner_for_table_var(table_var)
        stub_line = (
            f'    LUASF_STUB_VALUE({cpp_string_literal(owner)}, {cpp_string_literal(name)}, '
            f'{cpp_string_literal(type_ref_to_lua_type(type_ref))});'
        )
        stub_lines = [*stub_doc_lines(item), stub_line]
        if is_window_handle(type_ref):
            return [*stub_lines, f'    {table_var}["{name}"] = lua_sf::window_handle_to_integer({full_name});']
        if is_sf_string(type_ref.cpp):
            return [*stub_lines, f'    {table_var}["{name}"] = lua_sf::to_utf8_string({full_name});']
        if is_filesystem_path(type_ref.cpp):
            return [*stub_lines, f'    {table_var}["{name}"] = {full_name}.string();']
        if vector_element(type_ref.cpp):
            return [*stub_lines, f'    {table_var}["{name}"] = sol::as_table({full_name});']
        if optional_element(type_ref.cpp):
            return [*stub_lines, f'    {table_var}["{name}"] = lua_sf::optional_to_object(lua, {full_name});']
        return [*stub_lines, f'    {table_var}["{name}"] = {full_name};']


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate sol2 binding source files from output/sfml_api.json.")
    parser.add_argument("--api-json", default="output/sfml_api.json")
    parser.add_argument("--output-root", default="output")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    api_path = Path(args.api_json).resolve()
    output_root = Path(args.output_root).resolve()
    api = json.loads(api_path.read_text(encoding="utf-8"))
    generator = Sol2Generator(api, output_root)
    generator.generate()
    print(f"Generated sol2 bindings for {len(api.get('files', []))} headers into {output_root}")
    if generator.skipped:
        print(f"Skipped {len(generator.skipped)} signatures; see generated comments for details.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
