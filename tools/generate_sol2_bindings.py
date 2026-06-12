from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    from .replace_model import (
        AUDIO_EFFECT_PROCESSOR_LUA_TYPE,
        AUDIO_EFFECT_PROCESSOR_SIGNATURE,
        BYTE_TYPES,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LifecycleCategory,
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
        TYPE_DECL_KINDS,
        get_lifecycle,
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
    )
except ImportError:
    from replace_model import (
        AUDIO_EFFECT_PROCESSOR_LUA_TYPE,
        AUDIO_EFFECT_PROCESSOR_SIGNATURE,
        BYTE_TYPES,
        IGNORE_NAMES,
        IGNORE_PARAM_TYPES,
        IGNORE_RETURN_TYPES,
        IGNORED_NAMESPACES,
        INTEGER_TYPES,
        LifecycleCategory,
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
        TYPE_DECL_KINDS,
        get_lifecycle,
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
    )


@dataclass(frozen=True)
class StubParam:
    name: str
    lua_type: str


@dataclass(frozen=True)
class StubSignature:
    params: tuple[StubParam, ...]
    returns: tuple[str, ...] = ()


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


def clean_cpp_type(value: str) -> str:
    value = value.strip()
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


def sfml_include_for_file(file_item: dict[str, Any]) -> str:
    return "/".join(Path(file_item["path"]).parts[-3:])


def walk_declarations(items: list[dict[str, Any]]):
    for item in items:
        yield item
        yield from walk_declarations(item.get("children", []))


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
    for candidate in (type_ref.cpp, type_ref.source):
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


def std_function_lua_type(type_ref: TypeRef) -> str:
    signature = std_function_signature(type_ref)
    if signature == AUDIO_EFFECT_PROCESSOR_SIGNATURE:
        return AUDIO_EFFECT_PROCESSOR_LUA_TYPE
    return "fun(...): any"


def is_window_handle(type_ref: TypeRef) -> bool:
    source = remove_cvref(type_ref.source)
    return source in {"WindowHandle", "sf::WindowHandle"}


def is_anonymous_cpp_name(value: str) -> bool:
    return "(unnamed" in value or "(anonymous" in value


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
    if is_std_function(type_ref):
        return "sol::object"
    if is_sf_string(cpp) or is_filesystem_path(cpp) or is_string_view(cpp) or is_std_string(cpp) or is_std_wstring(cpp):
        return "std::string"
    if vector_element(cpp):
        return "sol::table"
    if optional_element(cpp):
        return "sol::object"
    return cpp


def from_lua_expr(type_ref: TypeRef, name: str) -> tuple[list[str], str]:
    cpp = type_ref.cpp
    if is_window_handle(type_ref):
        return [], f"{name}.native()"
    signature = std_function_signature(type_ref)
    if signature:
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


def sanitize_identifier(value: str) -> str:
    value = re.sub(r"[^0-9A-Za-z_]", "_", value)
    if value and value[0].isdigit():
        value = "_" + value
    return value or "unnamed"


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
    if qualified_name.startswith("sf::"):
        qualified_name = qualified_name[len("sf::") :]
        return "sf." + qualified_name.replace("::", ".")
    return qualified_name.replace("::", ".")


def lua_leaf_for_type(qualified_name: str) -> str:
    qualified_name = clean_cpp_type(qualified_name)
    return qualified_name.rsplit("::", 1)[-1]


def sanitize_lua_identifier(value: str) -> str:
    value = sanitize_identifier(value)
    if value in LUA_KEYWORDS:
        return f"{value}_"
    return value


def vector_template_lua_type(template_name: str, elem: str) -> str | None:
    elem = clean_cpp_type(elem)
    if template_name == "sf::Vector2":
        return {
            "int": "sf.Vector2i",
            "float": "sf.Vector2f",
            "unsigned int": "sf.Vector2u",
            "bool": "sf.Vector2b",
        }.get(elem)
    if template_name == "sf::Vector3":
        return {
            "int": "sf.Vector3i",
            "float": "sf.Vector3f",
            "unsigned int": "sf.Vector3u",
            "bool": "sf.Vector3b",
        }.get(elem)
    if template_name in {"sf::priv::Vector4", "sf::Glsl::Vector4"}:
        return {
            "int": "sf.Vector4i",
            "float": "sf.Vector4f",
            "bool": "sf.Vector4b",
        }.get(elem)
    if template_name == "sf::Rect":
        return {
            "int": "sf.IntRect",
            "float": "sf.FloatRect",
        }.get(elem)
    return None


def cpp_type_to_lua_type(value: str) -> str:
    value = clean_cpp_type(value)
    base = remove_cvref(value)
    if base.endswith("*") and not is_char_pointer(base):
        base = remove_pointer(base)

    if not base or base == "void":
        return "nil"
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

    type_ref = TypeRef(spelling=value, canonical=value)
    if std_function_signature(type_ref):
        return std_function_lua_type(type_ref)

    vec_elem = vector_element(base)
    if vec_elem:
        return f"{cpp_type_to_lua_type(vec_elem)}[]"

    opt_elem = optional_element(base)
    if opt_elem:
        return f"{cpp_type_to_lua_type(opt_elem)}|nil"

    for template_name in ("sf::Vector2", "sf::Vector3", "sf::priv::Vector4", "sf::Glsl::Vector4", "sf::Rect"):
        if is_template(base, template_name):
            mapped = vector_template_lua_type(template_name, split_template_args(base)[0])
            if mapped:
                return mapped

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


def stub_owner_for_table_var(table_var: str) -> str:
    if table_var == "sf":
        return "sf"
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
        prelude, expr = from_lua_expr(type_ref, name)
        plan.lua_params.append(f"{lua_type} {name}")
        plan.prelude.extend(prelude)
        plan.call_args.append(expr)
        if is_std_function(type_ref):
            plan.stub_param_types[sanitize_lua_identifier(name)] = std_function_lua_type(type_ref)
        plan.signature_key += (lua_type,)

    return plan


def make_lambda(
    item: dict[str, Any],
    owner_type: str | None,
    call_name: str,
    is_constructor: bool = False,
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
        factory = "lua_sf::makeLongLivedMemoryObject" if is_memory_lifecycle else "std::make_unique"
        lines.append(f"    return {factory}<{owner_type}>({', '.join(plan.call_args)});")
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


def overload_block(name: str, lambdas: list[str], indent: str, target: str = "set_function") -> list[str]:
    if not lambdas:
        return []
    if len(lambdas) == 1:
        lines = [f'{indent}{target}("{name}",']
        append_indented_block(lines, lambdas[0], indent + "    ")
        lines.append(f"{indent});")
        return lines
    lines = [f'{indent}{target}("{name}", sol::overload(']
    for index, lambda_code in enumerate(lambdas):
        suffix = "," if index + 1 < len(lambdas) else ""
        append_indented_block(lines, lambda_code, indent + "    ", suffix)
    lines.append(f"{indent}));")
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


def meta_assignment_block(var_name: str, meta_function: str, lambdas: list[str], indent: str = "    ") -> list[str]:
    if not lambdas:
        return []
    lines = [f"{indent}{var_name}[sol::meta_function::{meta_function}] ="]
    if len(lambdas) == 1:
        append_indented_block(lines, lambdas[0], indent + "    ")
    else:
        lines.append(f"{indent}    sol::overload(")
        for index, lambda_code in enumerate(lambdas):
            suffix = "," if index + 1 < len(lambdas) else ""
            append_indented_block(lines, lambda_code, indent + "        ", suffix)
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
        self.class_map = self._build_class_map()
        self.type_includes = self._build_type_includes()
        self.sorted_type_includes = sorted(self.type_includes.items(), key=lambda item: len(item[0]), reverse=True)

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
            "#include <memory>",
            "#include <tuple>",
            "#include <utility>",
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
            f'    LUASF_STUB_CLASS({cpp_string_literal(lua_path)});',
            *[
                f'    LUASF_STUB_FIELD({cpp_string_literal(constant["name"])}, "integer");'
                for constant in constants
            ],
            f'    {table_var}.new_enum("{lua_name}",',
        ]
        for index, constant in enumerate(constants):
            suffix = "," if index + 1 < len(constants) else ""
            lines.append(f'        "{constant["name"]}", {full_name}::{constant["name"]}{suffix}')
        lines.append("    );")
        return lines

    def _emit_class(self, cls: dict[str, Any], table_var: str) -> list[str]:
        full_name = cls.get("qualified_name") or cls.get("name")
        if not full_name or full_name == "sf::String" or full_name.startswith("sf::priv::"):
            return [f"    // Skipped class {full_name}: converted through lua_sf utilities."]

        lua_name = lua_leaf_for_type(full_name)
        lua_path = lua_path_for_type(full_name)
        var_name = f"type_{sanitize_identifier(full_name)}"
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
        lines.append(f'    LUASF_STUB_CLASS({cpp_string_literal(lua_path)});')
        lines.extend(self._stub_field_lines(cls))
        lines.extend(self._emit_constructors(cls, var_name, full_name))
        lines.extend(self._emit_fields(cls, var_name, full_name))
        lines.extend(self._emit_methods(cls, var_name, full_name))

        for child in cls.get("children", []):
            if child.get("kind") == "ENUM_DECL":
                lines.extend(self._emit_enum(child, nested_table_var))
            elif child.get("kind") in {"CLASS_DECL", "STRUCT_DECL"}:
                lines.extend(self._emit_class(child, nested_table_var))
        return lines

    def _stub_field_lines(self, cls: dict[str, Any]) -> list[str]:
        lines: list[str] = []
        for field_item in cls.get("children", []):
            if field_item.get("kind") != "FIELD_DECL" or field_item.get("access") not in (None, "public"):
                continue
            field_name = field_item.get("name")
            type_ref = TypeRef.from_json(field_item.get("type"))
            if not field_name or should_skip_type(type_ref):
                continue
            lines.append(
                f'    LUASF_STUB_FIELD({cpp_string_literal(field_name)}, '
                f'{cpp_string_literal(type_ref_to_lua_type(type_ref))});'
            )
        return lines

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

        overloads: list[tuple[tuple[int, int, int, int], str, StubSignature]] = []
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
                lambda_code, reason = make_lambda(ctor_item, full_name, full_name, is_constructor=True)
                if reason:
                    self.skipped.append(f"{full_name}::{ctor.get('displayname')}: {reason}")
                    continue
                if lambda_code:
                    stub_signature = stub_signature_for_item(ctor_item, "new", constructor_return=lua_owner)
                    if stub_signature:
                        overloads.append((overload_specificity_key(planned, overload_index), lambda_code, stub_signature))
                        overload_index += 1

        if not overloads:
            # Synthesize a default constructor for aggregate types
            # (types with public fields but no user-declared constructors).
            if self._has_default_constructible_aggregate(cls):
                default_lambda = f"[]() {{\n    return std::make_unique<{full_name}>();\n}}"
                default_stub = StubSignature((), (lua_owner,))
                overloads.append(((0, 0, 0, overload_index), default_lambda, default_stub))
            else:
                return []
        overloads.sort(key=lambda item: item[0])
        # Emit stub annotations for all overloads, longest first.
        sorted_stubs = [item[2] for item in overloads]
        stub_lines: list[str] = []
        for i, sig in enumerate(sorted_stubs):
            macro = "LUASF_STUB_FUNCTION" if i == 0 else "LUASF_STUB_OVERLOAD"
            stub_lines.append(
                f'    {macro}({cpp_string_literal(lua_owner)}, "new", '
                f'{cpp_string_literal(stub_fun_type(sig))});'
            )

        lambdas = [item[1] for item in overloads]
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

    def _emit_fields(self, cls: dict[str, Any], var_name: str, full_name: str) -> list[str]:
        lines: list[str] = []
        for field_item in cls.get("children", []):
            if field_item.get("kind") != "FIELD_DECL" or field_item.get("access") not in (None, "public"):
                continue
            field_name = field_item.get("name")
            type_ref = TypeRef.from_json(field_item.get("type"))
            if should_skip_type(type_ref):
                lines.append(f"    // Skipped field {full_name}::{field_name}: unsupported type {type_ref.cpp}")
                continue
            if return_needs_wrapper(type_ref):
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
                lines.append(f'    {var_name}["{field_name}"] = &{full_name}::{field_name};')
        return lines

    def _emit_methods(self, cls: dict[str, Any], var_name: str, full_name: str) -> list[str]:
        grouped: dict[str, list[tuple[tuple[int, int, int, int], str, StubSignature, bool]]] = {}
        skipped_lines: list[str] = []
        selected: dict[tuple[str, tuple[str, ...], bool], tuple[int, int, tuple[int, int, int, int], str, StubSignature, bool]] = {}
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
                    specificity = overload_specificity_key(planned, overload_index)
                    overload_index += 1
                    current = selected.get(key)
                    if current is None:
                        selected[key] = (score, priority, specificity, lambda_code, stub_signature, is_static)
                        selected_order.append((name, key))
                    elif priority > current[1] or (priority == current[1] and score > current[0]):
                        selected[key] = (score, priority, specificity, lambda_code, stub_signature, is_static)

        for name, key in selected_order:
            _score, _priority, specificity, lambda_code, stub_signature, is_static = selected[key]
            grouped.setdefault(name, []).append((specificity, lambda_code, stub_signature, is_static))

        lines: list[str] = []
        for name, items in grouped.items():
            sorted_items = sorted(items, key=lambda item: item[0])
            for i, (_specificity, _lambda_code, stub_sig, is_static) in enumerate(sorted_items):
                macro = "LUASF_STUB_FUNCTION" if i == 0 else "LUASF_STUB_OVERLOAD"
                function_type = stub_fun_type(stub_sig, None if is_static else lua_owner)
                lines.append(
                    f'    {macro}({cpp_string_literal(lua_owner)}, {cpp_string_literal(name)}, '
                    f'{cpp_string_literal(function_type)});'
                )
            lambdas = [item[1] for item in sorted_items]
            lines.extend(overload_block(name, lambdas, "    ", f"{var_name}.set_function"))
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
        selected: dict[tuple[str, tuple[str, ...]], tuple[int, str]] = {}
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
                if key not in selected:
                    selected[key] = (0 if dispatch_type != full_name else 1, lambda_code)
                    selected_order.append(key)
                elif selected[key][0] == 0 and dispatch_type == full_name:
                    selected[key] = (1, lambda_code)

                if name == "operator<<" and len(params) == 1 and remove_cvref(TypeRef.from_json(method.get("return_type")).cpp) == full_name:
                    param_type = TypeRef.from_json(params[0].get("type"))
                    io_info = packet_io_type_info(param_type)
                    if io_info and io_info["suffix"] not in write_candidates:
                        stub_signature = stub_signature_for_item(method_item, "write" + io_info["suffix"], owner_type=full_name)
                        if stub_signature:
                            write_candidates[io_info["suffix"]] = (lambda_code, stub_signature, io_info)

        grouped_meta: dict[str, list[str]] = {}
        for meta_function, signature_key in selected_order:
            grouped_meta.setdefault(meta_function, []).append(selected[(meta_function, signature_key)][1])
        for meta_function, lambdas in grouped_meta.items():
            lines.extend(meta_assignment_block(var_name, meta_function, lambdas))
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
            lines.extend(overload_block(method_name, [lambda_code], "    ", f"{var_name}.set_function"))

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
            f"""[{table_var}, {index_fn}](sol::this_state state, {full_name}& self, sol::object key) -> sol::object {{
    if (key.get_type() != sol::type::number)
        return {table_var}.get<sol::object>(key);
    const {index_type} index = {index_fn}(key);
    return sol::make_object(state, std::ref({call_target}.operator[](index)));
}}""",
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
        grouped: dict[str, list[tuple[tuple[int, int, int, int], str, StubSignature]]] = {}
        skipped_lines: list[str] = []
        selected: dict[tuple[str, tuple[str, ...]], tuple[int, tuple[int, int, int, int], str, StubSignature]] = {}
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
                    selected[key] = (score, specificity, lambda_code, stub_signature)
                    selected_order.append((name, key))
                elif score > current[0]:
                    selected[key] = (score, specificity, lambda_code, stub_signature)

        for name, key in selected_order:
            _score, specificity, lambda_code, stub_signature = selected[key]
            grouped.setdefault(name, []).append((specificity, lambda_code, stub_signature))

        lines: list[str] = []
        owner = stub_owner_for_table_var(table_var)
        for name, items in grouped.items():
            sorted_items = sorted(items, key=lambda item: item[0])
            for i, (_specificity, _lambda_code, stub_sig) in enumerate(sorted_items):
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
        if is_window_handle(type_ref):
            return [stub_line, f'    {table_var}["{name}"] = lua_sf::window_handle_to_integer({full_name});']
        if is_sf_string(type_ref.cpp):
            return [stub_line, f'    {table_var}["{name}"] = lua_sf::to_utf8_string({full_name});']
        if is_filesystem_path(type_ref.cpp):
            return [stub_line, f'    {table_var}["{name}"] = {full_name}.string();']
        if vector_element(type_ref.cpp):
            return [stub_line, f'    {table_var}["{name}"] = sol::as_table({full_name});']
        if optional_element(type_ref.cpp):
            return [stub_line, f'    {table_var}["{name}"] = lua_sf::optional_to_object(lua, {full_name});']
        return [stub_line, f'    {table_var}["{name}"] = {full_name};']


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
