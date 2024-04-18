
#!/usr/bin/env bash

if [ ! -e ./cred-macos ]; then
	echo "cred-macos not found, abort"
	echo "Must define DEVELOPER_ID_APPLICATION, TEAM_ID and PASS."
	exit 1
fi

source ./cred-macos
# https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/

VERSION=$(grep project CMakeLists.txt \
	| awk 'match($0, /[0-9]+.[0-9]+.[0-9]/) {print substr($0, RSTART, RLENGTH)}')

ARCH=$(uname -m)

pushd build-macos

rm -fr dist
mkdir dist

pushd dist

cp -a -R ../deCONZ.app .

#codesign --force -s "${DEVELOPER_ID_APPLICATION}" -v $PWD/deCONZ.app --deep --strict --options=runtime --timestamp

# verify signing (debug)
#codesign -dv --verbose=4 $PWD/deCONZ.app

rm -f *.zip

# compress exactly like Finder does
ditto -c -k --sequesterRsrc --keepParent deCONZ.app deconz_${VERSION}-macos_${ARCH}.zip

xcrun notarytool submit deconz_${VERSION}-macos_${ARCH}.zip \
	--apple-id phoscon.developer@gmail.com \
	--password ${PASS} --team-id $TEAM_ID --wait

xcrun stapler staple deCONZ.app

mv *.zip deconz_${VERSION}-macos_${ARCH}_unstapled.zip

ditto -c -k --sequesterRsrc --keepParent deCONZ.app deconz_${VERSION}-macos_${ARCH}.zip

popd

popd

