#!/bin/env sh

# Note: this script must be called from a MSYS2 MINGW32 shell!


QT_VERS="5.15.2"
MINGW_DIR="./qt5-bin/${QT_VERS}/mingw81_32/bin"
MINGW_DIR_ABS=$(readlink -f $MINGW_DIR)
ABS_PATH=$PWD
GCF_VERSION=V4_09_00
GCF_FILE=GCFFlasher_Win_${GCF_VERSION}.zip
BUILD_DIR=$ABS_PATH/build
QT_DIR=$MINGW_DIR
QT_PLUGINS_DIR=$(readlink -f "$MINGW_DIR/../plugins")
DECONZ_DIR=$ABS_PATH/..
NSI_SCRIPT=release.nsi
NSIS="/c/msys64/mingw32/bin/makensis.exe"
STAGE_DIR=$ABS_PATH/packwin32
OPENSSL_DIR=$(readlink -f ./openssl-win32-x86)

if [[ ! -d "$OPENSSL_DIR" ]]; then
    # this only works in a MSYS2 32 shell for some reaseon (TODO)
    ./build-openssl-win32.sh x86 || exit 2
fi

if [[ ! -d "$MINGW_DIR" ]]; then
    ./get_qt.sh || exit 3
fi

rm -rf build

export PATH="$MINGW_DIR:$PATH"

# copy applink.c to make OpenSSL BIO work on windows
cp "$OPENSSL_DIR/include/openssl/applink.c" "../src"

cmake -DBUILD_CHANNEL="" \
    -GNinja \
    -DOPENSSL_ROOT_DIR=$OPENSSL_DIR \
    -DCMAKE_BUILD_TYPE=Release -B build -S ..
cmake --build build

rm -f "../src/applink.c"


# ------------------------------------------------

rm -fr $STAGE_DIR
mkdir -p $STAGE_DIR

mkdir -p $STAGE_DIR/zcl

cp -a $DECONZ_DIR/src/plugins/de_web/general.xml $STAGE_DIR/zcl/ || exit 5
cp -ar $DECONZ_DIR/src/plugins/de_web/devices $STAGE_DIR/ || exit 5
cp -a $DECONZ_DIR/src/plugins/de_web/button_maps.json $STAGE_DIR/devices/ || exit 5

mkdir -p $STAGE_DIR/bin

cd $STAGE_DIR/bin
mkdir platforms
mkdir plugins


# GCFFlasher

curl -L -O http://deconz.dresden-elektronik.de/win/$GCF_FILE
unzip $GCF_FILE
rm $GCF_FILE

cp GCFFlasher_Win_${GCF_VERSION}/* .
cp ../../7za.exe .

rm -fr GCFFlasher_Win_${GCF_VERSION}


cp -a $BUILD_DIR/lib/sqlite3/*.dll . || exit 6
cp -a $BUILD_DIR/src/deCONZ.exe . || exit 6
cp -a $BUILD_DIR/src/lib/*.dll . || exit 6
#cp -a /src/deconz/build/_deps/deconzlib-build/*.dll . || exit 6
#cp -a /src/deconz/build/src/3rdparty/u_threads/*.dll . || exit 6
cp -a $BUILD_DIR/src/plugins/*/*.dll plugins || exit 6

cp -a \
    $QT_PLUGINS_DIR/platforms/qminimal.dll \
    $QT_PLUGINS_DIR/platforms/qwindows.dll \
    $QT_PLUGINS_DIR/styles/*.dll \
    platforms || exit 7

mkdir -p plugins/styles
cp -a \
    $QT_PLUGINS_DIR/styles/*.dll \
    plugins/styles || exit 7


cp -a \
    $MINGW_DIR_ABS/Qt5Core.dll \
    $MINGW_DIR_ABS/Qt5Gui.dll \
    $MINGW_DIR_ABS/Qt5Network.dll \
    $MINGW_DIR_ABS/Qt5Qml.dll \
    $MINGW_DIR_ABS/Qt5SerialPort.dll \
    $MINGW_DIR_ABS/Qt5WebSockets.dll \
    $MINGW_DIR_ABS/Qt5Widgets.dll \
    $MINGW_DIR_ABS/libstdc++-6.dll \
    $MINGW_DIR_ABS/libgcc_*.dll \
    $MINGW_DIR_ABS/libwinpthread-1.dll \
    $OPENSSL_DIR/bin/libcrypto*.dll \
    $OPENSSL_DIR/bin/libssl*.dll \
    . || exit 8

cd $STAGE_DIR

# Phoscon App

cp -ar $BUILD_DIR/webapp_2016 bin/plugins/de_web || exit 3
cp -ar $BUILD_DIR/pwa bin/plugins/de_web || exit 3
cp -a $DECONZ_DIR/src/plugins/de_web/description_in.xml bin/plugins/de_web/pwa || exit 3


cp -ar $DECONZ_DIR/icons $STAGE_DIR

mkdir doc
cp -a $DECONZ_DIR/linux/deCONZ/usr/share/deCONZ/doc/deCONZ-BHB-en.pdf doc


cp -ar ../nsis .

# update deCONZ version in NSIS script
VV=`awk '/deCONZ VERSION/{print $3}' $DECONZ_DIR/CMakeLists.txt | tr -d ')'`
MAJOR=`echo $VV | cut -d. -f1`
MINOR=`echo $VV | cut -d. -f2`
BUGFIX=`echo $VV | cut -d. -f3`
APP_VERSION=$(printf "%d.%02d.%02d.00" $MAJOR $MINOR $BUGFIX)
FILE_VERSION=$(printf "V%d_%02d_%02d" $MAJOR $MINOR $BUGFIX)
sed -i "s/"define\ FILE_VERSION".*/define FILE_VERSION \"${FILE_VERSION}\"/g" "nsis/$NSI_SCRIPT" || exit 2
sed -i "s/"define\ APP_VERSION".*/define APP_VERSION \"${APP_VERSION}\"/g" "nsis/$NSI_SCRIPT" || exit 3

"$NSIS" nsis/$NSI_SCRIPT || exit 9
