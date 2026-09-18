#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

CONFIG_OVERRIDE=${1:-}
PYTHON_EXE=".venv/bin/python"

if [ "$#" -gt 2 ]; then
    echo "Usage: sh build.sh [Debug|Release|RelWithDebInfo|MinSizeRel] [ME|ME-OH]" >&2
    exit 2
fi

if [ ! -x "$PYTHON_EXE" ]; then
    echo "Missing .venv. Run sh init.sh first." >&2
    exit 1
fi

SFML_TAG=
SFML_ME_TAG=
SFML_ME_OH_TAG=

while IFS='=' read -r key value; do
    value=$(printf '%s' "$value" | tr -d '\r')
    case "$key" in
        ''|'#'*) continue ;;
        SFML_TAG) SFML_TAG=$value ;;
        SFML_ME_TAG) SFML_ME_TAG=$value ;;
        SFML_ME_OH_TAG) SFML_ME_OH_TAG=$value ;;
    esac
done < versions.conf

case "${2:-}" in
    '')
        SFML_VARIANT=
        SFML_VARIANT_TAG=$SFML_TAG
        ;;
    ME|me)
        SFML_VARIANT=ME
        SFML_VARIANT_TAG=$SFML_ME_TAG
        ;;
    ME-OH|me-oh)
        SFML_VARIANT=ME-OH
        SFML_VARIANT_TAG=$SFML_ME_OH_TAG
        ;;
    *)
        echo "Unknown SFML variant '$2'. Use ME or ME-OH." >&2
        exit 2
        ;;
esac

if [ ! -f "third_party/SFML/CMakeLists.txt" ]; then
    echo "Missing third_party/SFML. Run sh init.sh ${SFML_VARIANT:-} first." >&2
    exit 1
fi

if [ "$(cat "third_party/SFML/.luasf-sfml-tag" 2>/dev/null || true)" != "$SFML_VARIANT_TAG" ]; then
    echo "third_party/SFML is not the ${SFML_VARIANT:-default} ($SFML_VARIANT_TAG) checkout." >&2
    echo "Run sh init.sh ${SFML_VARIANT:-} first." >&2
    exit 1
fi

if [ -z "$SFML_TAG" ] || [ -z "$SFML_ME_TAG" ] || [ -z "$SFML_ME_OH_TAG" ]; then
    echo "Missing SFML entries in versions.conf." >&2
    exit 1
fi

for dependency in "third_party/Lua/src/lua.h" "third_party/sol2/include/sol2/sol.hpp"; do
    if [ ! -f "$dependency" ]; then
        echo "Missing $dependency. Run sh init.sh ${SFML_VARIANT:-} first." >&2
        exit 1
    fi
done

apply_sol2_pr1606_patch() {
    echo "Applying sol2 PR #1606 patch if needed..."
    patch_file="$SCRIPT_DIR/cmake/sol/pr1606.patch"
    # The published sol2 headers use CRLF line endings, so the patch has to
    # ignore whitespace to match.
    if git apply --ignore-whitespace --reverse --check --directory=third_party/sol2 -p1 "$patch_file" >/dev/null 2>&1; then
        echo "PR #1606 patch already applied to sol2."
        return
    fi
    git apply --ignore-whitespace --check --directory=third_party/sol2 -p1 "$patch_file"
    git apply --ignore-whitespace --directory=third_party/sol2 -p1 "$patch_file"
}

apply_sol2_pr1606_patch

VARIANT_FILE="$SCRIPT_DIR/.luasf-sfml-variant"
PREVIOUS_VARIANT=$(cat "$VARIANT_FILE" 2>/dev/null || true)
if [ "$PREVIOUS_VARIANT" != "$SFML_VARIANT" ]; then
    echo "SFML variant changed; discarding the previous output/build directory."
    rm -rf output/build
fi

echo "Extracting SFML public API..."
"$PYTHON_EXE" tools/extract_sfml_api.py --include-dir third_party/SFML/include

echo "Generating sol2 bindings..."
"$PYTHON_EXE" tools/generate_sol2_bindings.py

echo "Generating standalone output CMake project..."
"$PYTHON_EXE" tools/generate_build_files.py --force-sort

printf '%s' "$SFML_VARIANT" > "$VARIANT_FILE"

SFML_ROOT="$SCRIPT_DIR/third_party/SFML"
LUA_ROOT="$SCRIPT_DIR/third_party/Lua"
SOL2_ROOT="$SCRIPT_DIR/third_party/sol2"

echo "Configuring output CMake project..."
set -- \
    -DLUASF_SFML_ROOT="$SFML_ROOT" \
    -DLUASF_LUA_ROOT="$LUA_ROOT" \
    -DLUASF_SOL2_ROOT="$SOL2_ROOT"
if [ -n "$CONFIG_OVERRIDE" ]; then
    set -- "$@" \
        -DLUASF_DEFAULT_CONFIG="$CONFIG_OVERRIDE" \
        -DCMAKE_BUILD_TYPE="$CONFIG_OVERRIDE"
fi
cmake -S output -B output/build "$@"

BUILD_CONFIG=$(cmake -N -LA output/build 2>/dev/null | sed -n 's/^LUASF_DEFAULT_CONFIG:[^=]*=//p' | head -n 1)
if [ -z "$BUILD_CONFIG" ]; then
    echo "Failed to read LUASF_DEFAULT_CONFIG from CMake cache." >&2
    exit 1
fi

echo "Building embedded LuaSF, host luac, and Lua stub from output CMake project..."
cmake --build output/build --config "$BUILD_CONFIG" --target LuaSF_build_outputs --parallel 1

EMBEDDED_MODULE_FILE=$(find output/build/bin -path "*/embedded/*" \( -type f -o -type l \) \( -name 'LuaSF.dll' -o -name 'LuaSF.dylib' -o -name 'LuaSF.so' \) 2>/dev/null | head -n 1 || true)

echo
echo "Done."
echo "Project: $SCRIPT_DIR/output"
echo "SFML variant: ${SFML_VARIANT:-default} ($SFML_VARIANT_TAG)"
if [ -n "$EMBEDDED_MODULE_FILE" ]; then
    echo "Embedded module: $SCRIPT_DIR/$EMBEDDED_MODULE_FILE"
else
    echo "Embedded module: output/build/bin/$BUILD_CONFIG/embedded"
fi
echo "Stub: $SCRIPT_DIR/output/build/LuaSF.d.lua"
