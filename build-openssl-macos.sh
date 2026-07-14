#!/usr/bin/env bash
set -euo pipefail

ARCH="${1:-}"
if [ "$ARCH" != "arm64" ] && [ "$ARCH" != "x86_64" ]; then
	echo "Usage: $0 {arm64|x86_64}"
	exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/openssl-macos-$ARCH"

if [ -d "$OUTPUT_DIR" ]; then
	echo "already built: $OUTPUT_DIR"
	exit 0
fi

OPENSSL_VERSION="${OPENSSL_VERSION:-3.6.3}"
CACHE_DIR="$SCRIPT_DIR/openssl-src"
TARBALL="$CACHE_DIR/openssl-$OPENSSL_VERSION.tar.gz"
SRC_DIR="$CACHE_DIR/openssl-$OPENSSL_VERSION"

MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}"
export MACOSX_DEPLOYMENT_TARGET

rm -fr "$CACHE_DIR"
mkdir -p "$CACHE_DIR"

if [ ! -f "$TARBALL" ]; then
	echo "Downloading OpenSSL $OPENSSL_VERSION ..."
	curl -fsSL "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" \
		-o "$TARBALL"
fi

if [ ! -d "$SRC_DIR" ]; then
	echo "Extracting ..."
	tar xzf "$TARBALL" -C "$CACHE_DIR"
fi

pushd "$SRC_DIR" > /dev/null

CONFIG_TARGET="darwin64-${ARCH}-cc"
echo "Configuring OpenSSL ($CONFIG_TARGET) -> $OUTPUT_DIR ..."
./Configure "$CONFIG_TARGET" \
	--prefix="$OUTPUT_DIR" \
	--openssldir="$OUTPUT_DIR/etc/ssl" \
	no-tests no-docs

echo "Building ..."
make -j"$(sysctl -n hw.ncpu)"
#make -j1
make install_sw

popd > /dev/null

echo "Done. OpenSSL $OPENSSL_VERSION ($ARCH) installed in $OUTPUT_DIR"
rm -fr "$CACHE_DIR"
