#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$ROOT_DIR/dist"
BUILD_ROOT="$ROOT_DIR/.pkgbuild"
SOURCE_COPY="$BUILD_ROOT/source"
PACKAGE_PREFIX="tasmota2wb_"

if command -v apt-get >/dev/null 2>&1 && [ -f /etc/debian_version ]; then
  if ! dpkg-query -W -f='${Status}' libxml2-dev 2>/dev/null | grep -q 'install ok installed'; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends libxml2-dev
  fi
fi

rm -rf "$BUILD_ROOT"
mkdir -p "$DIST_DIR" "$SOURCE_COPY"
rm -f "$DIST_DIR"/${PACKAGE_PREFIX}*.deb \
      "$DIST_DIR"/${PACKAGE_PREFIX}*.buildinfo \
      "$DIST_DIR"/${PACKAGE_PREFIX}*.changes

tar \
  --exclude='./.git' \
  --exclude='./dist' \
  --exclude='./.pkgbuild' \
  --exclude='./autom4te.cache' \
  --exclude='./debian/.debhelper' \
  --exclude='./debian/tasmota2wb' \
  --exclude='./debian/tmp' \
  --exclude='./debian/files' \
  --exclude='./debian/debhelper-build-stamp' \
  -C "$ROOT_DIR" \
  -cf - . | tar -C "$SOURCE_COPY" -xf -

cd "$SOURCE_COPY"
dpkg-buildpackage -us -uc -b

find "$BUILD_ROOT" -maxdepth 1 -type f \
  \( -name "${PACKAGE_PREFIX}*.deb" \
  -o -name "${PACKAGE_PREFIX}*.buildinfo" \
  -o -name "${PACKAGE_PREFIX}*.changes" \) \
  -exec cp {} "$DIST_DIR"/ \;

rm -rf "$BUILD_ROOT"

echo "Artifacts saved to $DIST_DIR"
