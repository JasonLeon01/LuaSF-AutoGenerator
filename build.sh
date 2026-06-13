#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

CONFIG_OVERRIDE=${1:-}
PYTHON_EXE=".venv/bin/python"

if [ ! -x "$PYTHON_EXE" ]; then
    echo "Missing .venv. Run sh init.sh first." >&2
    exit 1
fi

if [ ! -f "third_party/SFML/CMakeLists.txt" ]; then
    echo "Missing third_party/SFML. Run sh init.sh first." >&2
    exit 1
fi

if [ ! -f "third_party/Lua/lua.h" ]; then
    echo "Missing third_party/Lua. Run sh init.sh first." >&2
    exit 1
fi

if [ ! -f "third_party/sol2/include/sol2/sol.hpp" ]; then
    echo "Missing third_party/sol2. Run sh init.sh first." >&2
    exit 1
fi

apply_sol2_pr1606_patch() {
    if [ "$(uname -s)" != "Darwin" ]; then
        echo "Skipping PR #1606 patch to sol2 (not macOS)."
        return
    fi

    target="third_party/sol2/include/sol2/sol.hpp"
    if grep -q "T& emplace(T& arg) noexcept" "$target"; then
        echo "PR #1606 patch already applied to sol2."
        return
    fi

    echo "Applying PR #1606 patch to sol2 (Clang 18+ optional::emplace fix)..."
    if ! command -v patch >/dev/null 2>&1; then
        echo "Missing 'patch'; cannot apply PR #1606 patch for sol2 on macOS." >&2
        exit 1
    fi
    patch -N --forward "$target" < "$SCRIPT_DIR/cmake/sol/pr1606.patch"
}

apply_sol2_pr1606_patch

echo "Extracting SFML public API..."
"$PYTHON_EXE" tools/extract_sfml_api.py

echo "Generating sol2 bindings..."
"$PYTHON_EXE" tools/generate_sol2_bindings.py

echo "Generating standalone output CMake project..."
"$PYTHON_EXE" tools/generate_build_files.py --force-sort

echo "Configuring output CMake project..."
if [ -n "$CONFIG_OVERRIDE" ]; then
    cmake -S output -B output/build \
        -DLUASF_DEFAULT_CONFIG="$CONFIG_OVERRIDE" \
        -DCMAKE_BUILD_TYPE="$CONFIG_OVERRIDE"
else
    cmake -S output -B output/build
fi

BUILD_CONFIG=$(cmake -N -LA output/build 2>/dev/null | sed -n 's/^LUASF_DEFAULT_CONFIG:[^=]*=//p' | head -n 1)
if [ -z "$BUILD_CONFIG" ]; then
    echo "Failed to read LUASF_DEFAULT_CONFIG from CMake cache." >&2
    exit 1
fi

echo "Building LuaSF and Lua stub from output CMake project..."
cmake --build output/build --config "$BUILD_CONFIG" --target LuaSF_lua_stub --parallel 1

MODULE_FILE=$(find output/build/bin \( -type f -o -type l \) \( -name 'LuaSF.dll' -o -name 'LuaSF.dylib' -o -name 'LuaSF.so' \) 2>/dev/null | head -n 1 || true)

echo
echo "Done."
echo "Project: $SCRIPT_DIR/output"
if [ -n "$MODULE_FILE" ]; then
    echo "Module: $SCRIPT_DIR/$MODULE_FILE"
else
    echo "Module: output/build/bin"
fi
echo "Stub: $SCRIPT_DIR/output/build/LuaSF.lua"
