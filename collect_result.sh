#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

OUTPUT_DIR="$SCRIPT_DIR/output"
BUILD_DIR="$OUTPUT_DIR/build"
RESULT_DIR="$OUTPUT_DIR/result"
CONFIG=${1:-}

if [ ! -d "$BUILD_DIR" ]; then
    echo "Missing output/build. Run sh build.sh first." >&2
    exit 1
fi

if [ -z "$CONFIG" ]; then
    CONFIG=$(cmake -N -LA "$BUILD_DIR" 2>/dev/null | sed -n 's/^LUASF_DEFAULT_CONFIG:[^=]*=//p' | head -n 1 || true)
fi

if [ -z "$CONFIG" ]; then
    CONFIG=Release
fi

BIN_DIR="$BUILD_DIR/bin/$CONFIG"
LIB_DIR="$BUILD_DIR/lib/$CONFIG"
if [ ! -d "$BIN_DIR" ]; then
    BIN_DIR="$BUILD_DIR/bin"
fi
if [ ! -d "$LIB_DIR" ]; then
    LIB_DIR="$BUILD_DIR/lib"
fi

STUB_FILE="$BUILD_DIR/LuaSF.lua"

find_module_file() {
    find "$BIN_DIR" -maxdepth 1 \( -type f -o -type l \) \( -name 'LuaSF.dll' -o -name 'LuaSF.dylib' -o -name 'LuaSF.so' \) 2>/dev/null | head -n 1
}

MODULE_FILE=$(find_module_file || true)
if [ -z "$MODULE_FILE" ]; then
    echo "Missing LuaSF dynamic library under $BIN_DIR." >&2
    echo "Run sh build.sh $CONFIG first, or pass the built config to this script." >&2
    exit 1
fi

if [ ! -f "$STUB_FILE" ]; then
    echo "Missing Lua stub $STUB_FILE." >&2
    echo "Run sh build.sh $CONFIG first." >&2
    exit 1
fi

copy_tree_contents() {
    src=$1
    dst=$2

    if [ -d "$src" ]; then
        mkdir -p "$dst"
        cp -R "$src"/. "$dst"/
    fi
}

copy_runtime_files() {
    src=$1
    dst=$2

    if [ ! -d "$src" ]; then
        return
    fi

    find "$src" -maxdepth 1 \( -type f -o -type l \) \
        \( -name '*.dll' -o -name '*.dylib' -o -name '*.so' -o -name '*.so.*' \) \
        -exec cp -L {} "$dst"/ \;
}

write_lua_compat_header() {
    header=$1
    cat > "$RESULT_DIR/include/$header" << EOF
#include "lua/$header"
#undef LUA_VERSION_NUM
#define LUA_VERSION_NUM 504
#undef lua_newstate
#define lua_newstate(f, ud) lua_newstate((f), (ud), 0u)
EOF
}

echo "Collecting LuaSF build result..."
echo "Config: $CONFIG"
echo "Source: $BUILD_DIR"
echo "Result: $RESULT_DIR"

rm -rf "$RESULT_DIR"
mkdir -p "$RESULT_DIR/bin" "$RESULT_DIR/include" "$RESULT_DIR/stub"

copy_runtime_files "$BIN_DIR" "$RESULT_DIR/bin"
copy_runtime_files "$LIB_DIR" "$RESULT_DIR/bin"

if [ ! -f "$RESULT_DIR/bin/$(basename "$MODULE_FILE")" ]; then
    cp "$MODULE_FILE" "$RESULT_DIR/bin/"
fi

cp "$STUB_FILE" "$RESULT_DIR/stub/"

copy_tree_contents "$OUTPUT_DIR/include" "$RESULT_DIR/include"
copy_tree_contents "$BUILD_DIR/generated_include/sol" "$RESULT_DIR/include/sol"
copy_tree_contents "$OUTPUT_DIR/third_party/SFML/include" "$RESULT_DIR/include"
copy_tree_contents "$OUTPUT_DIR/third_party/sol2/include" "$RESULT_DIR/include"

if [ -d "$OUTPUT_DIR/third_party/Lua" ]; then
    mkdir -p "$RESULT_DIR/include/lua"
    cp "$OUTPUT_DIR"/third_party/Lua/*.h "$RESULT_DIR/include/lua/"
    for header in "$OUTPUT_DIR"/third_party/Lua/*.hpp; do
        [ -e "$header" ] || continue
        cp "$header" "$RESULT_DIR/include/lua/"
    done
    write_lua_compat_header lua.h
    write_lua_compat_header lauxlib.h
    write_lua_compat_header lualib.h
fi

if [ -d "$LIB_DIR" ]; then
    mkdir -p "$RESULT_DIR/lib"
    find "$LIB_DIR" -maxdepth 1 \( -type f -o -type l \) \
        \( -name '*.lib' -o -name '*.exp' -o -name '*.a' \) \
        -exec cp -L {} "$RESULT_DIR/lib"/ \;
    rmdir "$RESULT_DIR/lib" 2>/dev/null || true
fi

mkdir -p "$RESULT_DIR/cmake"
cp "cmake/result_CMakeLists.txt" "$RESULT_DIR/CMakeLists.txt"
cp "cmake/LuaSFTargets.cmake" "$RESULT_DIR/cmake/LuaSFTargets.cmake"
cp "cmake/result_README.md" "$RESULT_DIR/README.md"

{
    echo "LuaSF result package"
    echo "===================="
    echo "Config: $CONFIG"
    echo "Generated from: $BUILD_DIR"
    echo
    echo "bin:"
    ls -1 "$RESULT_DIR/bin"
    echo
    echo "stub:"
    ls -1 "$RESULT_DIR/stub"
    echo
    echo "include:"
    echo "- LuaSF generated headers from output/include"
    echo "- SFML public headers"
    echo "- sol2 public headers"
    echo "- Lua headers under include/lua"
    echo "- CMake generated sol compatibility headers under include/sol"
    echo "- Lua compatibility wrappers at include/lua.h, include/lauxlib.h, include/lualib.h"
    if [ -d "$RESULT_DIR/lib" ]; then
        echo
        echo "lib:"
        ls -1 "$RESULT_DIR/lib"
    fi
    echo
    echo "cmake:"
    echo "- add_subdirectory(path/to/result)"
    echo "- target_link_libraries(your_target PRIVATE LuaSF::LuaSF)"
    echo "- luasf_copy_runtime_files(your_target)"
} > "$RESULT_DIR/manifest.txt"

echo
echo "Done."
echo "Result folder: $RESULT_DIR"
echo "Runtime libraries: $RESULT_DIR/bin"
echo "Stub: $RESULT_DIR/stub/LuaSF.lua"
echo "Headers: $RESULT_DIR/include"
