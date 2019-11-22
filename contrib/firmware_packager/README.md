# Firmware Packager

This script is intended to make firmware updating easier until OEMs upload their firmware packages to the LVFS. It works by extracting the firmware binary contained in a Microsoft .exe file (intended for performing the firmware update from a Windows system) and repackaging it in a cab file usable by fwupd. The cab file can then be install using `fwupdmgr install`

## Prerequisites 

To run this script you will need

1. Python3.5, a standard install should include all packages you need
2. 7z (for extracting .exe files)
3. gcab (for creating the cab file)

## Usage

To create a firmware package, you must supply, at a minimum:

1. A string ID to name the firmware (`--firmware-id`). You are free to choose this, but [fwupd.org](http://fwupd.org/vendors.html) recommends using "a reverse-DNS prefix similar to java" and to "always use a .firmware suffix" (e.g. net.queuecumber.DellTBT.firmware)
2. A short name for the firmware package, again you are free to choose this (`--firmware-name`).
3. The unique ID of the device that the firmware is intended for (`--device-unique-id`). This *must* match the unique ID from `fwupdmgr get-devices`
4. The firmware version (`--release-version`), try to match the manufacturers versioning scheme
5. The path to the executable file to repackage (`--exe`)
6. The path *relative to the root of the exe archive* of the .bin file to package (`--bin`). Use 7z or archive-manager to inspect the .exe file and find this path. 
For example, if I want to package `dell-thunderbolt-firmware.exe` and I open the .exe with archive-manager and find that `Intel/tbt.bin` is the path to the
bin file inside the archive, I would pass `--exe dell-thunderbolt-firmware.exe --bin Intel/tbt.bin`
7. The path to the cab file to output (`--out`). 

## Documentation

`--firmware-name` Short name of the firmware package can be customized (e.g. DellTBT) **REQUIRED**

`--firmware-summary` One line description of the firmware package (e.g. Dell thunderbolt firmware)

`--firmware-description` Longer description of the firmware package. Theoretically this can include HTML but I haven't tried it

`--device-guid` GUID ID of the device this firmware will run on, this *must* match the output from `fwupdmgr get-devices` (e.g. 72533768-6a6c-5c06-994a-367374336810) **REQUIRED**

`--firmware-homepage` Website for the firmware provider (e.g. http://www.dell.com)

`-contact-info` Email address of the firmware developer (e.g. someone@something.net)

`--developer-name` Name of the firmware developer (e.g. Dell) **REQUIRED**

`--release-version` Version number of the firmware package (e.g. 4.21.01.002) **REQUIRED**
`--release-description` Description of the firmware release, again this can theoretically include HTML but I didn't try it.

`--exe` Executable file to extract firmware from (e.g. `dell-thunderbolt-firmware.exe`) **REQUIRED**

`--bin` Path to the .bin file inside the executable to use as the firmware image', relative to the root of the archive (e.g. `Intel/tbt.bin`) **REQUIRED**

`--out` Output cab file path (e.g. `updates/firmware.cab`) **REQUIRED**

## Example

Let's say we downloaded `Intel_TBT3_FW_UPDATE_NVM21_318RY_A01_4.21.01.002.exe` (available [here](https://downloads.dell.com/FOLDER04421073M/1/Intel_TBT3_FW_UPDATE_NVM21_318RY_A01_4.21.01.002.exe)) containing updated firmware for Dell laptops thunderbolt controllers. Since Dell hasn't made this available on the LVFS yet, we want to package and install it ourselves.

Opening the .exe with archive manager, we see it has a single folder: `Intel` and inside that, a set of firmware binaries (along with some microsoft junk). We pick the file `0x07BE_secure.bin` since we have a Dell XPS 9560 and that is its device string. 

Next we use `fwupdmgr` to get the device ID for the thunderbolt controller:

```
$ fwupdmgr get-devices
Thunderbolt Controller
  Guid:                 72533768-6a6c-5c06-994a-367374336810
  DeviceID:             08001575
  Plugin:               thunderbolt
  Flags:                internal|allow-online
  DeviceVendor:         Intel
  Version:              21.00
  Created:              2017-08-16
```
The GUID field contains what we are looking for

We can then run the firmware-packager with the following arguments:

```
$ firmware-packager --firmware-id net.queuecumber.DellTBT.firmware --firmware-name DellTBT --device-unique-id 72533768-6a6c-5c06-994a-367374336810 --release-version 4.21.01.002 --exe ~/Downloads/Intel_TBT3_FW_UPDATE_NVM21_318RY_A01_4.21.01.002.exe --bin Intel/0x07BE_secure.bin --out firmware.cab
Using temp directory /tmp/tmpoey6_zx_
Extracting firmware exe
Locating firmware bin
Creating metainfo
Cabbing firmware files
Done
```
And we should have a firmware.cab that contains the packaged firmware. We can then install this firmware with 
```
$ fwupdmgr install firmware.cab
```
