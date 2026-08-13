#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

OUTPUT_DIR="$SCRIPT_DIR/output"
BUILD_DIR="$OUTPUT_DIR/build"
RESULT_DIR="$OUTPUT_DIR/result"
EMBEDDED_RESULT_DIR="$RESULT_DIR/embedded"
EXTENSION_RESULT_DIR="$RESULT_DIR/extension"
PACKAGES_DIR="$OUTPUT_DIR/packages"
STAGING_DIR="$PACKAGES_DIR/.staging"

if ! command -v tar >/dev/null 2>&1; then
    echo "Missing tar. Install tar and retry." >&2
    exit 1
fi

if [ ! -d "$EMBEDDED_RESULT_DIR" ]; then
    echo "Missing output/result/embedded. Run sh collect_result.sh first." >&2
    exit 1
fi

if [ ! -d "$EXTENSION_RESULT_DIR" ]; then
    echo "Missing output/result/extension. Run sh collect_result.sh first." >&2
    exit 1
fi

if [ ! -f "$OUTPUT_DIR/callback_codecs.json" ]; then
    echo "Missing output/callback_codecs.json. Run sh build.sh first." >&2
    exit 1
fi

if [ ! -f "$EMBEDDED_RESULT_DIR/callback_codecs.json" ]; then
    echo "Missing embedded callback codec manifest. Run sh collect_result.sh first." >&2
    exit 1
fi

if [ ! -f "$EMBEDDED_RESULT_DIR/sfml_api.json" ]; then
    echo "Missing embedded SFML API snapshot. Run sh collect_result.sh first." >&2
    exit 1
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

PLATFORM_OS=$(detect_os)
PLATFORM_ARCH=$(detect_arch)
PLATFORM_COMPILER=$(detect_compiler)
PLATFORM_TAG="${PLATFORM_OS}-${PLATFORM_ARCH}-${PLATFORM_COMPILER}"

SOURCE_NAME=LuaSF-source
EMBEDDED_NAME="LuaSF-embedded-${PLATFORM_TAG}"
EXTENSION_NAME="LuaSF-extension-${PLATFORM_TAG}"

SOURCE_ARCHIVE="$PACKAGES_DIR/${SOURCE_NAME}.tar.gz"
EMBEDDED_ARCHIVE="$PACKAGES_DIR/${EMBEDDED_NAME}.tar.gz"
EXTENSION_ARCHIVE="$PACKAGES_DIR/${EXTENSION_NAME}.tar.gz"

echo "Packing LuaSF redistributable archives..."
echo "Platform: $PLATFORM_TAG"
echo "Packages: $PACKAGES_DIR"

rm -rf "$PACKAGES_DIR"
mkdir -p "$STAGING_DIR"

# Source package: output/ without build, bin, result, packages.
mkdir -p "$STAGING_DIR/$SOURCE_NAME"
find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 \
    ! -name build ! -name bin ! -name result ! -name packages \
    -exec cp -R {} "$STAGING_DIR/$SOURCE_NAME/" \;

# Embedded / extension packages with named top-level folders.
mkdir -p "$STAGING_DIR/$EMBEDDED_NAME" "$STAGING_DIR/$EXTENSION_NAME"
cp -R "$EMBEDDED_RESULT_DIR"/. "$STAGING_DIR/$EMBEDDED_NAME"/
cp -R "$EXTENSION_RESULT_DIR"/. "$STAGING_DIR/$EXTENSION_NAME"/

(
    cd "$STAGING_DIR"
    tar -czf "$SOURCE_ARCHIVE" "$SOURCE_NAME"
    tar -czf "$EMBEDDED_ARCHIVE" "$EMBEDDED_NAME"
    tar -czf "$EXTENSION_ARCHIVE" "$EXTENSION_NAME"
)

rm -rf "$STAGING_DIR"

echo
echo "Done."
echo "Source: $SOURCE_ARCHIVE"
echo "Embedded: $EMBEDDED_ARCHIVE"
echo "Extension: $EXTENSION_ARCHIVE"
