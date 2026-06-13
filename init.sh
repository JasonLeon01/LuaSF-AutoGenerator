#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

PYTHON_BIN=${PYTHON:-python3.12}
VENV_PYTHON=".venv/bin/python"

SFML_VERSION=
LUA_VERSION=
SOL2_VERSION=

while IFS='=' read -r key value; do
    value=$(printf '%s' "$value" | tr -d '\r')
    case "$key" in
        ''|'#'*) continue ;;
        SFML_VERSION) SFML_VERSION=$value ;;
        LUA_VERSION) LUA_VERSION=$value ;;
        SOL2_VERSION) SOL2_VERSION=$value ;;
    esac
done < versions.conf

if [ -z "$SFML_VERSION" ] || [ -z "$LUA_VERSION" ] || [ -z "$SOL2_VERSION" ]; then
    echo "Missing required versions in versions.conf." >&2
    exit 1
fi

if [ ! -x "$VENV_PYTHON" ]; then
    echo "Creating Python virtual environment..."
    "$PYTHON_BIN" -m venv .venv
fi

echo "Installing Python requirements into .venv..."
"$VENV_PYTHON" -m pip install -r requirements.txt

if [ ! -f "third_party/SFML/CMakeLists.txt" ]; then
    sh "$SCRIPT_DIR/download_lib.sh" "SFML" \
        "https://github.com/SFML/SFML/archive/refs/tags/$SFML_VERSION.tar.gz" \
        "sfml.tar.gz" \
        "SFML-$SFML_VERSION" \
        "SFML"
else
    echo "Using existing third_party/SFML."
fi

if [ ! -f "third_party/Lua/lua.h" ]; then
    sh "$SCRIPT_DIR/download_lib.sh" "Lua" \
        "https://github.com/lua/lua/archive/refs/tags/v$LUA_VERSION.tar.gz" \
        "lua.tar.gz" \
        "lua-$LUA_VERSION" \
        "Lua"
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
