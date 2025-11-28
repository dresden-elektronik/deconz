# deCONZ
The deCONZ is a generic application to monitor and control Zigbee devices.

This repository hosts the deCONZ core which handles connection to ConBee and RaspBee Zigbee coordinator hardware, low level Zigbee network device access and the node based GUI.

From 2012-2024 the deCONZ core was a closed source and as of 19 April 2024 is released as Open Source under BSD license.

## Plug-ins

The core functionality is extended by plug-ins which are located in different GIT repositories. During build these are fetched as GIT sub modules and compiled automatically.

* REST-API https://github.com/dresden-elektronik/deconz-rest-plugin
* OTA-Update https://github.com/dresden-elektronik/deconz-ota-plugin

Another important sub module is the deCONZ library https://github.com/dresden-elektronik/deconz-lib which provides API interfaces for the deCONZ core as well as plug-ins.

The web based Phoscon App is not part of this repository. It is fetched externally as zip file and included during  build in the resulting installer package.

## Building

Refer to [BUILDING.md](https://github.com/dresden-elektronik/deconz/blob/main/BUILDING.md) for instructions to create packages for various operating systems. Note these instruction currently only cover producing the release builds. In general you can use any IDE which supports CMake projects like VS Code, CLion or Qt creator to compile, run and debug deCONZ.

## Static analyzer

Build deCONZ with clang and scan-build to receive a HTML report.

```
rm -fr staticanalyze
scan-build --use-cc=clang --use-c++=clang++ cmake -B staticanalyze .
scan-build --use-cc=clang --use-c++=clang++ make -C staticanalyze -j2
```

When compilation is finished a command to display the HTML report is printed.
> scan-build: Run 'scan-view /tmp/scan-build-2025-11-28-113520-898242-1' to examine bug reports.

## Development

Since a while deCONZ v2.x is under a heavier shape shift with the following goals:

* Separate the Qt GUI from the core. The current "legacy" Qt GUI will become a separate GIT repository, development is also ongoing for a next generation GUI.
* The core and plug-ins should ultimately run on resource constraint systems with 8-32 MB RAM, like OpenWrt routers, even with large networks and hundreds of nodes.
* Remove dependencies on Qt and several other 3rd party libraries. The deCONZ library is extended to build up a complete replacement for the functionality we're using currently from Qt. The GCFFlasher 4 rewrite has shown that it's possible to reduce a Qt based 3.9 MB executable to 20 KB one with no non-native dependencies. Many of the utility functions are also reused in deCONZ core and plug-ins. Ultimately we only need to depend on a crypto library like OpenSSL, LibreSSL or BoringSSL and SQLite.
* Move remaining functionality from Zigbee Device Profile (ZDP) like querying neighbor tables to the REST-API `Device` class which maintains a holistic view of the state of each device.
* Currently deCONZ connects directly to the coordinator hardware via USB or serial interface and supports only one Zigbee network. The plan is to connect via local or network socket to GCFFlasher 4 https://github.com/dresden-elektronik/gcfflasher which holds the actual hardware connection. This way firmware updates can be handled again via API. Further Zigbee coordinator hardware can be freely placed, either on the same machine running deCONZ or another machine in the network.
  Another goal is to support multiple connections in parallel to split up large networks (more than 500 devices), while still having a combined view of them and support rule based automations across Zigbee networks.

Note these are all heavy tasks so there's no ETA, to stay tuned it's best to monitor the respective GIT repositories.
