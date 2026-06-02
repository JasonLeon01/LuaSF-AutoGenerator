#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

if [ "$#" -ne 5 ]; then
    echo "Usage: sh download_lib.sh <DisplayName> <URL> <ArchiveFile> <ExtractedFolder> <TargetFolder>" >&2
    exit 2
fi

LIB_NAME=$1
URL=$2
ARCHIVE=$3
SRC_FOLDER=$4
DEST_FOLDER=$5

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
