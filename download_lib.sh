#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

if [ "$#" -lt 5 ] || [ "$#" -gt 6 ]; then
    echo "Usage: sh download_lib.sh <DisplayName> <URL> <ArchiveFile> <ExtractedFolder> <TargetFolder> [SHA256]" >&2
    exit 2
fi

LIB_NAME=$1
URL=$2
ARCHIVE=$3
SRC_FOLDER=$4
DEST_FOLDER=$5
EXPECTED_SHA256=${6:-}

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

mkdir -p third_party

echo "Downloading $LIB_NAME..."
download_url "$URL" "$ARCHIVE"

if [ -n "$EXPECTED_SHA256" ]; then
    echo "Verifying $LIB_NAME SHA-256..."
    if command -v sha256sum >/dev/null 2>&1; then
        printf '%s  %s\n' "$EXPECTED_SHA256" "$ARCHIVE" | sha256sum --check -
    elif command -v shasum >/dev/null 2>&1; then
        actual_sha256=$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')
        if [ "$actual_sha256" != "$EXPECTED_SHA256" ]; then
            echo "SHA-256 mismatch: $actual_sha256" >&2
            rm -f "$ARCHIVE"
            exit 1
        fi
    else
        echo "Missing sha256sum or shasum; cannot verify $ARCHIVE" >&2
        rm -f "$ARCHIVE"
        exit 1
    fi
fi

echo "Extracting $LIB_NAME..."
if command -v tar >/dev/null 2>&1; then
    tar -xzf "$ARCHIVE" -C third_party
else
    echo "Missing tar; cannot extract $ARCHIVE" >&2
    rm -f "$ARCHIVE"
    exit 1
fi

rm -f "$ARCHIVE"

if [ -d "third_party/$SRC_FOLDER" ]; then
    mv "third_party/$SRC_FOLDER" "third_party/$DEST_FOLDER"
else
    echo "$LIB_NAME source folder not found: third_party/$SRC_FOLDER" >&2
    exit 1
fi
