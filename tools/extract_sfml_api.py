from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

from clang import cindex


DEFAULT_LIBCLANG = r"C:\Program Files\LLVM\bin\libclang.dll"
DEFAULT_MODULES = ("Audio", "Graphics", "Network", "System", "Window")
IGNORED_MACROS = (
    "SFML_AUDIO_API",
    "SFML_GRAPHICS_API",
    "SFML_NETWORK_API",
    "SFML_SYSTEM_API",
    "SFML_WINDOW_API",
)
EXTRA_DEFINES = (
    "_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
)
EXCLUDED_HEADERS = {
    "Audio": {
        "AudioResource.hpp",
        "Export.hpp",
        "SoundFileFactory.hpp",
    },
    "Graphics": {
        "Drawable.hpp",
        "Export.hpp",
    },
    "Network": {
        "Export.hpp",
        "SocketHandle.hpp",
    },
    "System": {
        "Err.hpp",
        "Exception.hpp",
        "Export.hpp",
        "NativeActivity.hpp",
        "String.hpp",
        "SuspendAwareClock.hpp",
        "Utf.hpp",
    },
    "Window": {
        "Event.hpp",
        "Export.hpp",
        "GlResource.hpp",
        "Vulkan.hpp",
    },
}

# Some public aliases are declared in a regular header while the template
# definitions they expose live in an included implementation file.  Associate
# those declarations with the public header so downstream generators still
# produce a single binding unit for the public surface.
DECLARATION_COMPANIONS = {
    "SFML/Graphics/Glsl.hpp": ("SFML/Graphics/Glsl.inl",),
}

DECL_KINDS = {
    cindex.CursorKind.NAMESPACE,
    cindex.CursorKind.CLASS_DECL,
    cindex.CursorKind.STRUCT_DECL,
    cindex.CursorKind.CLASS_TEMPLATE,
    cindex.CursorKind.FUNCTION_TEMPLATE,
    cindex.CursorKind.CXX_METHOD,
    cindex.CursorKind.CONSTRUCTOR,
    cindex.CursorKind.DESTRUCTOR,
    cindex.CursorKind.CONVERSION_FUNCTION,
    cindex.CursorKind.FUNCTION_DECL,
    cindex.CursorKind.FIELD_DECL,
    cindex.CursorKind.ENUM_DECL,
    cindex.CursorKind.ENUM_CONSTANT_DECL,
    cindex.CursorKind.VAR_DECL,
    cindex.CursorKind.TYPEDEF_DECL,
    cindex.CursorKind.TYPE_ALIAS_DECL,
}

FUNCTION_KINDS = {
    cindex.CursorKind.CXX_METHOD,
    cindex.CursorKind.CONSTRUCTOR,
    cindex.CursorKind.DESTRUCTOR,
    cindex.CursorKind.CONVERSION_FUNCTION,
    cindex.CursorKind.FUNCTION_DECL,
    cindex.CursorKind.FUNCTION_TEMPLATE,
}

RECORD_KINDS = {
    cindex.CursorKind.CLASS_DECL,
    cindex.CursorKind.STRUCT_DECL,
    cindex.CursorKind.CLASS_TEMPLATE,
}


def default_libclang_path() -> str:
    env_path = os.environ.get("LIBCLANG_PATH")
    if env_path:
        return env_path

    candidates: list[str]
    system = platform.system()
    if system == "Darwin":
        candidates = [
            "/opt/homebrew/opt/llvm/lib/libclang.dylib",
            "/usr/local/opt/llvm/lib/libclang.dylib",
            "/Library/Developer/CommandLineTools/usr/lib/libclang.dylib",
            "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib",
        ]
    elif system == "Linux":
        candidates = [
            "/usr/lib/llvm-18/lib/libclang.so",
            "/usr/lib/llvm-17/lib/libclang.so",
            "/usr/lib/llvm-16/lib/libclang.so",
            "/usr/lib/x86_64-linux-gnu/libclang-18.so",
            "/usr/lib/x86_64-linux-gnu/libclang-17.so",
            "/usr/lib/x86_64-linux-gnu/libclang-16.so",
            "/usr/lib/libclang.so",
        ]
    else:
        candidates = [DEFAULT_LIBCLANG]

    for candidate in candidates:
        if Path(candidate).is_file():
            return candidate
    return candidates[0]


def system_sysroot() -> str | None:
    """Return the Xcode SDK path on macOS, or None if not found / not on macOS."""
    if platform.system() != "Darwin":
        return None
    try:
        return subprocess.check_output(
            ["xcrun", "--show-sdk-path"], encoding="utf-8"
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def clang_resource_dir(libclang_path: str) -> str | None:
    """Try to find the clang resource directory from a nearby clang binary."""
    if platform.system() != "Darwin":
        return None
    libclang = Path(libclang_path)
    # Homebrew: /opt/homebrew/opt/llvm/lib/libclang.dylib → /opt/homebrew/opt/llvm/bin/clang
    clang_candidates = [
        libclang.parent.parent / "bin" / "clang",
    ]
    for clang in clang_candidates:
        if clang.is_file():
            try:
                return subprocess.check_output(
                    [str(clang), "-print-resource-dir"], encoding="utf-8"
                ).strip()
            except (subprocess.CalledProcessError, FileNotFoundError):
                pass
    return None


def configure_libclang(path: str) -> None:
    if not Path(path).is_file():
        raise FileNotFoundError(
            f"libclang not found: {path}. Set LIBCLANG_PATH or pass --libclang."
        )
    cindex.Config.set_library_file(path)


def access_name(cursor: cindex.Cursor) -> str | None:
    try:
        access = cursor.access_specifier
    except Exception:
        return None

    if access == cindex.AccessSpecifier.PUBLIC:
        return "public"
    if access == cindex.AccessSpecifier.PROTECTED:
        return "protected"
    if access == cindex.AccessSpecifier.PRIVATE:
        return "private"

    parent = cursor.semantic_parent
    if parent and parent.kind == cindex.CursorKind.STRUCT_DECL:
        return "public"
    if parent and parent.kind in RECORD_KINDS:
        return "private"
    return None


def qualified_name(cursor: cindex.Cursor) -> str:
    names: list[str] = []
    current = cursor
    while current and current.kind != cindex.CursorKind.TRANSLATION_UNIT:
        if current.spelling:
            names.append(current.spelling)
        current = current.semantic_parent
    return "::".join(reversed(names))


def clean_doc(raw_comment: str | None) -> str | None:
    if not raw_comment:
        return None
    cleaned_lines: list[str] = []
    for line in raw_comment.splitlines():
        line = line.strip()
        line = re.sub(r"^/\*+<?\s?", "", line)
        line = re.sub(r"\*/$", "", line)
        line = re.sub(r"^\*+\s?", "", line)
        line = re.sub(r"^//[/!<]*\s?", "", line)
        cleaned_lines.append(line.strip())
    text = "\n".join(cleaned_lines).strip()
    return text or None


def type_info(type_obj: cindex.Type | None) -> dict[str, str] | None:
    if not type_obj or type_obj.kind == cindex.TypeKind.INVALID:
        return None
    canonical = type_obj.get_canonical()
    return {
        "spelling": type_obj.spelling,
        "canonical": canonical.spelling if canonical else type_obj.spelling,
        "kind": str(type_obj.kind).replace("TypeKind.", ""),
    }


def default_value(param: cindex.Cursor) -> str | None:
    tokens = [token.spelling for token in param.get_tokens()]
    if "=" not in tokens:
        return None
    index = tokens.index("=")
    value = " ".join(tokens[index + 1 :]).strip()
    value = re.sub(r"\s*::\s*", "::", value)
    value = re.sub(r"\s*([(),<>])\s*", r"\1", value)
    return value or None


def parameters(cursor: cindex.Cursor) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    arguments = list(cursor.get_arguments() or [])
    if not arguments and cursor.kind == cindex.CursorKind.FUNCTION_TEMPLATE:
        arguments = [
            child
            for child in cursor.get_children()
            if child.kind == cindex.CursorKind.PARM_DECL
        ]
    for index, arg in enumerate(arguments):
        result.append(
            {
                "name": arg.spelling or f"arg{index}",
                "type": type_info(arg.type),
                "default": default_value(arg),
                "line": arg.location.line if arg.location and arg.location.file else None,
            }
        )
    return result


def base_classes(cursor: cindex.Cursor) -> list[dict[str, Any]]:
    bases: list[dict[str, Any]] = []
    for child in cursor.get_children():
        if child.kind == cindex.CursorKind.CXX_BASE_SPECIFIER:
            bases.append(
                {
                    "name": child.type.spelling,
                    "access": access_name(child),
                    "line": child.location.line if child.location and child.location.file else None,
                }
            )
    return bases


def cursor_file(cursor: cindex.Cursor) -> Path | None:
    location = cursor.location
    if not location or not location.file:
        return None
    try:
        return Path(location.file.name).resolve()
    except OSError:
        return None


def is_in_target_file(cursor: cindex.Cursor, target_file: Path) -> bool:
    source = cursor_file(cursor)
    if source is None:
        return False
    try:
        return source == target_file.resolve()
    except OSError:
        return False


def is_public_surface(cursor: cindex.Cursor) -> bool:
    access = access_name(cursor)
    if access in {"private", "protected"}:
        return False
    parent = cursor.semantic_parent
    while parent and parent.kind != cindex.CursorKind.TRANSLATION_UNIT:
        parent_access = access_name(parent)
        if parent_access in {"private", "protected"}:
            return False
        parent = parent.semantic_parent
    return True


def node_to_dict(
    cursor: cindex.Cursor,
    target_file: Path,
    include_non_public: bool,
) -> dict[str, Any] | list[dict[str, Any]] | None:
    if not is_in_target_file(cursor, target_file):
        return None

    if cursor.kind not in DECL_KINDS:
        return None

    if not include_non_public and not is_public_surface(cursor):
        return None

    if cursor.kind in RECORD_KINDS and not cursor.is_definition():
        return None

    if cursor.kind == cindex.CursorKind.ENUM_DECL and cursor.is_anonymous():
        constants: list[dict[str, Any]] = []
        for child in cursor.get_children():
            if child.kind != cindex.CursorKind.ENUM_CONSTANT_DECL:
                continue
            if not include_non_public and not is_public_surface(child):
                continue
            constant: dict[str, Any] = {
                "kind": "VAR_DECL",
                "name": child.spelling,
                "displayname": child.displayname,
                "qualified_name": qualified_name(child),
                "type": type_info(cursor.enum_type),
                "value": child.enum_value,
                "access": access_name(child),
                "readonly": True,
                "line": child.location.line if child.location and child.location.file else None,
            }
            doc = clean_doc(getattr(child, "raw_comment", None))
            if doc:
                constant["doc"] = doc
            constants.append(constant)
        return constants or None

    item: dict[str, Any] = {
        "kind": str(cursor.kind).replace("CursorKind.", ""),
        "name": cursor.spelling,
        "displayname": cursor.displayname,
        "qualified_name": qualified_name(cursor),
        "access": access_name(cursor),
        "line": cursor.location.line if cursor.location and cursor.location.file else None,
    }

    doc = clean_doc(getattr(cursor, "raw_comment", None))
    if doc:
        item["doc"] = doc

    if cursor.kind in RECORD_KINDS:
        item["base_classes"] = base_classes(cursor)
        if hasattr(cursor, "is_abstract_record"):
            item["abstract"] = cursor.is_abstract_record()

    if cursor.kind in {cindex.CursorKind.CLASS_TEMPLATE, cindex.CursorKind.FUNCTION_TEMPLATE}:
        template_parameters: list[dict[str, Any]] = []
        for child in cursor.get_children():
            if child.kind == cindex.CursorKind.TEMPLATE_TYPE_PARAMETER:
                template_parameters.append({"name": child.spelling, "kind": "type"})
            elif child.kind == cindex.CursorKind.TEMPLATE_NON_TYPE_PARAMETER:
                template_parameters.append(
                    {
                        "name": child.spelling,
                        "kind": "non_type",
                        "type": type_info(child.type),
                    }
                )
        if template_parameters:
            item["template_parameters"] = template_parameters

    if cursor.kind == cindex.CursorKind.FIELD_DECL:
        item["type"] = type_info(cursor.type)
        try:
            item["readonly"] = cursor.type.is_const_qualified()
        except Exception:
            pass

    if cursor.kind in FUNCTION_KINDS:
        item["parameters"] = parameters(cursor)
        if cursor.kind != cindex.CursorKind.CONSTRUCTOR:
            item["return_type"] = type_info(cursor.result_type)
        if hasattr(cursor, "is_static_method"):
            item["static"] = cursor.is_static_method()
        if cursor.kind == cindex.CursorKind.CXX_METHOD and hasattr(cursor, "is_const_method"):
            item["const"] = cursor.is_const_method()
        if cursor.kind == cindex.CursorKind.CONSTRUCTOR:
            if hasattr(cursor, "is_copy_constructor"):
                item["copy_constructor"] = cursor.is_copy_constructor()
            if hasattr(cursor, "is_move_constructor"):
                item["move_constructor"] = cursor.is_move_constructor()
        tokens = [token.spelling for token in cursor.get_tokens()]
        if "delete" in tokens:
            item["deleted"] = True

    if cursor.kind == cindex.CursorKind.ENUM_DECL:
        item["scoped"] = cursor.is_scoped_enum()
        item["type"] = type_info(cursor.enum_type)

    if cursor.kind == cindex.CursorKind.ENUM_CONSTANT_DECL:
        item["value"] = cursor.enum_value

    if cursor.kind == cindex.CursorKind.VAR_DECL:
        item["type"] = type_info(cursor.type)
        tokens = [token.spelling for token in cursor.get_tokens()]
        if "constexpr" in tokens or "const" in tokens:
            item["readonly"] = True

    if cursor.kind in {cindex.CursorKind.TYPEDEF_DECL, cindex.CursorKind.TYPE_ALIAS_DECL}:
        item["type"] = type_info(cursor.underlying_typedef_type)

    children: list[dict[str, Any]] = []
    for child in cursor.get_children():
        parsed = node_to_dict(child, target_file, include_non_public)
        if parsed is not None:
            if isinstance(parsed, list):
                children.extend(parsed)
            else:
                children.append(parsed)
    if children:
        item["children"] = children

    return item


def parse_umbrella(
    index: cindex.Index,
    headers: list[Path],
    include_dir: Path,
    standard: str,
    include_non_public: bool,
    libclang_path: str = "",
) -> tuple[dict[Path, list[dict[str, Any]]], list[str]]:
    args = [
        f"-std={standard}",
        "-x",
        "c++",
        f"-I{include_dir}",
        "-Wno-macro-redefined",
        *[f"-D{macro}=" for macro in IGNORED_MACROS],
        *[f"-D{macro}" for macro in EXTRA_DEFINES],
    ]

    sysroot = system_sysroot()
    if sysroot:
        args.append("-isysroot")
        args.append(sysroot)
        res_dir = clang_resource_dir(libclang_path)
        if res_dir:
            args.append("-resource-dir")
            args.append(res_dir)

    umbrella_name = str(include_dir.parent / "__sfml_api_umbrella.hpp")
    umbrella_content = "\n".join(
        f'#include "{header.relative_to(include_dir).as_posix()}"' for header in headers
    )
    header_set = {header.resolve() for header in headers}
    declaration_owners: dict[Path, Path] = {header: header for header in header_set}
    for header in headers:
        public_key = header.relative_to(include_dir).as_posix()
        for companion_name in DECLARATION_COMPANIONS.get(public_key, ()):
            companion = (include_dir / companion_name).resolve()
            if not companion.is_file():
                raise FileNotFoundError(
                    f"missing declaration companion for {public_key}: {companion}"
                )
            declaration_owners[companion] = header.resolve()

    tu = index.parse(
        umbrella_name,
        args=args,
        unsaved_files=[(umbrella_name, umbrella_content)],
        options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
        | cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES,
    )
    diagnostics = [str(diag) for diag in tu.diagnostics]

    declarations_by_file: dict[Path, list[dict[str, Any]]] = {header.resolve(): [] for header in headers}
    for child in tu.cursor.get_children():
        source = cursor_file(child)
        owner = declaration_owners.get(source)
        if owner is None:
            continue
        parsed = node_to_dict(child, source, include_non_public)
        if parsed is not None:
            if isinstance(parsed, list):
                declarations_by_file[owner].extend(parsed)
            else:
                declarations_by_file[owner].append(parsed)
    return declarations_by_file, diagnostics


def collect_headers(include_dir: Path, modules: tuple[str, ...], include_excluded: bool) -> list[Path]:
    headers: list[Path] = []
    for module in modules:
        module_dir = include_dir / "SFML" / module
        if not module_dir.is_dir():
            continue
        excluded = EXCLUDED_HEADERS.get(module, set())
        for header in sorted(module_dir.rglob("*.hpp")):
            if not include_excluded and header.name in excluded:
                continue
            headers.append(header)
    return sorted(headers)


def build_api(args: argparse.Namespace) -> dict[str, Any]:
    project_root = Path(args.project_root).resolve()
    include_dir = (project_root / args.include_dir).resolve()
    modules = tuple(args.modules.split(",")) if args.modules else DEFAULT_MODULES
    headers = collect_headers(include_dir, modules, args.include_excluded_headers)

    index = cindex.Index.create()
    files: list[dict[str, Any]] = []
    diagnostics_by_file: dict[str, list[str]] = {}
    declarations_by_file, diagnostics = parse_umbrella(
        index=index,
        headers=headers,
        include_dir=include_dir,
        standard=args.standard,
        include_non_public=args.include_non_public,
        libclang_path=args.libclang,
    )

    for header in headers:
        relative_header = header.relative_to(project_root).as_posix()
        files.append(
            {
                "path": relative_header,
                "module": header.relative_to(include_dir / "SFML").parts[0],
                "declarations": declarations_by_file.get(header.resolve(), []),
            }
        )
    if diagnostics:
        diagnostics_by_file["__sfml_api_umbrella.hpp"] = diagnostics

    return {
        "schema_version": 1,
        "generator": Path(__file__).name,
        "platform": platform.platform(),
        "libclang": args.libclang,
        "include_dir": include_dir.relative_to(project_root).as_posix(),
        "standard": args.standard,
        "modules": list(modules),
        "excluded_headers": {module: sorted(headers) for module, headers in EXCLUDED_HEADERS.items()},
        "header_count": len(headers),
        "files": files,
        "diagnostics": diagnostics_by_file,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract SFML public C++ API declarations to JSON.")
    parser.add_argument("--project-root", default=Path(__file__).resolve().parents[1])
    parser.add_argument("--include-dir", default="third_party/SFML/include")
    parser.add_argument("--output", default="output/sfml_api.json")
    parser.add_argument("--libclang", default=default_libclang_path())
    parser.add_argument("--standard", default="c++20")
    parser.add_argument("--modules", default=",".join(DEFAULT_MODULES))
    parser.add_argument(
        "--include-non-public",
        action="store_true",
        help="Keep protected/private class members in the JSON with access labels.",
    )
    parser.add_argument(
        "--include-excluded-headers",
        action="store_true",
        help="Also parse headers excluded by the PySF reference configuration.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = Path(args.project_root).resolve()
    configure_libclang(args.libclang)

    api = build_api(args)
    output = (project_root / args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(api, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Wrote {api['header_count']} headers to {output}")
    if api["diagnostics"]:
        print(f"Clang produced diagnostics for {len(api['diagnostics'])} headers.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
