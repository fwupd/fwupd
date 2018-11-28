Installed tests
=========

A test suite that can be used to interact with a fake device is installed when
configured with `-Ddaemon=true` and `-Dtests=true`.

By default this test suite is disabled.

Enabling
=======
To enable the test suite:
1. Modify `/etc/fwupd/daemon.conf` to remove the `test` plugin from `BlacklistPlugins`
   ```
   # sed "s,^Enabled=false,Enabled=true," -i /etc/fwupd/remotes.d/fwupd-tests.conf
   ```
2. Enable the `fwupd-tests` remote for local CAB files.
   ```
   # fwupdmgr enable-remote fwupd-tests
   ```

Using test suite
=====
When the daemon is started with the test suite enabled a fake webcam device will be created with a pending update.

```
Integrated Webcam™
  DeviceId:             08d460be0f1f9f128413f816022a6439e0078018
  Guid:                 b585990a-003e-5270-89d5-3705a17f9a43
  Summary:              A fake webcam
  Plugin:               test
  Flags:                updatable|supported|registered
  Vendor:               ACME Corp.
  VendorId:             USB:0x046D
  Version:              1.2.2
  VersionLowest:        1.2.0
  VersionBootloader:    0.1.2
  Icon:                 preferences-desktop-keyboard
  Created:              2018-11-29
```

## Upgrading
This can be upgraded to a firmware version `1.2.4` by using `fwupdmgr update` or any fwupd frontend.

```
$ fwupdmgr get-updates
Integrated Webcam™ has firmware updates:
GUID:                    b585990a-003e-5270-89d5-3705a17f9a43
ID:                      fakedevice.firmware
Update Version:          1.2.4
Update Name:             FakeDevice Firmware
Update Summary:          Firmware for the ACME Corp Integrated Webcam
Update Remote ID:        fwupd-tests
Update Checksum:         SHA1(fc0aabcf98bf3546c91270f2941f0acd0395dd79)
Update Location:         ./fakedevice124.cab
Update Description:      Fixes another bug with the flux capacitor to prevent time going backwards.

$ fwupdmgr update
Decompressing…         [***************************************]
Authenticating…        [***************************************]
Updating Integrated Webcam™…                                   ]
Verifying…             [***************************************] Less than one minute remaining…
```

## Downgrading
It can also be downgraded to firmware version `1.2.3`.
```
$ fwupdmgr downgrade
Choose a device:
0.	Cancel
1.	08d460be0f1f9f128413f816022a6439e0078018 (Integrated Webcam™)
2.	8a21cacfb0a8d2b30c5ee9290eb71db021619f8b (XPS 13 9370 System Firmware)
3.	d10c5f0ed12c6dc773f596b8ac51f8ace4355380 (XPS 13 9370 Thunderbolt Controller)
1
Decompressing…         [***************************************]
Authenticating…        [***************************************]
Downgrading Integrated Webcam™…               \                ]
Verifying…             [***************************************] Less than one minute remaining…
```
