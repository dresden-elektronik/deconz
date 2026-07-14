#!/usr/bin/env bash

ARCH="${1:-}"
if [ "$ARCH" != "arm64" ] && [ "$ARCH" != "x86_64" ]; then
	echo "Usage: $0 [arm64|x86_64]"
	exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -e ./cred-macos ]; then
	echo "cred-macos not found:"
	echo "for codesign define DEVELOPER_ID_APPLICATION, TEAM_ID and PASS in cred-macos file."
#	exit 1
fi

# Official Qt from aqtinstall (monolithic install: every Qt*.framework sits
# under one lib/ and every plugin under one plugins/). Unlike Homebrew's
# split cellar layout, this lets macdeployqt resolve a plugin's @rpath to a
# framework the main binary doesn't directly link -- e.g. the SVG icon engine
# plugin (libqsvgicon.dylib -> QtSvg.framework) and the virtual keyboard
# plugin (libqtvirtualkeyboardplugin.dylib -> QtVirtualKeyboard.framework).
AQT_QT_DIR="${AQT_QT_DIR:-$HOME/Qt}"
QT_VERSION="${QT_VERSION:-6.11.1}"
QT_LOC="$AQT_QT_DIR/$QT_VERSION/macos"

if [ ! -e "$QT_LOC/bin/macdeployqt" ]; then
	echo "Qt $QT_VERSION not found in $QT_LOC; installing via aqtinstall..."
	AQT_VENV="${AQT_VENV:-$HOME/.aqt-venv}"
	if [ ! -e "$AQT_VENV/bin/aqt" ]; then
		echo "Creating aqtinstall venv in $AQT_VENV ..."
		python3 -m venv "$AQT_VENV"
		"$AQT_VENV/bin/pip" install --upgrade aqtinstall
	fi
	# qtsvg (QtSvg.framework + libqsvgicon) and qtdeclarative (QtQml/QtQuick,
	# needed by the virtual keyboard plugin) ship in the default clang_64
	# install. The add-on modules below are the ones deCONZ links or deploys:
	# qtserialport/qtwebsockets/qt5compat are linked directly, qtvirtualkeyboard
	# provides the VK plugin, qtimageformats provides the extra image-format
	# plugins (jp2/mng/tga/tiff/webp). qtpdf is intentionally NOT installed
	# (deCONZ doesn't use PDF); debug_info is skipped to keep the install small.
	"$AQT_VENV/bin/aqt" install-qt mac desktop "$QT_VERSION" clang_64 \
		-m qtserialport qtimageformats qtvirtualkeyboard qtwebsockets qt5compat \
		-O "$AQT_QT_DIR"
	if [ ! -e "$QT_LOC/bin/macdeployqt" ]; then
		echo "aqtinstall failed to install Qt $QT_VERSION into $QT_LOC, abort"
		exit 1
	fi
fi

export PATH="$QT_LOC/bin:$PATH"

source ./cred-macos

# https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/

# awk '/project\(/ { gsub(")",""); print $3;}' CMakeLists.txt
VERSION=$(grep project CMakeLists.txt \
	| awk 'match($0, /[0-9]+.[0-9]+.[0-9]/) {print substr($0, RSTART, RLENGTH)}')

YEAR=$(date "+%Y")

# create Info.plist

cat << EOF > Info.plist
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
	<key>CFBundleIconFile</key>
	<string>deconz.icns</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleGetInfoString</key>
	<string>Zigbee controller.</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleExecutable</key>
	<string>deCONZ</string>
	<key>CFBundleIdentifier</key>
	<string>de.phoscon.deconz</string>
	<key>CFBundleName</key>
	<string>deCONZ</string>
	<key>CFBundleDisplayName</key>
	<string>deCONZ</string>
	<key>NSHumanReadableCopyright</key>
	<string>© 2012-${YEAR}, dresden elektronik. All rights reserved.
        </string>
	<key>CFBundleShortVersionString</key>
        <string>${VERSION}</string>
	<key>NOTE</key>
	<string></string>
</dict>
</plist>

EOF


rm -fr "build-macos-$ARCH"
mkdir -p "build-macos-$ARCH"

pushd "build-macos-$ARCH"

mkdir -p deCONZ.app/Contents/Frameworks

pushd deCONZ.app/Contents/Frameworks

"$SCRIPT_DIR/build-openssl-macos.sh" "$ARCH"
OPENSSL_LIBDIR="$SCRIPT_DIR/openssl-macos-$ARCH/lib"

LIBCRYPTO=$(find "$OPENSSL_LIBDIR" -name 'libcrypto.3.dylib')
LIBSSL=$(find "$OPENSSL_LIBDIR" -name 'libssl.3.dylib')

cp "$LIBCRYPTO" "$LIBSSL" .

install_name_tool -change "$OPENSSL_LIBDIR/libcrypto.3.dylib" @loader_path/../Frameworks/libcrypto.3.dylib ./libssl.3.dylib
install_name_tool -change "$OPENSSL_LIBDIR/libssl.3.dylib" @loader_path/../Frameworks/libssl.3.dylib ./libssl.3.dylib

install_name_tool -change "$OPENSSL_LIBDIR/libcrypto.3.dylib" @loader_path/../Frameworks/libcrypto.3.dylib ./libcrypto.3.dylib
install_name_tool -id "@rpath/libssl.3.dylib" libssl.3.dylib
install_name_tool -id "@rpath/libcrypto.3.dylib" libcrypto.3.dylib

chmod 644 libssl.3.dylib
chmod 644 libcrypto.3.dylib

popd

# Deploy Qt into the bundle. The aqtinstall Qt is monolithic (all frameworks
# under one lib/, all plugins under one plugins/), so macdeployqt resolves
# every plugin's @rpath to its framework -- including ones the main binary
# doesn't directly link (QtSvg for the SVG icon engine plugin,
# QtVirtualKeyboard for the virtual keyboard plugin). No symlink/move hack.
# -libpath is a harmless hint pointing macdeployqt at the Qt lib dir;
# -always-overwrite lets repeated runs refresh the bundle in place.
# -no-codesign disables macdeployqt's built-in ad-hoc signing + verification:
# it rewrites the pre-staged OpenSSL dylib ids during deploy, which would
# otherwise trip its own `codesign --verify` and print an ERROR. The real
# signing (with the developer ID) is done by the dedicated block below.
# https://doc.qt.io/qt-6/macos-deployment.html
#
# QT_SKIP_AUTO_PLUGIN_INCLUSION keeps Qt's CMake from auto-including plugin
# targets at build time. It is harmless with the aqt Qt (qtpdf isn't
# installed, so no QPdfPlugin error can occur either way) and does not affect
# macdeployqt, which bundles the standard plugin set (cocoa, svg icon engine,
# virtual keyboard, image formats, styles, tls, ...) independently of it.

#OPENSSL_ROOT_ARGS=("-DOPENSSL_ROOT_DIR=$SCRIPT_DIR/openssl-macos-$ARCH")

OPENSSL_ROOT_DIR=$SCRIPT_DIR/openssl-macos-$ARCH \
cmake  -DCMAKE_MACOSX_BUNDLE=ON \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH=$QT_LOC \
	-DUSE_QT6=ON \
	-DQT_SKIP_AUTO_PLUGIN_INCLUSION=ON \
	-DCMAKE_OSX_ARCHITECTURES="$ARCH" \
	-G Ninja .. \
	&& cmake --build . \
	&& cmake --install . --prefix . \
	&& $QT_LOC/bin/macdeployqt deCONZ.app -always-overwrite -no-codesign -libpath="$QT_LOC/lib"

# macdeployqt deploys the SVG *icon engine* (iconengines/libqsvgicon.dylib)
# but not the SVG *image format* plugin (imageformats/libqsvg.dylib, from the
# qtsvg module), since deCONZ doesn't load .svg files as QImages. Copy it in
# too so the bundle carries full SVG support; the rpath normalization below
# rewrites its QtSvg reference into the bundle and the codesign block signs it.
cp "$QT_LOC/plugins/imageformats/libqsvg.dylib" deCONZ.app/Contents/PlugIns/imageformats/

# macdeployqt with the official @rpath-based aqt Qt copies every framework
# and plugin into the bundle but leaves the @rpath load commands and the
# plugins' build-time LC_RPATH (@loader_path/../../lib, which points at
# Contents/lib and doesn't exist in an .app). Plugins then only resolve at
# runtime via dyld's rpath cascade from the main binary. Rewrite every
# @rpath/Qt*.framework reference to an explicit @executable_path/../Frameworks
# path (and fix the Qt framework install IDs) so the bundle is fully
# self-contained and otool -L shows the in-bundle path directly. This mirrors
# what macdeployqt does automatically for absolute-path (Homebrew) Qt.
normalize_qt_rpaths() {
	local f=$1 ref fw
	for ref in $(otool -L "$f" 2>/dev/null | awk '{print $1}' \
					| grep -E '^@rpath/Qt.*\.framework/' | sort -u); do
		fw=${ref#@rpath/}
		install_name_tool -change "$ref" "@executable_path/../Frameworks/$fw" "$f"
	done
}

for f in deCONZ.app/Contents/MacOS/deCONZ \
		 deCONZ.app/Contents/Frameworks/*.dylib \
		 deCONZ.app/Contents/Frameworks/Qt*.framework/Versions/*/* \
		 $(find deCONZ.app/Contents/PlugIns -type f -name '*.dylib'); do
	[ -f "$f" ] || continue
	normalize_qt_rpaths "$f"
done

# Fix Qt framework install IDs: @rpath/QtX -> @executable_path/../Frameworks/QtX
for fwbin in deCONZ.app/Contents/Frameworks/Qt*.framework/Versions/*/*; do
	[ -f "$fwbin" ] || continue
	fwrel=${fwbin#deCONZ.app/Contents/Frameworks/}
	install_name_tool -id "@executable_path/../Frameworks/$fwrel" "$fwbin"
done

# Ensure the OpenSSL dylib install IDs point into the bundle. macdeployqt
# rewrites libcrypto's id but leaves libssl at @rpath (no binary links it by
# load command -- the TLS backend dlopens it by name, resolved via rpath).
install_name_tool -id "@executable_path/../Frameworks/libssl.3.dylib" deCONZ.app/Contents/Frameworks/libssl.3.dylib
install_name_tool -id "@executable_path/../Frameworks/libcrypto.3.dylib" deCONZ.app/Contents/Frameworks/libcrypto.3.dylib

if [ -e ../cred-macos ]; then
	# Plugins need to be signed separate due not found by --deep
	for i in $(find deCONZ.app/Contents/Plugins -name '*.dylib')
	do
		codesign --force --verify --verbose --timestamp --options runtime  --sign "${DEVELOPER_ID_APPLICATION}" "$i"
	done

	codesign --deep --force --verify --verbose --timestamp --options runtime  --sign "${DEVELOPER_ID_APPLICATION}" "deCONZ.app"
fi

# zip -r9y deconz_${VERSION}-macos_${ARCH}.zip deCONZ.app/
# 7z a -mx9 deconz_${VERSION}-macos_${ARCH}.zip deCONZ.app

# compress exactly like Finder does
ditto -c -k --sequesterRsrc --keepParent deCONZ.app deconz_${VERSION}-macos_${ARCH}.zip

popd

# keep Info.plist unmodified
git checkout HEAD -- Info.plist

