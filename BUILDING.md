# Build deCONZ

This guide describes how to build a deCONZ package for various platforms.

## Checkout sources

The main repository contains only deCONZ core.

* Plug-ins like the REST-API and OTA plug-ins are in separate repositories which are referenced as GIT sub modules.
* The Phoscon App and old WebApp 2016 are packaged with a deCONZ release and are fetched automatically by CMake from https://deconz.dresden-elektronik.de/phoscon_app

```
git clone https://github.com/dresden-elektronik/deconz.git
cd deconz
git submodule update --init --recursive
```

## Build for Linux

```
cmake -DQT_VERSION_MAJOR=5 -DCMAKE_BUILD_TYPE=Release -B build .
cmake --build build --parallel 4
```

Optional create Debian `.deb` package

```
cd build
cpack -G "DEB;TGZ" .
```


## Build installer for Windows

**Dependencies:**

* MSYS2 installation in `C:\msys64`
* Official Qt 5.15.2 installation in `C:\Qt`

Open GIT Bash

```sh
cd build-mingw
./build-msys2.sh
```

Compiled deCONZ installer is in:

```plain
build-mingw/packwin32/nsis/deCONZ_Setup_Win32_<version>.exe
```

## Build deCONZ.app bundle for macOS

Note this method requires that you have an Apple developer account and ID.

Create file `cred-macos` with Apple sign credentials if not exist:

```sh
DEVELOPER_ID_APPLICATION="Developer ID Application: dresden elektronik ingenieurtechnik gmbH (XXXXXXXXXXXXXX)"
TEAM_ID=XXXXXXXXXXX
PASS='insert password here'
```

Run build script to compile via CMake

```
./build-macos.sh
```

Run sign script to sign deCONZ.app bundle

```
./sign-macos.sh
```

Compressed final bundle

```
build-macos/dist/deconz_1.23.4-macos_x86_64.zip
```
