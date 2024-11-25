#!/bin/env sh

MINGW_DIR="/c/Qt/5.15.2/mingw81_32/bin"
ABS_PATH=$PWD
GCF_VERSION=V4_05_02
GCF_FILE=GCFFlasher_Win_${GCF_VERSION}.zip
BUILD_DIR=$ABS_PATH/build
QT_DIR=$MINGW_DIR
QT_PLUGINS_DIR=$MINGW_DIR/../plugins
DECONZ_DIR=$ABS_PATH/..
NSI_SCRIPT=release.nsi
NSIS="/c/msys64/mingw32/bin/makensis.exe"
STAGE_DIR=$ABS_PATH/packwin32
OPENSSL_DIR="$(PWD)/openssl-3/x86"

CMAKE=/c/Qt/Tools/CMake_64/bin

OPENSSL_ZIP=openssl-3.3.0.zip
OPENSSL_URL="https://download.firedaemon.com/FireDaemon-OpenSSL/$OPENSSL_ZIP"

if [[ ! -f "$OPENSSL_ZIP" ]]; then
    curl -L -O $OPENSSL_URL
    unzip $OPENSSL_ZIP
fi

rm -rf build

export PATH="$CMAKE:/c/Qt/Tools/mingw810_32/bin:$PATH"

cmake -DBUILD_CHANNEL="" \
    -GNinja \
    -DOPENSSL_ROOT_DIR=$OPENSSL_DIR \
    -DCMAKE_C_COMPILER=/c/Qt/Tools/mingw810_32/bin/gcc.exe \
    -DCMAKE_CXX_COMPILER=/c/Qt/Tools/mingw810_32/bin/g++.exe \
    -DCMAKE_PREFIX_PATH=/c/Qt/5.15.2/mingw81_32 \
    -DQt5_DIR=/c/Qt/5.15.2/mingw81_32/lib/cmake/Qt5 \
    -DCMAKE_BUILD_TYPE=Release -B build -S ..
cmake --build build


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
    $MINGW_DIR/Qt5Core.dll \
    $MINGW_DIR/Qt5Gui.dll \
    $MINGW_DIR/Qt5Network.dll \
    $MINGW_DIR/Qt5Qml.dll \
    $MINGW_DIR/Qt5SerialPort.dll \
    $MINGW_DIR/Qt5WebSockets.dll \
    $MINGW_DIR/Qt5Widgets.dll \
    $MINGW_DIR/libstdc++-6.dll \
    $MINGW_DIR/libgcc_*.dll \
    $MINGW_DIR/libwinpthread-1.dll \
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
