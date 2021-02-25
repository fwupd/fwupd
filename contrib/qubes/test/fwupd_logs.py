#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

UPDATE_INFO = """{
                   "Devices" : [
                       {
                       "Name" : "ColorHug2",
                       "DeviceId" : "b0a78eb71f4eeea7df8fb114522556ba8ce22074",
                       "Guid" : [
                           "2082b5e0-7a64-478a-b1b2-e3404fab6dad",
                           "aa4b4156-9732-55db-9500-bf6388508ee3",
                           "101ee86a-7bea-59fb-9f89-6b6297ceed3b",
                           "2fa8891f-3ece-53a4-adc4-0dd875685f30"
                       ],
                       "Summary" : "An open source display colorimeter",
                       "Plugin" : "colorhug",
                       "Protocol" : "com.hughski.colorhug",
                       "Flags" : [
                           "updatable",
                           "supported",
                           "registered",
                           "self-recovery",
                           "add-counterpart-guids"
                       ],
                       "Vendor" : "Hughski Ltd.",
                       "VendorId" : "USB:0x273F",
                       "Version" : "2.0.6",
                       "VersionFormat" : "triplet",
                       "Icons" : [
                           "colorimeter-colorhug"
                       ],
                       "InstallDuration" : 8,
                       "Created" : 1614224175,
                       "Releases" : [
                           {
                           "AppstreamId" : "com.hughski.ColorHug2.firmware",
                           "RemoteId" : "lvfs",
                           "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                           "Description" : "<p>This release fixes prevents the firmware returning an error when the remote SHA1 hash was never sent.</p>",
                           "Version" : "2.0.7",
                           "Filename" : "hughski-colorhug2-2.0.7.cab",
                           "Protocol" : "com.hughski.colorhug",
                           "Checksum" : [
                               "80bddeb898cda5b87d9837e13a9ace19846053bf",
                               "32c4a2c9be787cdf1d757c489d6455bd7bb14053425180b6d331c37e1ccc1cda"
                           ],
                           "License" : "GPL-2.0+",
                           "Size" : 16384,
                           "Created" : 1482901200,
                           "Locations" : [
                               "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
                               "ipfs://QmUByRuHG9Gb2s8gKKVqDcjhUrn8vy62B4WqjbpWDD42cf"
                           ],
                           "Uri" : "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
                           "Homepage" : "http://www.hughski.com/",
                           "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                           "Vendor" : "Hughski Limited",
                           "Flags" : [
                               "is-upgrade"
                           ],
                           "InstallDuration" : 8
                           }
                       ]
                       }
                    ]
                 }
"""

DMI_DECODE = """# dmidecode 3.1
Getting SMBIOS data from sysfs.
SMBIOS 3.1.1 present.

Handle 0x0000, DMI type 0, 26 bytes
BIOS Information
    Vendor: Dell Inc.
    Version: P1.00
    Release Date: 02/09/2018
    Address: 0xF0000
    Runtime Size: 64 kB
    ROM Size: 16 MB
    Characteristics:
        PCI is supported
        BIOS is upgradeable
        BIOS shadowing is allowed
        Boot from CD is supported
        Selectable boot is supported
        BIOS ROM is socketed
        EDD is supported
        5.25"/1.2 MB floppy services are supported (int 13h)
        3.5"/720 kB floppy services are supported (int 13h)
        3.5"/2.88 MB floppy services are supported (int 13h)
        Print screen service is supported (int 5h)
        8042 keyboard services are supported (int 9h)
        Serial services are supported (int 14h)
        Printer services are supported (int 17h)
        ACPI is supportedUSB legacy is supported
        BIOS boot specification is supported
        Targeted content distribution is supported
        UEFI is supported
    BIOS Revision: 5.13
"""

GET_DEVICES = """{
  "Devices" : [
    {
      "Name" : "ColorHug2",
      "DeviceId" : "cf294bf55b333004beb7c41f952c1838c23e1f4a",
      "Guid" : [
        "2082b5e0-7a64-478a-b1b2-e3404fab6dad",
        "aa4b4156-9732-55db-9500-bf6388508ee3",
        "101ee86a-7bea-59fb-9f89-6b6297ceed3b",
        "2fa8891f-3ece-53a4-adc4-0dd875685f30"
      ],
      "Summary" : "An open source display colorimeter",
      "Plugin" : "colorhug",
      "Protocol" : "com.hughski.colorhug",
      "Flags" : [
        "updatable",
        "supported",
        "registered",
        "self-recovery",
        "add-counterpart-guids"
      ],
      "Vendor" : "Hughski Ltd.",
      "VendorId" : "USB:0x273F",
      "Version" : "2.0.6",
      "VersionFormat" : "triplet",
      "Icons" : [
        "colorimeter-colorhug"
      ],
      "InstallDuration" : 8,
      "Created" : 1614246373,
      "Releases" : [
        {
          "AppstreamId" : "com.hughski.ColorHug2.firmware",
          "RemoteId" : "lvfs",
          "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
          "Description" : "<p>This release fixes prevents the firmware returning an error when the remote SHA1 hash was never sent.</p>",
          "Version" : "2.0.7",
          "Filename" : "hughski-colorhug2-2.0.7.cab",
          "Protocol" : "com.hughski.colorhug",
          "Checksum" : [
            "80bddeb898cda5b87d9837e13a9ace19846053bf",
            "32c4a2c9be787cdf1d757c489d6455bd7bb14053425180b6d331c37e1ccc1cda"
          ],
          "License" : "GPL-2.0+",
          "Size" : 16384,
          "Created" : 1482901200,
          "Locations" : [
            "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
            "ipfs://QmUByRuHG9Gb2s8gKKVqDcjhUrn8vy62B4WqjbpWDD42cf"
          ],
          "Uri" : "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
          "Homepage" : "http://www.hughski.com/",
          "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
          "Vendor" : "Hughski Limited",
          "Flags" : [
            "is-upgrade"
          ],
          "InstallDuration" : 8
        },
        {
          "AppstreamId" : "com.hughski.ColorHug2.firmware",
          "RemoteId" : "lvfs",
          "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
          "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on the second half of batch 16</li><li>Fix the firmware upgrade process using new versions of fwupd</li></ul>",
          "Version" : "2.0.6",
          "Filename" : "hughski-colorhug2-2.0.6.cab",
          "Protocol" : "com.hughski.colorhug",
          "Checksum" : [
            "60e28bb402b427dbce19e150d63987f5e18c1880",
            "a646b1798ce7f5ac26229aa85c35cc4f44a5bd8bfc9e5332a8ec815aef075566"
          ],
          "License" : "GPL-2.0+",
          "Size" : 16384,
          "Created" : 1450792062,
          "Locations" : [
            "https://fwupd.org/downloads/170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab",
            "ipfs://QmdWFrYo1YJxgGU37Qy7LkwPQM26vPMVxLRANUga6TzSjW"
          ],
          "Uri" : "https://fwupd.org/downloads/170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab",
          "Homepage" : "http://www.hughski.com/",
          "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
          "Vendor" : "Hughski Limited",
          "InstallDuration" : 8
        },
        {
          "AppstreamId" : "com.hughski.ColorHug2.firmware",
          "RemoteId" : "lvfs",
          "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
          "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on batch 16</li><li>Make the self test more sensitive to detect floating pins</li></ul>",
          "Version" : "2.0.5",
          "Filename" : "hughski-colorhug2-2.0.5.cab",
          "Protocol" : "com.hughski.colorhug",
          "Checksum" : [
            "e37b9d360d61157657335d80585a005ff2593108",
            "8cd379eb2e1467e4fda92c20650306dc7e598b1d421841bbe19d9ed6ea01e3ee"
          ],
          "License" : "GPL-2.0+",
          "Size" : 16384,
          "Created" : 1444059405,
          "Locations" : [
            "https://fwupd.org/downloads/f7dd4ab29fa610438571b8b62b26b0b0e57bb35b-hughski-colorhug2-2.0.5.cab",
            "ipfs://QmQ648kwvv52wuqPoKjm5zLGXngQnmuJzp1xtJmTEbzgz5"
          ],
          "Uri" : "https://fwupd.org/downloads/f7dd4ab29fa610438571b8b62b26b0b0e57bb35b-hughski-colorhug2-2.0.5.cab",
          "Homepage" : "http://www.hughski.com/",
          "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
          "Vendor" : "Hughski Limited",
          "Flags" : [
            "is-downgrade"
          ],
          "InstallDuration" : 8
        },
        {
          "AppstreamId" : "com.hughski.ColorHug2.firmware",
          "RemoteId" : "lvfs",
          "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
          "Description" : "<p>This unstable release adds the following features:</p><ul><li>Add TakeReadingArray to enable panel latency measurements</li><li>Speed up the auto-scaled measurements considerably, using 256ms as the smallest sample duration</li></ul>",
          "Version" : "2.0.2",
          "Filename" : "hughski-colorhug2-2.0.2.cab",
          "Protocol" : "com.hughski.colorhug",
          "Checksum" : [
            "1b43bd71bbed2cf0e9c9efcca79799f07b3d0dd2",
            "c09674fb818d4a1033dbde2fab5885716aed1d8b751b428f16687a78f2a4d61f"
          ],
          "License" : "GPL-2.0+",
          "Size" : 15680,
          "Created" : 1416675439,
          "Locations" : [
            "https://fwupd.org/downloads/30a121f26c039745aeb5585252d4a9b5386d71cb-hughski-colorhug2-2.0.2.cab",
            "ipfs://QmZ1DKKsWZQuvnff2DJTDJESMaXTpsc5zfNGX7Sb2HibAn"
          ],
          "Uri" : "https://fwupd.org/downloads/30a121f26c039745aeb5585252d4a9b5386d71cb-hughski-colorhug2-2.0.2.cab",
          "Homepage" : "http://www.hughski.com/",
          "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
          "Vendor" : "Hughski Limited",
          "Flags" : [
            "is-downgrade"
          ],
          "InstallDuration" : 8
        }
      ]
    },
    {
      "Name" : "Display controller",
      "DeviceId" : "ecf0d22adf39a244a723466378a8884aa22b7e78",
      "Guid" : [
        "e358a53d-98bc-5565-b55e-7df8e0d06c5e",
        "7365091f-756a-5c83-878c-edd1120ca718",
        "06208e9f-1dd0-5857-b700-3d77525793aa",
        "af9ff5a0-c613-5da3-bab8-5d411adebbca"
      ],
      "Plugin" : "optionrom",
      "Flags" : [
        "internal",
        "registered",
        "can-verify",
        "can-verify-image"
      ],
      "VendorId" : "PCI:0x1234",
      "Version" : "02",
      "VersionFormat" : "plain",
      "Icons" : [
        "audio-card"
      ],
      "Created" : 1614209932
    },
    {
      "Name" : "Intel(R) Core™ i7-7700HQ CPU @ 2.80GHz",
      "DeviceId" : "4bde70ba4e39b28f9eab1628f9dd6e6244c03027",
      "Guid" : [
        "b9a2dd81-159e-5537-a7db-e7101d164d3f",
        "30249f37-d140-5d3e-9319-186b1bd5cac3",
        "809a0b93-8a12-5338-a571-ad5583acf896",
        "d0f754d5-1395-5573-bc83-85ba955da70a"
      ],
      "Plugin" : "cpu",
      "Flags" : [
        "internal",
        "registered"
      ],
      "Vendor" : "Intel",
      "Version" : "0x000000de",
      "VersionFormat" : "hex",
      "VersionRaw" : 222,
      "Icons" : [
        "computer"
      ],
      "Created" : 1614209932
    }
  ]
}
"""

GET_DEVICES_NO_UPDATES = """{
    "Devices" : [
        {
            "Name" : "ColorHug2",
            "DeviceId" : "203f56e4e186d078ce76725e708400aafc253aac",
            "Guid" : [
                "2082b5e0-7a64-478a-b1b2-e3404fab6dad",
                "aa4b4156-9732-55db-9500-bf6388508ee3",
                "101ee86a-7bea-59fb-9f89-6b6297ceed3b",
                "2fa8891f-3ece-53a4-adc4-0dd875685f30"
            ],
            "Summary" : "An open source display colorimeter",
            "Plugin" : "colorhug",
            "Protocol" : "com.hughski.colorhug",
            "Flags" : [
                "updatable",
                "supported",
                "registered",
                "self-recovery",
                "add-counterpart-guids"
            ],
            "Vendor" : "Hughski Ltd.",
            "VendorId" : "USB:0x273F",
            "Version" : "2.0.7",
            "VersionFormat" : "triplet",
            "Icons" : [
                "colorimeter-colorhug"
            ],
            "InstallDuration" : 8,
            "Created" : 1592916092,
            "Releases" : [
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This release fixes prevents the firmware returning an error when the remote SHA1 hash was never sent.</p>",
                    "Version" : "2.0.7",
                    "Filename" : "658851e6f27c4d87de19cd66b97b610d100efe09",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "490be5c0b13ca4a3f169bf8bc682ba127b8f7b96"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1482901200,
                    "Uri" : "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on the second half of batch 16</li><li>Fix the firmware upgrade process using new versions of fwupd</li></ul>",
                    "Version" : "2.0.6",
                    "Filename" : "f038b5ca40e6d7c1c0299a9e1dcc129d5f6371b6",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "03c9c14db1894a00035ececcfae192865a710e52"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1450792062,
                    "Uri" : "https://fwupd.org/downloads/170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on batch 16</li><li>Make the self test more sensitive to detect floating pins</li></ul>",
                    "Version" : "2.0.5",
                    "Filename" : "ae76c6b704b60f9d1d88dc2c8ec8a62d7b2331dc",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "4ee9dfa38df3b810f739d8a19d13da1b3175fb87"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1444059405,
                    "Uri" : "https://fwupd.org/downloads/f7dd4ab29fa610438571b8b62b26b0b0e57bb35b-hughski-colorhug2-2.0.5.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This unstable release adds the following features:</p><ul><li>Add TakeReadingArray to enable panel latency measurements</li><li>Speed up the auto-scaled measurements considerably, using 256ms as the smallest sample duration</li></ul>",
                    "Version" : "2.0.2",
                    "Filename" : "d4b3144daeb2418634f9d464d88d55590bcd9ac7",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "448527af3ce019d03dbb77aaebaa7eb893f1ea20"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 15680,
                    "Created" : 1416675439,
                    "Uri" : "https://fwupd.org/downloads/30a121f26c039745aeb5585252d4a9b5386d71cb-hughski-colorhug2-2.0.2.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                }
            ]
        },
        {
            "Name" : "GP106 [GeForce GTX 1060 6GB]",
            "DeviceId" : "71b677ca0f1bc2c5b804fa1d59e52064ce589293",
            "Guid" : [
                "b080a9ba-fff8-5de0-b641-26f782949f94",
                "f95bfce3-18e4-58b0-bd81-136457521383"
            ],
            "Plugin" : "optionrom",
            "Flags" : [
                "internal",
                "registered",
                "can-verify",
                "can-verify-image"
            ],
            "Vendor" : "NVIDIA Corporation",
            "VendorId" : "PCI:0x10DE",
            "Version" : "a1",
            "VersionFormat" : "plain",
            "Icons" : [
                "audio-card"
            ],
            "Created" : 1592899254
        },
        {
            "Name" : "Intel(R) Core™ i5-8400 CPU @ 2.80GHz",
            "DeviceId" : "4bde70ba4e39b28f9eab1628f9dd6e6244c03027",
            "Guid" : [
                "b9a2dd81-159e-5537-a7db-e7101d164d3f"
            ],
            "Plugin" : "cpu",
            "Flags" : [
                "internal",
                "registered"
            ],
            "Vendor" : "GenuineIntel",
            "Version" : "0xd6",
            "VersionFormat" : "hex",
            "Icons" : [
                "computer"
            ],
            "Created" : 1592899249
        },
        {
            "Name" : "SSDPR-CX400-256",
            "DeviceId" : "948241a24320627284597ec95079cc1341c90518",
            "Guid" : [
                "09fa3842-45bc-5226-a8ec-1668fc61f88f",
                "57d6b2ff-710d-5cd2-98be-4f6b8b7c5287",
                "36bebd37-b680-5d56-83a1-6693033d4098"
            ],
            "Summary" : "ATA Drive",
            "Plugin" : "ata",
            "Protocol" : "org.t13.ata",
            "Flags" : [
                "internal",
                "updatable",
                "require-ac",
                "registered",
                "needs-reboot",
                "usable-during-update"
            ],
            "Vendor" : "Phison",
            "VendorId" : "ATA:0x1987",
            "Version" : "SBFM61.3",
            "VersionFormat" : "plain",
            "Icons" : [
                "drive-harddisk"
            ],
            "Created" : 1592899254
        }
    ]
}
"""


GET_DEVICES_NO_VERSION = """{
    "Devices" : [
        {
            "Name" : "ColorHug2",
            "DeviceId" : "203f56e4e186d078ce76725e708400aafc253aac",
            "Guid" : [
                "2082b5e0-7a64-478a-b1b2-e3404fab6dad",
                "aa4b4156-9732-55db-9500-bf6388508ee3",
                "101ee86a-7bea-59fb-9f89-6b6297ceed3b",
                "2fa8891f-3ece-53a4-adc4-0dd875685f30"
            ],
            "Summary" : "An open source display colorimeter",
            "Plugin" : "colorhug",
            "Protocol" : "com.hughski.colorhug",
            "Flags" : [
                "updatable",
                "supported",
                "registered",
                "self-recovery",
                "add-counterpart-guids"
            ],
            "Vendor" : "Hughski Ltd.",
            "VendorId" : "USB:0x273F",
            "VersionFormat" : "triplet",
            "Icons" : [
                "colorimeter-colorhug"
            ],
            "InstallDuration" : 8,
            "Created" : 1592916092,
            "Releases" : [
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This release fixes prevents the firmware returning an error when the remote SHA1 hash was never sent.</p>",
                    "Version" : "2.0.7",
                    "Filename" : "658851e6f27c4d87de19cd66b97b610d100efe09",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "490be5c0b13ca4a3f169bf8bc682ba127b8f7b96"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1482901200,
                    "Uri" : "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on the second half of batch 16</li><li>Fix the firmware upgrade process using new versions of fwupd</li></ul>",
                    "Filename" : "f038b5ca40e6d7c1c0299a9e1dcc129d5f6371b6",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "03c9c14db1894a00035ececcfae192865a710e52"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1450792062,
                    "Uri" : "https://fwupd.org/downloads/170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on batch 16</li><li>Make the self test more sensitive to detect floating pins</li></ul>",
                    "Version" : "2.0.5",
                    "Filename" : "ae76c6b704b60f9d1d88dc2c8ec8a62d7b2331dc",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "4ee9dfa38df3b810f739d8a19d13da1b3175fb87"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1444059405,
                    "Uri" : "https://fwupd.org/downloads/f7dd4ab29fa610438571b8b62b26b0b0e57bb35b-hughski-colorhug2-2.0.5.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This unstable release adds the following features:</p><ul><li>Add TakeReadingArray to enable panel latency measurements</li><li>Speed up the auto-scaled measurements considerably, using 256ms as the smallest sample duration</li></ul>",
                    "Version" : "2.0.2",
                    "Filename" : "d4b3144daeb2418634f9d464d88d55590bcd9ac7",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "448527af3ce019d03dbb77aaebaa7eb893f1ea20"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 15680,
                    "Created" : 1416675439,
                    "Uri" : "https://fwupd.org/downloads/30a121f26c039745aeb5585252d4a9b5386d71cb-hughski-colorhug2-2.0.2.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                }
            ]
        },
        {
            "Name" : "ColorHug2",
            "DeviceId" : "203f56e4e186d078ce76725e708400aafc253aac",
            "Guid" : [
                "2082b5e0-7a64-478a-b1b2-e3404fab6dad",
                "aa4b4156-9732-55db-9500-bf6388508ee3",
                "101ee86a-7bea-59fb-9f89-6b6297ceed3b",
                "2fa8891f-3ece-53a4-adc4-0dd875685f30"
            ],
            "Summary" : "An open source display colorimeter",
            "Plugin" : "colorhug",
            "Protocol" : "com.hughski.colorhug",
            "Flags" : [
                "updatable",
                "supported",
                "registered",
                "self-recovery",
                "add-counterpart-guids"
            ],
            "Vendor" : "Hughski Ltd.",
            "Version" : "2.0.6",
            "VendorId" : "USB:0x273F",
            "VersionFormat" : "triplet",
            "Icons" : [
                "colorimeter-colorhug"
            ],
            "InstallDuration" : 8,
            "Created" : 1592916092,
            "Releases" : [
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This release fixes prevents the firmware returning an error when the remote SHA1 hash was never sent.</p>",
                    "Version" : "2.0.7",
                    "Filename" : "658851e6f27c4d87de19cd66b97b610d100efe09",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "490be5c0b13ca4a3f169bf8bc682ba127b8f7b96"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1482901200,
                    "Uri" : "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on the second half of batch 16</li><li>Fix the firmware upgrade process using new versions of fwupd</li></ul>",
                    "Version" : "2.0.6",
                    "Filename" : "f038b5ca40e6d7c1c0299a9e1dcc129d5f6371b6",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "03c9c14db1894a00035ececcfae192865a710e52"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1450792062,
                    "Uri" : "https://fwupd.org/downloads/170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This stable release fixes the following problems:</p><ul><li>Fix the swapped LEDs on batch 16</li><li>Make the self test more sensitive to detect floating pins</li></ul>",
                    "Version" : "2.0.5",
                    "Filename" : "ae76c6b704b60f9d1d88dc2c8ec8a62d7b2331dc",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "4ee9dfa38df3b810f739d8a19d13da1b3175fb87"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 16384,
                    "Created" : 1444059405,
                    "Uri" : "https://fwupd.org/downloads/f7dd4ab29fa610438571b8b62b26b0b0e57bb35b-hughski-colorhug2-2.0.5.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                },
                {
                    "AppstreamId" : "com.hughski.ColorHug2.firmware",
                    "RemoteId" : "lvfs",
                    "Summary" : "Firmware for the Hughski ColorHug2 Colorimeter",
                    "Description" : "<p>This unstable release adds the following features:</p><ul><li>Add TakeReadingArray to enable panel latency measurements</li><li>Speed up the auto-scaled measurements considerably, using 256ms as the smallest sample duration</li></ul>",
                    "Version" : "2.0.2",
                    "Filename" : "d4b3144daeb2418634f9d464d88d55590bcd9ac7",
                    "Protocol" : "com.hughski.colorhug",
                    "Checksum" : [
                        "448527af3ce019d03dbb77aaebaa7eb893f1ea20"
                    ],
                    "License" : "GPL-2.0+",
                    "Size" : 15680,
                    "Created" : 1416675439,
                    "Uri" : "https://fwupd.org/downloads/30a121f26c039745aeb5585252d4a9b5386d71cb-hughski-colorhug2-2.0.2.cab",
                    "Homepage" : "http://www.hughski.com/",
                    "SourceUrl" : "https://github.com/hughski/colorhug2-firmware",
                    "Vendor" : "Hughski Limited",
                    "Flags" : [
                        "is-downgrade"
                    ],
                    "InstallDuration" : 8
                }
            ]
        },
        {
            "Name" : "GP106 [GeForce GTX 1060 6GB]",
            "DeviceId" : "71b677ca0f1bc2c5b804fa1d59e52064ce589293",
            "Guid" : [
                "b080a9ba-fff8-5de0-b641-26f782949f94",
                "f95bfce3-18e4-58b0-bd81-136457521383"
            ],
            "Plugin" : "optionrom",
            "Flags" : [
                "internal",
                "registered",
                "can-verify",
                "can-verify-image"
            ],
            "Vendor" : "NVIDIA Corporation",
            "VendorId" : "PCI:0x10DE",
            "VersionFormat" : "plain",
            "Icons" : [
                "audio-card"
            ],
            "Created" : 1592899254
        },
        {
            "Name" : "Intel(R) Core™ i5-8400 CPU @ 2.80GHz",
            "DeviceId" : "4bde70ba4e39b28f9eab1628f9dd6e6244c03027",
            "Guid" : [
                "b9a2dd81-159e-5537-a7db-e7101d164d3f"
            ],
            "Plugin" : "cpu",
            "Flags" : [
                "internal",
                "registered"
            ],
            "Vendor" : "GenuineIntel",
            "Version" : "0xd6",
            "VersionFormat" : "hex",
            "Icons" : [
                "computer"
            ],
            "Created" : 1592899249
        },
        {
            "Name" : "SSDPR-CX400-256",
            "DeviceId" : "948241a24320627284597ec95079cc1341c90518",
            "Guid" : [
                "09fa3842-45bc-5226-a8ec-1668fc61f88f",
                "57d6b2ff-710d-5cd2-98be-4f6b8b7c5287",
                "36bebd37-b680-5d56-83a1-6693033d4098"
            ],
            "Summary" : "ATA Drive",
            "Plugin" : "ata",
            "Protocol" : "org.t13.ata",
            "Flags" : [
                "internal",
                "updatable",
                "require-ac",
                "registered",
                "needs-reboot",
                "usable-during-update"
            ],
            "Vendor" : "Phison",
            "VendorId" : "ATA:0x1987",
            "Version" : "SBFM61.3",
            "VersionFormat" : "plain",
            "Icons" : [
                "drive-harddisk"
            ],
            "Created" : 1592899254
        }
    ]
}
"""

HEADS_XML = """<?xml version='1.0' encoding='utf-8'?>
<components origin="lvfs" version="0.9">
  <component type="firmware">
    <id>com.3mdeb.heads.x230.firmware</id>
    <name>Heads x230 System Update</name>
    <summary>x230 heads system firmware</summary>
    <description>
      <p>x230 heads system firmware</p>
    </description>
    <provides>
      <firmware type="flashed">596c3466-0506-5ca5-a68f-dc34532a93d3</firmware>
    </provides>
    <url type="homepage">http://osresearch.net/</url>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>GPLv2</project_license>
    <developer_name>coreboot</developer_name>
    <X-categories>
      <category>X-System</category>
    </X-categories>
    <releases>
      <release version="0.2.3" timestamp="1599436800" urgency="high">
        <location>https://fwupd.org/downloads/e747a435bf24fd6081b77b6704b39cec5fa2dcf62e0ca6b86d8a6460121a1d07-heads_coreboot_x230-v0_2_3.cab</location>
        <checksum type="sha1" filename="e747a435bf24fd6081b77b6704b39cec5fa2dcf62e0ca6b86d8a6460121a1d07-heads_coreboot_x230-v0_2_3.cab" target="container">1a54e69ca2b58d1218035115d481480eaf4c66e4</checksum>
        <checksum type="sha256" filename="e747a435bf24fd6081b77b6704b39cec5fa2dcf62e0ca6b86d8a6460121a1d07-heads_coreboot_x230-v0_2_3.cab" target="container">ba519a7a5d8136c8ade0cf0c775c58f3165f42798ff631c3f57f075897ef1586</checksum>
        <checksum type="sha1" filename="firmware.rom" target="content">76373f1b5a157b6563d3605271472901b03f57f3</checksum>
        <checksum type="sha256" filename="firmware.rom" target="content">9a9c5dbd3faf90ff7a1f4c9be8d71c4db93dd69fa690f8722fec19c5a51aed9e</checksum>
        <description>
          <p>Fixes flash-gui issue.</p>
        </description>
        <size type="installed">12582912</size>
        <size type="download">12591670</size>
      </release>
      <release version="0.2.2" timestamp="1599436800" urgency="high">
        <location>https://fwupd.org/downloads/1a0f0ad487a40bb27a49db55e256a207a33dac92c5c53761501c9fb89e4fd115-heads_coreboot_x230-v0_2_2.cab</location>
        <checksum type="sha1" filename="1a0f0ad487a40bb27a49db55e256a207a33dac92c5c53761501c9fb89e4fd115-heads_coreboot_x230-v0_2_2.cab" target="container">58e85d012ad1d5c6f98e8fe65202b4d6c8a6ec03</checksum>
        <checksum type="sha256" filename="1a0f0ad487a40bb27a49db55e256a207a33dac92c5c53761501c9fb89e4fd115-heads_coreboot_x230-v0_2_2.cab" target="container">94430160d35cf74adf29c7fc1490b44497e1a3f0fff72733efe2982c61c9a772</checksum>
        <checksum type="sha1" filename="firmware.rom" target="content">8e97ce38396e281fcf9a5a248819925a2fa04265</checksum>
        <checksum type="sha256" filename="firmware.rom" target="content">a6774661407622f345bf0ac2f113540507f0288bb97bf5dba586059c0653f659</checksum>
        <description>
          <p>Lenovo x230 heads system firmware</p>
        </description>
        <size type="installed">12582912</size>
        <size type="download">12591680</size>
      </release>
    </releases>
  </component>
</components>
"""
