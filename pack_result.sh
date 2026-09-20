#!/usr/bin/env sh
set -eu

# Keep macOS AppleDouble metadata out of the portable source archive.
export COPYFILE_DISABLE=1

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

SOURCE_ONLY=0
case "$#:${1:-}" in
    0:) ;;
    1:--source-only) SOURCE_ONLY=1 ;;
    *)
        echo "Usage: sh pack_result.sh [--source-only]" >&2
        exit 2
        ;;
esac

OUTPUT_DIR="$SCRIPT_DIR/output"
BUILD_DIR="$OUTPUT_DIR/build"
RESULT_DIR="$OUTPUT_DIR/result"
EMBEDDED_RESULT_DIR="$RESULT_DIR/embedded"
PACKAGES_DIR="$OUTPUT_DIR/packages"
STAGING_DIR="$PACKAGES_DIR/.staging"
if [ "$SOURCE_ONLY" -eq 1 ]; then
    STAGING_DIR="$PACKAGES_DIR/.staging-source"
fi

if ! command -v tar >/dev/null 2>&1; then
    echo "Missing tar. Install tar and retry." >&2
    exit 1
fi

for source_file in callback_codecs.json sfml_api.json; do
    if [ ! -f "$OUTPUT_DIR/LuaSF/$source_file" ]; then
        echo "Missing output/LuaSF/$source_file. Run sh build.sh first." >&2
        exit 1
    fi
done

if [ "$SOURCE_ONLY" -eq 0 ]; then
    if [ ! -d "$EMBEDDED_RESULT_DIR" ]; then
        echo "Missing output/result/embedded. Run sh collect_result.sh first." >&2
        exit 1
    fi
    for embedded_file in callback_codecs.json sfml_api.json; do
        if [ ! -f "$EMBEDDED_RESULT_DIR/$embedded_file" ]; then
            echo "Missing embedded $embedded_file. Run sh collect_result.sh first." >&2
            exit 1
        fi
    done
fi

cmake_cache_value() {
    key=$1
    if [ ! -d "$BUILD_DIR" ]; then
        return 0
    fi
    cmake -N -LA "$BUILD_DIR" 2>/dev/null | sed -n "s/^${key}:[^=]*=//p" | head -n 1 || true
}

normalize_os() {
    case $1 in
        Darwin|darwin|macOS|macos|OSX|osx) echo macOS ;;
        Windows|windows|WIN32|win32) echo Windows ;;
        Linux|linux) echo Linux ;;
        *) echo "$1" ;;
    esac
}

normalize_arch() {
    case $1 in
        arm64|aarch64|ARM64|AARCH64) echo ARM64 ;;
        x86_64|amd64|AMD64|x64|X64) echo x64 ;;
        *) echo "$1" ;;
    esac
}

normalize_compiler() {
    case $1 in
        AppleClang|Clang|clang) echo clang ;;
        MSVC|msvc) echo MSVC ;;
        GNU|gcc|GCC) echo gcc ;;
        *) echo "$1" ;;
    esac
}

detect_os() {
    value=$(cmake_cache_value CMAKE_SYSTEM_NAME)
    if [ -z "$value" ]; then
        value=$(uname -s 2>/dev/null || true)
    fi
    if [ -z "$value" ]; then
        echo "Failed to detect OS for package naming." >&2
        exit 1
    fi
    normalize_os "$value"
}

detect_arch() {
    value=$(cmake_cache_value CMAKE_SYSTEM_PROCESSOR)
    if [ -z "$value" ]; then
        value=$(uname -m 2>/dev/null || true)
    fi
    if [ -z "$value" ]; then
        echo "Failed to detect architecture for package naming." >&2
        exit 1
    fi
    normalize_arch "$value"
}

detect_compiler() {
    value=$(cmake_cache_value CMAKE_CXX_COMPILER_ID)
    if [ -z "$value" ]; then
        if command -v clang++ >/dev/null 2>&1 || command -v clang >/dev/null 2>&1; then
            value=Clang
        elif command -v g++ >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1; then
            value=GNU
        fi
    fi
    if [ -z "$value" ]; then
        echo "Failed to detect compiler for package naming." >&2
        exit 1
    fi
    normalize_compiler "$value"
}

PLATFORM_TAG=
if [ "$SOURCE_ONLY" -eq 0 ]; then
    PLATFORM_OS=$(detect_os)
    PLATFORM_ARCH=$(detect_arch)
    PLATFORM_COMPILER=$(detect_compiler)
    PLATFORM_TAG="${PLATFORM_OS}-${PLATFORM_ARCH}-${PLATFORM_COMPILER}"
fi

SFML_VARIANT=$(cat "$SCRIPT_DIR/.luasf-sfml-variant" 2>/dev/null || true)
VARIANT_SUFFIX=
if [ -n "$SFML_VARIANT" ]; then
    VARIANT_SUFFIX="-$SFML_VARIANT"
fi

SOURCE_VARIANT_SUFFIX=$VARIANT_SUFFIX
if [ "$SFML_VARIANT" = "ME-OH" ]; then
    SOURCE_VARIANT_SUFFIX=-ME
fi
SOURCE_NAME="LuaSF-source${SOURCE_VARIANT_SUFFIX}"
EMBEDDED_NAME="LuaSF-embedded${VARIANT_SUFFIX}-${PLATFORM_TAG}"

SOURCE_ARCHIVE="$PACKAGES_DIR/${SOURCE_NAME}.tar.gz"
EMBEDDED_ARCHIVE="$PACKAGES_DIR/${EMBEDDED_NAME}.tar.gz"

echo "Packing LuaSF redistributable archives..."
echo "SFML variant: ${SFML_VARIANT:-default}"
if [ "$SOURCE_ONLY" -eq 0 ]; then
    echo "Platform: $PLATFORM_TAG"
fi
echo "Packages: $PACKAGES_DIR"

if [ "$SOURCE_ONLY" -eq 0 ]; then
    rm -rf "$PACKAGES_DIR"
else
    rm -rf "$STAGING_DIR"
fi
mkdir -p "$STAGING_DIR"

# Source archives contain only the two reusable CMake source projects.
# ME-OH consumes the ME source package; only its embedded package is distinct.
if [ "$SFML_VARIANT" != "ME-OH" ] || [ "$SOURCE_ONLY" -eq 1 ]; then
    for source_project in LuaSF LuaGlue; do
        if [ ! -f "$OUTPUT_DIR/$source_project/CMakeLists.txt" ]; then
            echo "Missing source project $source_project. Run sh build.sh first." >&2
            exit 1
        fi
        mkdir -p "$STAGING_DIR/$source_project"
        (cd "$OUTPUT_DIR/$source_project" && tar --exclude=build --exclude=.git --exclude=__pycache__ --exclude=.cache -cf - .) |
            (cd "$STAGING_DIR/$source_project" && tar -xf -)
    done
fi

# Embedded package with a named top-level folder.
if [ "$SOURCE_ONLY" -eq 0 ]; then
    mkdir -p "$STAGING_DIR/$EMBEDDED_NAME"
    cp -R "$EMBEDDED_RESULT_DIR"/. "$STAGING_DIR/$EMBEDDED_NAME"/
fi

(
    cd "$STAGING_DIR"
    if [ "$SFML_VARIANT" != "ME-OH" ] || [ "$SOURCE_ONLY" -eq 1 ]; then
        tar -czf "$STAGING_DIR/${SOURCE_NAME}.tar.gz" LuaSF LuaGlue
        mv "$STAGING_DIR/${SOURCE_NAME}.tar.gz" "$SOURCE_ARCHIVE"
    fi
    if [ "$SOURCE_ONLY" -eq 0 ]; then
        tar -czf "$EMBEDDED_ARCHIVE" "$EMBEDDED_NAME"
    fi
)

rm -rf "$STAGING_DIR"

echo
echo "Done."
if [ "$SFML_VARIANT" != "ME-OH" ] || [ "$SOURCE_ONLY" -eq 1 ]; then
    echo "Source: $SOURCE_ARCHIVE"
else
    echo "Source: use LuaSF-source-ME"
fi
if [ "$SOURCE_ONLY" -eq 0 ]; then
    echo "Embedded: $EMBEDDED_ARCHIVE"
fi
