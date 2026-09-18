#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

if [ "$#" -gt 1 ]; then
    echo "Usage: sh init.sh [ME|ME-OH]" >&2
    exit 2
fi

PYTHON_BIN=${PYTHON:-python3.12}
VENV_PYTHON=".venv/bin/python"

SFML_VERSION=
SFML_REPOSITORY=
SFML_TAG=
SFML_ME_REPOSITORY=
SFML_ME_TAG=
SFML_ME_OH_REPOSITORY=
SFML_ME_OH_TAG=
LUA_VERSION=
LUA_SHA256=
SOL2_VERSION=

while IFS='=' read -r key value; do
    value=$(printf '%s' "$value" | tr -d '\r')
    case "$key" in
        ''|'#'*) continue ;;
        SFML_VERSION) SFML_VERSION=$value ;;
        SFML_REPOSITORY) SFML_REPOSITORY=$value ;;
        SFML_TAG) SFML_TAG=$value ;;
        SFML_ME_REPOSITORY) SFML_ME_REPOSITORY=$value ;;
        SFML_ME_TAG) SFML_ME_TAG=$value ;;
        SFML_ME_OH_REPOSITORY) SFML_ME_OH_REPOSITORY=$value ;;
        SFML_ME_OH_TAG) SFML_ME_OH_TAG=$value ;;
        LUA_VERSION) LUA_VERSION=$value ;;
        LUA_SHA256) LUA_SHA256=$value ;;
        SOL2_VERSION) SOL2_VERSION=$value ;;
    esac
done < versions.conf

if [ -z "$SFML_REPOSITORY" ] || [ -z "$SFML_TAG" ] ||
   [ -z "$SFML_ME_REPOSITORY" ] || [ -z "$SFML_ME_TAG" ] ||
   [ -z "$SFML_ME_OH_REPOSITORY" ] || [ -z "$SFML_ME_OH_TAG" ] ||
   [ -z "$LUA_VERSION" ] || [ -z "$LUA_SHA256" ] || [ -z "$SOL2_VERSION" ]; then
    echo "Missing required versions in versions.conf." >&2
    exit 1
fi

case "${1:-}" in
    '')
        SFML_VARIANT=
        SFML_VARIANT_REPOSITORY=$SFML_REPOSITORY
        SFML_VARIANT_TAG=$SFML_TAG
        ;;
    ME|me)
        SFML_VARIANT=ME
        SFML_VARIANT_REPOSITORY=$SFML_ME_REPOSITORY
        SFML_VARIANT_TAG=$SFML_ME_TAG
        ;;
    ME-OH|me-oh)
        SFML_VARIANT=ME-OH
        SFML_VARIANT_REPOSITORY=$SFML_ME_OH_REPOSITORY
        SFML_VARIANT_TAG=$SFML_ME_OH_TAG
        ;;
    *)
        echo "Unknown SFML variant '$1'. Use ME or ME-OH." >&2
        exit 2
        ;;
esac

if [ ! -x "$VENV_PYTHON" ]; then
    echo "Creating Python virtual environment..."
    "$PYTHON_BIN" -m venv .venv
fi

echo "Installing Python requirements into .venv..."
"$VENV_PYTHON" -m pip install -r requirements.txt

SFML_DIR="third_party/SFML"
SFML_TAG_FILE="$SFML_DIR/.luasf-sfml-tag"
SFML_SOURCE_FOLDER="${SFML_VARIANT_REPOSITORY##*/}-$SFML_VARIANT_TAG"
if [ ! -f "$SFML_DIR/CMakeLists.txt" ] ||
   [ "$(cat "$SFML_TAG_FILE" 2>/dev/null || true)" != "$SFML_VARIANT_TAG" ]; then
    rm -rf "$SFML_DIR" "third_party/$SFML_SOURCE_FOLDER"
    sh "$SCRIPT_DIR/download_lib.sh" "SFML $SFML_VARIANT_TAG" \
        "https://github.com/$SFML_VARIANT_REPOSITORY/archive/refs/tags/$SFML_VARIANT_TAG.tar.gz" \
        "sfml.tar.gz" \
        "$SFML_SOURCE_FOLDER" \
        "SFML"
    printf '%s\n' "$SFML_VARIANT_TAG" > "$SFML_TAG_FILE"
else
    echo "Using existing third_party/SFML ($SFML_VARIANT_TAG)."
fi

if [ ! -f "third_party/Lua/src/lua.h" ]; then
    sh "$SCRIPT_DIR/download_lib.sh" "Lua" \
        "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz" \
        "lua.tar.gz" \
        "lua-$LUA_VERSION" \
        "Lua" \
        "$LUA_SHA256"
else
    echo "Using existing third_party/Lua."
fi

download_url() {
    url=$1
    out=$2

    if command -v curl >/dev/null 2>&1; then
        curl -L "$url" -o "$out"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$out" "$url"
    else
        echo "Missing curl or wget; cannot download $url" >&2
        return 1
    fi
}

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

if [ ! -f "third_party/sol2/include/sol2/sol.hpp" ]; then
    echo "Downloading sol2 headers..."
    mkdir -p "third_party/sol2/include/sol2"
    for file in config.hpp forward.hpp sol.hpp; do
        download_url \
            "https://github.com/ThePhD/sol2/releases/download/v$SOL2_VERSION/$file" \
            "third_party/sol2/include/sol2/$file"
    done
else
    echo "Using existing third_party/sol2."
fi

apply_sol2_pr1606_patch

echo
echo "Dependencies are ready in $SCRIPT_DIR/third_party."
echo "SFML variant: ${SFML_VARIANT:-default} ($SFML_VARIANT_TAG)"
