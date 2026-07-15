#!/bin/env bash

QT_VERS=6.10.3
QTDIR=qt6-bin

# deprecated Qt6 no longer supports 32-bit Windows
QT5_VERS=5.15.2
QT5DIR=qt5-bin

if [[ ! -d "venv" ]]; then
	python -m venv venv || exit 1
fi

# resolve dynamically since sometimes it's venv/bin and sometimes venv/Scripts depending on the shell
PIP=$(find venv -name pip3.exe)

if [[ ! -f "$AQT" ]]; then
	$PIP install aqtinstall || exit 2
fi

AQT=$(find venv -name aqt.exe)

rm -fr $QTDIR
mkdir $QTDIR

rm -fr $QT5DIR
mkdir $QT5DIR

# $AQT list-qt windows desktop
# $AQT list-qt windows desktop --long-modules $QT_VERS win64_mingw
# $AQT list-qt windows desktop --arch 5.15.2

$AQT install-qt --outputdir $QTDIR windows desktop $QT_VERS win64_mingw -m qtwebsockets qtserialport qt5compat || exit 3
$AQT install-qt --outputdir $QT5DIR windows desktop $QT5_VERS win32_mingw81 || exit 4

