# Synaptics MST

This plugin supports querying and flashing Synaptics MST hubs used in Dell systems
and docks.

## Requirements
### (Kernel) DP Aux Interface
Kernel 4.6 introduced an DRM DP Aux interface for manipulation of the registers
needed to access an MST hub.
This patch can be backported to earlier kernels:
https://github.com/torvalds/linux/commit/e94cb37b34eb8a88fe847438dba55c3f18bf024a

### libsmbios
At compilation time and runtime you will need libsmbios_c version 2.3.0 or later
* source:		https://github.com/dell/libsmbios
* rpms:		https://apps.fedoraproject.org/packages/libsmbios
* debs (Debian):	http://tracker.debian.org/pkg/libsmbios
* debs (Ubuntu):	http://launchpad.net/ubuntu/+source/libsmbios

If you don't want or need this functionality you can use the
`--disable-dell` option.

## Usage
Supported devices will be displayed in `# fwupdmgr get-devices` output.

Here is an example output from a Dell WD15 dock:

```
Dell WD15/TB16 wired Dock Synaptics VMM3332
  Guid:                 653cd006-5433-57db-8632-0413af4d3fcc
  DeviceID:             MST-1-1-0-0
  Plugin:               synapticsmst
  Flags:                allow-online
  Version:              3.10.002
  Created:              2017-01-13
  Modified:             2017-01-13
  Trusted:              none
```
Payloads can be flashed just like any other plugin from LVFS.

## Supported devices
Not all Dell systems or accessories contain MST hubs.
Here is a sample list of systems known to support them however:
1. Dell WD15 dock
2. Dell TB16 dock
3. Latitude E5570
4. Latitude E5470
5. Latitude E5270
6. Latitude E7470
7. Latitude E7270
8. Latitude E7450
9. Latitude E7250
10. Latitude E5550
11. Latitude E5450
12. Latitude E5250
13. Latitude Rugged 5414
14. Latitude Rugged 7214
15. Latitude Rugged 7414
