#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

OUTPUT_DIR="$SCRIPT_DIR/output"
BUILD_DIR="$OUTPUT_DIR/build"
RESULT_DIR="$OUTPUT_DIR/result"
EMBEDDED_RESULT_DIR="$RESULT_DIR/embedded"
EXTENSION_RESULT_DIR="$RESULT_DIR/extension"
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
SFML_LIB_DIR="$BUILD_DIR/third_party/SFML/lib/$CONFIG"
if [ ! -d "$SFML_LIB_DIR" ]; then
    SFML_LIB_DIR="$BUILD_DIR/third_party/SFML/lib"
fi
EMBEDDED_BIN_DIR="$BUILD_DIR/bin/$CONFIG/embedded"
if [ ! -d "$EMBEDDED_BIN_DIR" ]; then
    EMBEDDED_BIN_DIR="$BUILD_DIR/bin/embedded/$CONFIG"
fi
if [ ! -d "$EMBEDDED_BIN_DIR" ]; then
    EMBEDDED_BIN_DIR="$BIN_DIR"
fi
EXTENSION_BIN_DIR="$BUILD_DIR/bin/$CONFIG/extension"
if [ ! -d "$EXTENSION_BIN_DIR" ]; then
    EXTENSION_BIN_DIR="$BUILD_DIR/bin/extension/$CONFIG"
fi
if [ ! -d "$EXTENSION_BIN_DIR" ]; then
    EXTENSION_BIN_DIR="$BUILD_DIR/bin/extension"
fi
EMBEDDED_LIB_DIR="$BUILD_DIR/lib/$CONFIG/embedded"
if [ ! -d "$EMBEDDED_LIB_DIR" ]; then
    EMBEDDED_LIB_DIR="$BUILD_DIR/lib/embedded/$CONFIG"
fi
if [ ! -d "$EMBEDDED_LIB_DIR" ]; then
    EMBEDDED_LIB_DIR="$LIB_DIR"
fi

STUB_FILE="$BUILD_DIR/LuaSF.d.lua"
LUAC_FILE=$(find "$BUILD_DIR/tools" -maxdepth 2 \( -type f -o -type l \) \( -name 'luac' -o -name 'luac.exe' \) 2>/dev/null | head -n 1 || true)

find_embedded_module_file() {
    find "$EMBEDDED_BIN_DIR" -maxdepth 1 \( -type f -o -type l \) \( -name 'LuaSF.dll' -o -name 'LuaSF.dylib' -o -name 'LuaSF.so' \) 2>/dev/null | head -n 1
}

find_extension_module_file() {
    find "$EXTENSION_BIN_DIR" -maxdepth 1 \( -type f -o -type l \) \( -name 'LuaSF.dll' -o -name 'LuaSF.so' \) 2>/dev/null | head -n 1
}

EMBEDDED_MODULE_FILE=$(find_embedded_module_file || true)
if [ -z "$EMBEDDED_MODULE_FILE" ]; then
    echo "Missing embedded LuaSF dynamic library under $EMBEDDED_BIN_DIR." >&2
    echo "Run sh build.sh $CONFIG first, or pass the built config to this script." >&2
    exit 1
fi

EXTENSION_MODULE_FILE=$(find_extension_module_file || true)
if [ -z "$EXTENSION_MODULE_FILE" ]; then
    echo "Missing Lua extension module under $EXTENSION_BIN_DIR." >&2
    echo "Run sh build.sh $CONFIG first, or pass the built config to this script." >&2
    exit 1
fi

if [ ! -f "$STUB_FILE" ]; then
    echo "Missing Lua stub $STUB_FILE." >&2
    echo "Run sh build.sh $CONFIG first." >&2
    exit 1
fi

if [ -z "$LUAC_FILE" ]; then
    echo "Missing host luac under $BUILD_DIR/tools." >&2
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

copy_extension_runtime_files() {
    src=$1
    dst=$2

    if [ ! -d "$src" ]; then
        return
    fi

    find "$src" -maxdepth 1 \( -type f -o -type l \) \
        \( -name '*.dll' -o -name '*.dylib' -o -name '*.so' -o -name '*.so.*' \) \
        ! -name 'lua.dll' \
        ! -name 'liblua*.dylib' \
        ! -name 'liblua*.so' \
        ! -name 'liblua*.so.*' \
        -exec cp -L {} "$dst"/ \;
}

write_lua_compat_header() {
    header=$1
    cat > "$EMBEDDED_RESULT_DIR/include/$header" << EOF
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
mkdir -p \
    "$EMBEDDED_RESULT_DIR/bin" \
    "$EMBEDDED_RESULT_DIR/include" \
    "$EMBEDDED_RESULT_DIR/stub" \
    "$EMBEDDED_RESULT_DIR/tools" \
    "$EXTENSION_RESULT_DIR/bin" \
    "$EXTENSION_RESULT_DIR/stub"

copy_runtime_files "$EMBEDDED_BIN_DIR" "$EMBEDDED_RESULT_DIR/bin"
copy_runtime_files "$EMBEDDED_LIB_DIR" "$EMBEDDED_RESULT_DIR/bin"
copy_extension_runtime_files "$EXTENSION_BIN_DIR" "$EXTENSION_RESULT_DIR/bin"

if [ ! -f "$EMBEDDED_RESULT_DIR/bin/$(basename "$EMBEDDED_MODULE_FILE")" ]; then
    cp "$EMBEDDED_MODULE_FILE" "$EMBEDDED_RESULT_DIR/bin/"
fi
if [ ! -f "$EXTENSION_RESULT_DIR/bin/$(basename "$EXTENSION_MODULE_FILE")" ]; then
    cp "$EXTENSION_MODULE_FILE" "$EXTENSION_RESULT_DIR/bin/"
fi

if [ -d "$SCRIPT_DIR/requirements" ]; then
    find "$SCRIPT_DIR/requirements" -maxdepth 1 -type f -name '*.dll' -exec cp {} "$EMBEDDED_RESULT_DIR/bin"/ \;
    find "$SCRIPT_DIR/requirements" -maxdepth 1 -type f -name '*.dll' -exec cp {} "$EXTENSION_RESULT_DIR/bin"/ \;
fi

cp "$STUB_FILE" "$EMBEDDED_RESULT_DIR/stub/"
cp "$STUB_FILE" "$EXTENSION_RESULT_DIR/stub/"
cp "$LUAC_FILE" "$EMBEDDED_RESULT_DIR/tools/"

copy_tree_contents "$OUTPUT_DIR/include" "$EMBEDDED_RESULT_DIR/include"
copy_tree_contents "$BUILD_DIR/generated_include/sol" "$EMBEDDED_RESULT_DIR/include/sol"
copy_tree_contents "$OUTPUT_DIR/third_party/SFML/include" "$EMBEDDED_RESULT_DIR/include"
copy_tree_contents "$OUTPUT_DIR/third_party/sol2/include" "$EMBEDDED_RESULT_DIR/include"

if [ -d "$OUTPUT_DIR/third_party/Lua/src" ]; then
    mkdir -p "$EMBEDDED_RESULT_DIR/include/lua"
    cp "$OUTPUT_DIR"/third_party/Lua/src/*.h "$EMBEDDED_RESULT_DIR/include/lua/"
    for header in "$OUTPUT_DIR"/third_party/Lua/src/*.hpp; do
        [ -e "$header" ] || continue
        cp "$header" "$EMBEDDED_RESULT_DIR/include/lua/"
    done
    write_lua_compat_header lua.h
    write_lua_compat_header lauxlib.h
    write_lua_compat_header lualib.h
fi

if [ -d "$EMBEDDED_LIB_DIR" ] || [ -d "$LIB_DIR" ] || [ -d "$SFML_LIB_DIR" ]; then
    mkdir -p "$EMBEDDED_RESULT_DIR/lib"
    for source_dir in "$EMBEDDED_LIB_DIR" "$LIB_DIR" "$SFML_LIB_DIR"; do
        [ -d "$source_dir" ] || continue
        find "$source_dir" -maxdepth 1 \( -type f -o -type l \) \
            \( -name '*.lib' -o -name '*.exp' -o -name '*.a' \) \
            -exec cp -L {} "$EMBEDDED_RESULT_DIR/lib"/ \;
    done
    rmdir "$EMBEDDED_RESULT_DIR/lib" 2>/dev/null || true
fi

mkdir -p "$EMBEDDED_RESULT_DIR/cmake"
cp "cmake/result_CMakeLists.txt" "$EMBEDDED_RESULT_DIR/CMakeLists.txt"
cp "cmake/LuaSFTargets.cmake" "$EMBEDDED_RESULT_DIR/cmake/LuaSFTargets.cmake"
cp "cmake/result_README.md" "$EMBEDDED_RESULT_DIR/README.md"

{
    echo "LuaSF embedded result package"
    echo "============================="
    echo "Config: $CONFIG"
    echo "Generated from: $BUILD_DIR"
    echo
    echo "bin:"
    ls -1 "$EMBEDDED_RESULT_DIR/bin"
    echo
    echo "stub:"
    ls -1 "$EMBEDDED_RESULT_DIR/stub"
    echo
    echo "tools:"
    ls -1 "$EMBEDDED_RESULT_DIR/tools"
    echo
    echo "include:"
    echo "- LuaSF generated headers from output/include"
    echo "- SFML public headers"
    echo "- sol2 public headers"
    echo "- Lua headers under include/lua"
    echo "- CMake generated sol compatibility headers under include/sol"
    echo "- Lua compatibility wrappers at include/lua.h, include/lauxlib.h, include/lualib.h"
    if [ -d "$EMBEDDED_RESULT_DIR/lib" ]; then
        echo
        echo "lib:"
        ls -1 "$EMBEDDED_RESULT_DIR/lib"
    fi
    echo
    echo "cmake:"
    echo "- add_subdirectory(path/to/result/embedded)"
    echo "- target_link_libraries(your_target PRIVATE LuaSF::LuaSF)"
    echo "- luasf_copy_runtime_files(your_target)"
} > "$EMBEDDED_RESULT_DIR/manifest.txt"

{
    echo "LuaSF Lua extension package"
    echo "==========================="
    echo "Config: $CONFIG"
    echo "Generated from: $BUILD_DIR"
    echo
    echo "bin:"
    ls -1 "$EXTENSION_RESULT_DIR/bin"
    echo
    echo "stub:"
    ls -1 "$EXTENSION_RESULT_DIR/stub"
    echo
    echo "Usage:"
    echo "- Add bin to package.cpath or copy LuaSF next to your Lua script."
    echo "- require(\"LuaSF\") returns the sf table."
    echo "- The host Lua runtime provides lua_State; lua.dll is not bundled here."
} > "$EXTENSION_RESULT_DIR/manifest.txt"

{
    echo "LuaSF result packages"
    echo "====================="
    echo "Config: $CONFIG"
    echo "Generated from: $BUILD_DIR"
    echo
    echo "embedded:"
    echo "- C/C++ embedded Lua integration package."
    echo "- CMake package root: embedded"
    echo
    echo "extension:"
    echo "- Plain Lua C extension package for require(\"LuaSF\")."
} > "$RESULT_DIR/manifest.txt"

echo
echo "Done."
echo "Result folder: $RESULT_DIR"
echo "Embedded runtime libraries: $EMBEDDED_RESULT_DIR/bin"
echo "Lua extension libraries: $EXTENSION_RESULT_DIR/bin"
echo "Embedded stub: $EMBEDDED_RESULT_DIR/stub/LuaSF.d.lua"
echo "Extension stub: $EXTENSION_RESULT_DIR/stub/LuaSF.d.lua"
echo "Headers: $EMBEDDED_RESULT_DIR/include"
echo "Host luac: $EMBEDDED_RESULT_DIR/tools/$(basename "$LUAC_FILE")"
