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

if [ ! -f "third_party/Lua/src/lua.h" ]; then
    echo "Missing third_party/Lua. Run sh init.sh first." >&2
    exit 1
fi

if [ ! -f "third_party/sol2/include/sol2/sol.hpp" ]; then
    echo "Missing third_party/sol2. Run sh init.sh first." >&2
    exit 1
fi

apply_sol2_pr1606_patch() {
    echo "Applying sol2 PR #1606 patch if needed..."
    patch_file="$SCRIPT_DIR/cmake/sol/pr1606.patch"
    if git apply --reverse --check --directory=third_party/sol2 -p1 "$patch_file" >/dev/null 2>&1; then
        echo "PR #1606 patch already applied to sol2."
        return
    fi
    git apply --check --directory=third_party/sol2 -p1 "$patch_file"
    git apply --directory=third_party/sol2 -p1 "$patch_file"
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

echo "Building embedded LuaSF, Lua extension, host luac, and Lua stub from output CMake project..."
cmake --build output/build --config "$BUILD_CONFIG" --target LuaSF_build_outputs --parallel 1

EMBEDDED_MODULE_FILE=$(find output/build/bin -path "*/embedded/*" \( -type f -o -type l \) \( -name 'LuaSF.dll' -o -name 'LuaSF.dylib' -o -name 'LuaSF.so' \) 2>/dev/null | head -n 1 || true)
EXTENSION_MODULE_FILE=$(find output/build/bin -path "*/extension/*" \( -type f -o -type l \) \( -name 'LuaSF.dll' -o -name 'LuaSF.so' \) 2>/dev/null | head -n 1 || true)

echo
echo "Done."
echo "Project: $SCRIPT_DIR/output"
if [ -n "$EMBEDDED_MODULE_FILE" ]; then
    echo "Embedded module: $SCRIPT_DIR/$EMBEDDED_MODULE_FILE"
else
    echo "Embedded module: output/build/bin/$BUILD_CONFIG/embedded"
fi
if [ -n "$EXTENSION_MODULE_FILE" ]; then
    echo "Lua extension: $SCRIPT_DIR/$EXTENSION_MODULE_FILE"
else
    echo "Lua extension: output/build/bin/$BUILD_CONFIG/extension"
fi
echo "Stub: $SCRIPT_DIR/output/build/LuaSF.d.lua"
