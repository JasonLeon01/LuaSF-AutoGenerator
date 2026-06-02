from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
ORDER_CACHE_VERSION = 3
MODULE_ORDER = {"System": 0, "Window": 1, "Graphics": 2, "Audio": 3, "Network": 4}
TYPE_DECL_KINDS = {"CLASS_DECL", "STRUCT_DECL", "ENUM_DECL", "TYPEDEF_DECL", "TYPE_ALIAS_DECL"}

MANUAL_DEPENDENCIES = {
    "bind_Vector": {"bind_Angle", "bind_Color"},
    "bind_Rect": {"bind_Vector"},
    "bind_Matrix": {"bind_Transform"},
    "bind_Event": {"bind_Vector", "bind_Joystick", "bind_Keyboard", "bind_Mouse", "bind_Sensor"},
    "bind_Handle": set(),
    "bind_Drawable": set(),
}

MANUAL_TYPE_OWNERS = {
    "sf::Drawable": "bind_Drawable",
    "sf::Event": "bind_Event",
    "sf::Event::Closed": "bind_Event",
    "sf::Event::Resized": "bind_Event",
    "sf::Event::FocusLost": "bind_Event",
    "sf::Event::FocusGained": "bind_Event",
    "sf::Event::TextEntered": "bind_Event",
    "sf::Event::KeyPressed": "bind_Event",
    "sf::Event::KeyReleased": "bind_Event",
    "sf::Event::MouseWheelScrolled": "bind_Event",
    "sf::Event::MouseButtonPressed": "bind_Event",
    "sf::Event::MouseButtonReleased": "bind_Event",
    "sf::Event::MouseMoved": "bind_Event",
    "sf::Event::MouseMovedRaw": "bind_Event",
    "sf::Event::MouseEntered": "bind_Event",
    "sf::Event::MouseLeft": "bind_Event",
    "sf::Event::JoystickButtonPressed": "bind_Event",
    "sf::Event::JoystickButtonReleased": "bind_Event",
    "sf::Event::JoystickMoved": "bind_Event",
    "sf::Event::JoystickConnected": "bind_Event",
    "sf::Event::JoystickDisconnected": "bind_Event",
    "sf::Event::TouchBegan": "bind_Event",
    "sf::Event::TouchMoved": "bind_Event",
    "sf::Event::TouchEnded": "bind_Event",
    "sf::Event::SensorChanged": "bind_Event",
    "sf::FloatRect": "bind_Rect",
    "sf::IntRect": "bind_Rect",
    "sf::Rect": "bind_Rect",
    "sf::Rect<float>": "bind_Rect",
    "sf::Rect<int>": "bind_Rect",
    "sf::Vector2": "bind_Vector",
    "sf::Vector2<bool>": "bind_Vector",
    "sf::Vector2<float>": "bind_Vector",
    "sf::Vector2<int>": "bind_Vector",
    "sf::Vector2<unsigned int>": "bind_Vector",
    "sf::Vector2b": "bind_Vector",
    "sf::Vector2f": "bind_Vector",
    "sf::Vector2i": "bind_Vector",
    "sf::Vector2u": "bind_Vector",
    "sf::Vector3": "bind_Vector",
    "sf::Vector3<bool>": "bind_Vector",
    "sf::Vector3<float>": "bind_Vector",
    "sf::Vector3<int>": "bind_Vector",
    "sf::Vector3<unsigned int>": "bind_Vector",
    "sf::Vector3b": "bind_Vector",
    "sf::Vector3f": "bind_Vector",
    "sf::Vector3i": "bind_Vector",
    "sf::Vector3u": "bind_Vector",
    "sf::Glsl::Bvec2": "bind_Vector",
    "sf::Glsl::Bvec3": "bind_Vector",
    "sf::Glsl::Bvec4": "bind_Vector",
    "sf::Glsl::Ivec2": "bind_Vector",
    "sf::Glsl::Ivec3": "bind_Vector",
    "sf::Glsl::Ivec4": "bind_Vector",
    "sf::Glsl::Vec2": "bind_Vector",
    "sf::Glsl::Vec3": "bind_Vector",
    "sf::Glsl::Vec4": "bind_Vector",
    "sf::priv::Vector4": "bind_Vector",
    "sf::priv::Vector4<bool>": "bind_Vector",
    "sf::priv::Vector4<float>": "bind_Vector",
    "sf::priv::Vector4<int>": "bind_Vector",
    "sf::Glsl::Mat3": "bind_Matrix",
    "sf::Glsl::Mat4": "bind_Matrix",
    "sf::priv::Matrix": "bind_Matrix",
    "sf::priv::Matrix<3, 3>": "bind_Matrix",
    "sf::priv::Matrix<4, 4>": "bind_Matrix",
    "sf::WindowHandle": "bind_Handle",
    "WindowHandle": "bind_Handle",
}


@dataclass(frozen=True)
class BindingEntry:
    name: str
    include_path: str
    source_path: str
    header_path: str
    module: str
    manual: bool
    order_hint: int


def clean_cpp_type(value: str) -> str:
    value = (value or "").strip()
    value = re.sub(r"\s+", " ", value)
    value = value.replace(" &", "&").replace("& ", "&")
    value = value.replace(" *", "*").replace("* ", "*")
    value = value.replace("< ", "<").replace(" >", ">")
    value = value.replace(" ,", ",")
    return value


def rel(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_support_sources(project_root: Path, output_root: Path) -> None:
    include_out = output_root / "include"
    src_out = output_root / "src"
    tools_out = output_root / "tools"
    include_out.mkdir(parents=True, exist_ok=True)
    src_out.mkdir(parents=True, exist_ok=True)
    tools_out.mkdir(parents=True, exist_ok=True)

    for path in include_out.glob("*"):
        if path.is_file() and (path.name in {"utils.hpp", "utils.inl"} or path.name.startswith("bind_")):
            path.unlink()
    for path in src_out.glob("bind_*.cpp"):
        if path.is_file():
            path.unlink()

    for path in sorted((project_root / "include").glob("*")):
        if path.is_file() and path.suffix in {".hpp", ".inl"}:
            copy_file(path, include_out / path.name)

    for path in sorted((project_root / "src").glob("*.cpp")):
        copy_file(path, src_out / path.name)

    stub_dumper = project_root / "tools" / "lua_stub_dump.cpp"
    if stub_dumper.exists():
        copy_file(stub_dumper, tools_out / stub_dumper.name)


def copy_dependencies(project_root: Path, output_root: Path) -> None:
    source_root = project_root / "third_party"
    dest_root = output_root / "third_party"
    dest_root.mkdir(parents=True, exist_ok=True)

    ignore = shutil.ignore_patterns(
        ".git",
        ".github",
        ".vs",
        "__pycache__",
        "build",
        "out",
        "cmake-build-*",
    )

    for name in ("SFML", "Lua", "sol2"):
        src = source_root / name
        dst = dest_root / name
        if not src.exists():
            raise FileNotFoundError(f"missing dependency directory: {src}")
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src, dst, ignore=ignore)


def load_api(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def discover_entries(api: dict[str, Any], project_root: Path, output_root: Path) -> dict[str, BindingEntry]:
    entries: dict[str, BindingEntry] = {}

    manual_header_root = output_root / "include"
    manual_source_root = output_root / "src"
    for index, header in enumerate(sorted(manual_header_root.glob("bind_*.hpp"))):
        source = manual_source_root / f"{header.stem}.cpp"
        if not source.exists():
            continue
        entries[header.stem] = BindingEntry(
            name=header.stem,
            include_path=header.name,
            source_path=rel(source, output_root),
            header_path=rel(header, output_root),
            module="manual",
            manual=True,
            order_hint=index,
        )

    for index, file_item in enumerate(api.get("files", [])):
        module = file_item["module"]
        stem = Path(file_item["path"]).stem
        name = f"bind_{stem}"
        if name in entries:
            continue
        header = output_root / "include" / module / f"{name}.hpp"
        source = output_root / "src" / module / f"{name}.cpp"
        if not header.exists() or not source.exists():
            continue
        entries[name] = BindingEntry(
            name=name,
            include_path=f"{module}/{name}.hpp",
            source_path=rel(source, output_root),
            header_path=rel(header, output_root),
            module=module,
            manual=False,
            order_hint=index,
        )

    return entries


def walk_declarations(items: list[dict[str, Any]]):
    for item in items:
        yield item
        yield from walk_declarations(item.get("children", []))


def build_type_owner_map(api: dict[str, Any], entries: dict[str, BindingEntry]) -> dict[str, str]:
    owners: dict[str, str] = {
        type_name: owner for type_name, owner in MANUAL_TYPE_OWNERS.items() if owner in entries
    }

    for file_item in api.get("files", []):
        name = f"bind_{Path(file_item['path']).stem}"
        if name not in entries:
            continue
        for item in walk_declarations(file_item.get("declarations", [])):
            if item.get("kind") not in TYPE_DECL_KINDS:
                continue
            qualified_name = item.get("qualified_name") or item.get("name")
            if qualified_name:
                owners.setdefault(clean_cpp_type(qualified_name), name)

    return owners


def is_pointer_or_reference(type_text: str) -> bool:
    return "*" in type_text or "&" in type_text


def type_strings(item: dict[str, Any]) -> list[str]:
    values: list[str] = []

    for base in item.get("base_classes", []):
        if base.get("name"):
            base_name = base["name"]
            values.append(base_name)
            if "::" not in base_name:
                parent_qn = item.get("qualified_name", "")
                if "::" in parent_qn:
                    ns = parent_qn.rsplit("::", 1)[0]
                    values.append(f"{ns}::{base_name}")

    type_info = item.get("type")
    if type_info:
        values.extend([type_info.get("spelling", ""), type_info.get("canonical", "")])

    return_type = item.get("return_type")
    if return_type:
        values.extend([return_type.get("spelling", ""), return_type.get("canonical", "")])

    for param in item.get("parameters", []):
        type_info = param.get("type") or {}
        spelling = clean_cpp_type(type_info.get("spelling", ""))
        canonical = clean_cpp_type(type_info.get("canonical", ""))
        if not is_pointer_or_reference(spelling) and not is_pointer_or_reference(canonical):
            values.extend([spelling, canonical])

    return [clean_cpp_type(value) for value in values if value]


def contains_type(type_text: str, type_name: str) -> bool:
    if not type_text or not type_name:
        return False

    if "<" in type_name:
        return type_name.replace(" ", "") in type_text.replace(" ", "")

    pattern = rf"(?<![A-Za-z0-9_:]){re.escape(type_name)}(?![A-Za-z0-9_:])"
    return re.search(pattern, type_text) is not None


def build_dependency_graph(
    api: dict[str, Any],
    entries: dict[str, BindingEntry],
    owners: dict[str, str],
) -> dict[str, set[str]]:
    graph: dict[str, set[str]] = {name: set() for name in entries}
    sorted_type_owners = sorted(owners.items(), key=lambda item: len(item[0]), reverse=True)

    for name, deps in MANUAL_DEPENDENCIES.items():
        if name not in graph:
            continue
        graph[name].update(dep for dep in deps if dep in graph and dep != name)

    for file_item in api.get("files", []):
        name = f"bind_{Path(file_item['path']).stem}"
        if name not in graph:
            continue

        for item in walk_declarations(file_item.get("declarations", [])):
            for type_text in type_strings(item):
                for type_name, owner in sorted_type_owners:
                    if owner == name or owner not in graph:
                        continue
                    if contains_type(type_text, type_name):
                        graph[name].add(owner)

    return graph


def rank(entry: BindingEntry) -> tuple[int, int, int, str]:
    manual_rank = 0 if entry.manual else 1
    module_rank = MODULE_ORDER.get(entry.module, 99)
    return (manual_rank, module_rank, entry.order_hint, entry.name)


def topological_sort(entries: dict[str, BindingEntry], graph: dict[str, set[str]]) -> tuple[list[str], list[dict[str, Any]]]:
    remaining = {name: set(deps) for name, deps in graph.items()}
    order: list[str] = []
    cycle_breaks: list[dict[str, Any]] = []

    while remaining:
        ready = [name for name, deps in remaining.items() if not deps]
        if ready:
            current = min(ready, key=lambda name: rank(entries[name]))
        else:
            current = min(remaining, key=lambda name: rank(entries[name]))
            cycle_breaks.append({"entry": current, "remaining_dependencies": sorted(remaining[current])})

        order.append(current)
        remaining.pop(current)
        for deps in remaining.values():
            deps.discard(current)

    return order, cycle_breaks


def input_hash(api_path: Path, entries: dict[str, BindingEntry]) -> str:
    digest = hashlib.sha256()
    digest.update(f"schema={SCHEMA_VERSION};cache={ORDER_CACHE_VERSION}".encode())
    digest.update(api_path.read_bytes())
    for entry in sorted(entries.values(), key=lambda item: item.name):
        digest.update(
            json.dumps(
                {
                    "name": entry.name,
                    "include": entry.include_path,
                    "source": entry.source_path,
                    "header": entry.header_path,
                    "module": entry.module,
                    "manual": entry.manual,
                },
                sort_keys=True,
            ).encode()
        )
    return digest.hexdigest()


def load_cached_order(cache_path: Path, digest: str, entries: dict[str, BindingEntry]) -> list[str] | None:
    if not cache_path.exists():
        return None

    try:
        cache = json.loads(cache_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None

    order = cache.get("order")
    if cache.get("schema_version") != SCHEMA_VERSION:
        return None
    if cache.get("order_cache_version") != ORDER_CACHE_VERSION:
        return None
    if cache.get("input_hash") != digest:
        return None
    if not isinstance(order, list) or set(order) != set(entries):
        return None
    return order


def save_cached_order(
    cache_path: Path,
    digest: str,
    order: list[str],
    graph: dict[str, set[str]],
    cycle_breaks: list[dict[str, Any]],
) -> None:
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(
        json.dumps(
            {
                "schema_version": SCHEMA_VERSION,
                "order_cache_version": ORDER_CACHE_VERSION,
                "input_hash": digest,
                "order": order,
                "dependencies": {name: sorted(deps) for name, deps in sorted(graph.items())},
                "cycle_breaks": cycle_breaks,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )


def sorted_order(api_path: Path, cache_path: Path, api: dict[str, Any], entries: dict[str, BindingEntry], force_sort: bool) -> tuple[list[str], bool]:
    digest = input_hash(api_path, entries)
    if not force_sort:
        cached = load_cached_order(cache_path, digest, entries)
        if cached is not None:
            return cached, True

    owners = build_type_owner_map(api, entries)
    graph = build_dependency_graph(api, entries, owners)
    order, cycle_breaks = topological_sort(entries, graph)
    save_cached_order(cache_path, digest, order, graph, cycle_breaks)
    return order, False


def cmake_list(paths: list[str]) -> str:
    if not paths:
        return ""
    return "\n".join(f'    "{path}"' for path in paths)


def collect_header_paths(project_root: Path, entries: dict[str, BindingEntry], order: list[str]) -> list[str]:
    seen: set[str] = set()
    headers: list[str] = []

    for path in sorted((project_root / "include").glob("*.hpp")) + sorted((project_root / "include").glob("*.inl")):
        header = rel(path, project_root)
        headers.append(header)
        seen.add(header)

    for name in order:
        header = entries[name].header_path
        if header not in seen:
            headers.append(header)
            seen.add(header)

    return headers


def render_outputs(
    cmake_project_root: Path,
    order: list[str],
    entries: dict[str, BindingEntry],
    cmake_template: Path,
    source_template: Path,
    public_header_template: Path,
    cmake_output: Path,
    source_output: Path,
    public_header_output: Path,
    project_name: str,
    target_name: str,
    module_name: str,
    state_factory_name: str,
) -> None:
    sources = [entries[name].source_path for name in order]
    headers = collect_header_paths(cmake_project_root, entries, order)
    entry_source = rel(source_output, cmake_project_root)
    public_header = rel(public_header_output, cmake_project_root)
    includes = "\n".join(f'#include "{entries[name].include_path}"' for name in order)
    bind_calls = "\n".join(f"    {name}(lua);" for name in order)

    cmake_output.write_text(
        cmake_template.read_text(encoding="utf-8").format(
            project_name=project_name,
            target_name=target_name,
            module_name=module_name,
            binding_sources=cmake_list(sources),
            binding_headers=cmake_list(headers),
            entry_source=entry_source,
            public_header=public_header,
        ),
        encoding="utf-8",
    )

    public_header_output.parent.mkdir(parents=True, exist_ok=True)
    public_header_output.write_text(
        public_header_template.read_text(encoding="utf-8").format(
            module_name=module_name,
            state_factory_name=state_factory_name,
        ),
        encoding="utf-8",
    )

    source_output.parent.mkdir(parents=True, exist_ok=True)
    source_output.write_text(
        source_template.read_text(encoding="utf-8").format(
            module_name=module_name,
            state_factory_name=state_factory_name,
            includes=includes,
            bind_calls=bind_calls,
        ),
        encoding="utf-8",
    )

    legacy_main = source_output.parent / "main.cpp"
    if legacy_main.exists() and legacy_main.resolve() != source_output.resolve():
        legacy_main.unlink()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate CMakeLists.txt and LuaSF DLL entry files.")
    parser.add_argument("--api-json", default="output/sfml_api.json")
    parser.add_argument("--output-root", default="output")
    parser.add_argument("--cache", default="output/binding_order.json")
    parser.add_argument("--cmake-template", default="CMakeLists.txt.in")
    parser.add_argument("--source-template", "--main-template", default="LuaSF.cpp.in", dest="source_template")
    parser.add_argument("--header-template", default="LuaSF.hpp.in")
    parser.add_argument("--cmake-output", default="output/CMakeLists.txt")
    parser.add_argument("--source-output", "--main-output", default="output/LuaSF.cpp", dest="source_output")
    parser.add_argument("--header-output", default="output/include/LuaSF.hpp")
    parser.add_argument("--project-name", default="LuaSF")
    parser.add_argument("--target-name", default="LuaSF")
    parser.add_argument("--module-name", default="LuaSF")
    parser.add_argument("--state-factory-name", default="LuaSF_create_state")
    parser.add_argument("--copy-dependencies", dest="copy_dependencies", action="store_true", default=True)
    parser.add_argument("--no-copy-dependencies", dest="copy_dependencies", action="store_false")
    parser.add_argument("--force-sort", action="store_true", help="Ignore output/binding_order.json and recompute the order.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = Path.cwd().resolve()
    api_path = (project_root / args.api_json).resolve()
    output_root = (project_root / args.output_root).resolve()
    cache_path = (project_root / args.cache).resolve()

    api = load_api(api_path)
    copy_support_sources(project_root, output_root)
    if args.copy_dependencies:
        copy_dependencies(project_root, output_root)
    entries = discover_entries(api, project_root, output_root)
    order, used_cache = sorted_order(api_path, cache_path, api, entries, args.force_sort)
    render_outputs(
        cmake_project_root=output_root,
        order=order,
        entries=entries,
        cmake_template=(project_root / args.cmake_template).resolve(),
        source_template=(project_root / args.source_template).resolve(),
        public_header_template=(project_root / args.header_template).resolve(),
        cmake_output=(project_root / args.cmake_output).resolve(),
        source_output=(project_root / args.source_output).resolve(),
        public_header_output=(project_root / args.header_output).resolve(),
        project_name=args.project_name,
        target_name=args.target_name,
        module_name=args.module_name,
        state_factory_name=args.state_factory_name,
    )

    cache_note = "using cached order" if used_cache else "after topological sort"
    dependency_note = " with copied dependencies" if args.copy_dependencies else ""
    print(
        f"Generated output/CMakeLists.txt, output/LuaSF.cpp, and output/include/LuaSF.hpp "
        f"for {len(order)} binding units ({cache_note}){dependency_note}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
