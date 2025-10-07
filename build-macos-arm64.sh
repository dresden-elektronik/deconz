#!/usr/bin/env bash

if [ ! -e ./cred-macos ]; then
	echo "cred-macos not found:"
	echo "for codesign define DEVELOPER_ID_APPLICATION, TEAM_ID and PASS in cred-macos file."
#	exit 1
fi

# Homebrew
HOMEBREW_DIR=$(brew --prefix)

if [ ! -e "$HOMEBREW_DIR/Cellar" ]; then
	echo "homebrew installation not found, abort"
	exit 1
fi

if [ ! -e "$HOMEBREW_DIR/Cellar/qt@5" ]; then
	echo "Qt5 not found, abort"
	exit 1
fi

# QT_LOC=~/Qt/5.15.2/clang_64
# QT_LOC=$(find $HOMEBREW_DIR/Cellar/qt@5 -name Qt5Config.cmake)

# look for README in version specific directory:
#     $HOMEBREW_DIR/Cellar/qt@5/<version>/README
QT_LOC=$(find $HOMEBREW_DIR/Cellar/qt@5 -name README)
if [  -z "${QT_LOC}" ]; then
	echo "qt@5/<version> directory not found, abort"
	exit 1
fi
QT_LOC=$(dirname "$QT_LOC") # -> $HOMEBREW_DIR/Cellar/qt@5/<version>

source ./cred-macos

# https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/

# awk '/project\(/ { gsub(")",""); print $3;}' CMakeLists.txt
VERSION=$(grep project CMakeLists.txt \
	| awk 'match($0, /[0-9]+.[0-9]+.[0-9]/) {print substr($0, RSTART, RLENGTH)}')

ARCH=$(uname -m)
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


rm -fr build-macos
mkdir -p build-macos

pushd build-macos

mkdir -p deCONZ.app/Contents/Frameworks

pushd deCONZ.app/Contents/Frameworks

LIBCRYPTO=$(find $HOMEBREW_DIR/opt/openssl/lib -name 'libcrypto.3.dylib')
LIBSSL=$(find $HOMEBREW_DIR/opt/openssl/lib -name 'libssl.3.dylib')

cp $LIBCRYPTO $LIBSSL .

install_name_tool -change $HOMEBREW_DIR/opt/openssl/lib/libcrypto.3.dylib @loader_path/../Frameworks/libcrypto.3.dylib ./libssl.3.dylib
install_name_tool -change $HOMEBREW_DIR/opt/openssl/lib/libssl.3.dylib @loader_path/../Frameworks/libssl.3.dylib ./libssl.3.dylib

install_name_tool -change $HOMEBREW_DIR/opt/openssl/lib/libcrypto.3.dylib @loader_path/../Frameworks/libcrypto.3.dylib ./libcrypto.3.dylib
install_name_tool -id "@rpath/libssl.3.dylib" libssl.3.dylib
install_name_tool -id "@rpath/libcrypto.3.dylib" libcrypto.3.dylib

chmod 644 libssl.3.dylib
chmod 644 libcrypto.3.dylib

popd

# 	-DCMAKE_OSX_ARCHITECTURES=arm64 \

cmake  -DCMAKE_MACOSX_BUNDLE=ON \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH=$QT_LOC \
	-G Ninja .. \
	&& cmake --build . \
	&& cmake --install . --prefix . \
	&& $QT_LOC/bin/macdeployqt deCONZ.app

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

