fwupd
=====

fwupd is a simple daemon to allow session software to update device firmware on
your local machine. It's designed for desktops, but this project is probably
quite interesting for phones, tablets and server farms, so I'd be really happy
if this gets used on other non-desktop hardware.

You can either use a GUI software manager like GNOME Software to view and apply
updates, the command-line tool or the system D-Bus interface directly.

Introduction
------------

Updating firmware easily is actually split into two parts:

 * Providing metadata about what vendor updates are available (AppStream)
 * A mechanism to actually apply the file onto specific hardware (this project)

What do we actually need to apply firmware easily? A raw binary firmware file
isn't so useful, and so Microsoft have decided we should all package it up in a
.cab file (a bit like a .zip file) along with a .inf file that describes the
update in more detail. The .inf file gives us the hardware ID of what the
firmware is referring to, as well as the vendor and a short update description.

So far the update descriptions have been less than awesome so we also need some
way of fixing up some of the update descriptions to be suitable to show the user.

I'm asking friendly upstream vendors to start shipping a MetaInfo file alongside
the .inf file in the firmware .cab file. This means we can have fully localized
update descriptions, along with all the usual things you'd expect from an
update, e.g. the upstream vendor, the licensing information, etc.

Of course, a lot of vendors are not going to care about good descriptions, and
won't be interested in shipping another file in the update just for Linux users.
For that, we can actually "inject" a replacement MetaInfo file when we curate
the AppStream metadata. This allows us to download all the .cab files we care
about, but are not allowed to redistribute, run the `appstream-builder` on them,
then package up just the XML metadata which can be consumed by pretty much any
distribution tool.

A lot of people don't have UEFI hardware that is capable of applying capsule
firmware updates, so I've also added a ColorHug provider, which predictably also
lets you update the firmware on your ColorHug device. It's a lot lower risk
testing all this code with a £20 device than your nice shiny expensive prototype
hardware.

I'm also happy to accept patches for other hardware that supports updates,
although the internal API isn't 100% stable yet. The provider concept allows
vendors to do pretty much anything to get the list of attached hardware, as long
as a unique hardware component is in some way mapped to a GUID value.
Ideally the tools would be open source, or better still not needing any external
tools at all. Reading a VID/PID and then writing firmware to a chip usually
isn't rocket science.

What is standardised is the metadata, using AppStream 0.9 as the interchange
format. A lot of tools already talk AppStream and so this makes working with
other desktop and server tools very easy. Actually generating the AppStream
metadata can either be done using using `appstream-builder`, or some random
vendor-specific non-free Perl/C++/awk script that operates on internal data;
the point is that as long as the output format is AppStream and the metadata
GUID matches the hardware GUID we don't really care.

Security
--------

By default, any users are able to install firmware to removable hardware.
The logic here is that if the hardware can be removed, it can easily be moved to
a device that the user already has root access on, and asking for authentication
would just be security theatre.

For non-removable devices, e.g. UEFI firmware, admin users are able to update
firmware without the root password. By default, we already let admin user and
root update glibc and the kernel without additional authentication, and these
would be a much easier target to backdoor. The firmware updates themselves
have a checksum, and the metadata describing this checksum is provided by the
distribution either as GPG-signed repository metadata, or installed from a
package, which is expected to also be signed. It is important that clients that
are downloading firmware for fwupd check the checksum before asking fwupd to
update a specific device.

User Interaction
----------------

No user interaction should be required when actually applying updates. Making
it prohibited means we can do the upgrade with a fancy graphical splash screen,
without having to worry about locales and input methods. Updating firmware
should be no more dangerous than installing a new kernel or glibc package.

Offline Updates Lifecycle
-------------------------

Offline updates are done using a special boot target which means that the usual
graphical environment is not started. Once the firmware update has completed the
system will reboot.

Devices go through the following lifecycles:

 * created -> `SCHEDULED` -> `SUCCESS` -> deleted
 * created -> `SCHEDULED` -> `FAILED` -> deleted

Any user-visible output is available using the `GetResults()` D-Bus method, and
the database entry is only deleted once the `ClearResults()` method is called.

The results are obtained and cleared either using a provider-supplied method
or using a small SQLite database located at `/var/lib/fwupd/pending.db`

ColorHug Support
----------------

You need to install colord 1.2.9 which may be newer that your distribution
provides. Compile it from source https://github.com/hughsie/colord or grab the
RPMS here http://people.freedesktop.org/~hughsient/fedora/

If you don't want or need this functionality you can use `--disable-colorhug`

UEFI Support
------------

If you're wondering where to get fwupdate from, either compile it form source
(you might also need a newer efivar) from https://github.com/rhinstaller/fwupdate
or grab the RPMs here https://pjones.fedorapeople.org/fwupdate/

If you don't want or need this functionality you can use `--disable-uefi`

Vendor Firmware Updates
=======================

This document explains what steps a vendor needs to take so that firmware
updates are downloaded and applied to user hardware automatically.

Different hardware update methods can be supported, but would require a new
plugin and there would need to be interfaces available to be able to write
(or at least trigger) the firmware from userspace as the root user.

What do I have to do?
---------------------

As per the [ Microsoft guidelines](https://msdn.microsoft.com/en-us/library/windows/hardware/dn917810%28v=vs.85%29.aspx),
package up your firmware into a `.cab` file, with these files inside:

* The actual `.cap` file your engineers have created
* The `.inf` file describing the .cap file,
  described [here](https://msdn.microsoft.com/en-us/library/windows/hardware/ff547402%28v=vs.85%29.aspx)
* The optional `.asc` file which is a detached GPG signature of the firmware file.
* The optional `.metainfo.xml` file with a long description and extra metadata,
  described [here](http://www.freedesktop.org/software/appstream/docs/sect-Quickstart-Addons.html)

You can create a `.cab` file using `makecab.exe` on Windows and `gcab --create`
on Linux.

It is recommended you name the `.cab` file with the hardware name and the version
number, e.g. `colorhug-als-1.2.3.cab`. It's mandatory that the files inside the
`.cab` file have the same basename, for example this is would be valid:

    colorhug2-1.2.3.cab
     |- firmware.inf
     |- firmware.bin
     |- firmware.bin.asc
     \- firmware.metainfo.xml

An example `.inf` file might look like this:

```ini
[Version]
Class=Firmware
ClassGuid=84f40464-9272-4ef7-9399-cd95f12da696
DriverVer=03/03/2015,3.0.2

[Firmware_CopyFiles]
firmware.bin
```

An example `.metainfo.xml` file might look like this:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- Copyright 2015 Richard Hughes <richard@hughsie.com> -->
<component type="firmware">
  <id>84f40464-9272-4ef7-9399-cd95f12da696</id>
  <name>ColorHugALS Firmware</name>
  <summary>Firmware for the ColorHugALS Ambient Light Sensor</summary>
  <description>
    <p>
      Updating the firmware on your ColorHugALS device improves performance and
      adds new features.
    </p>
  </description>
  <url type="homepage">http://www.hughski.com/</url>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-2.0+</project_license>
  <developer_name>Hughski Limited</developer_name>
  <releases>
    <release version="3.0.2" timestamp="1424116753">
      <location>http://www.hughski.com/downloads/colorhug-als/firmware/colorhug-als-3.0.2.cab</location>
      <description>
        <p>This stable release fixes the following bugs:</p>
        <ul>
          <li>Fix the return code from GetHardwareVersion</li>
          <li>Scale the output of TakeReadingRaw by the datasheet values</li>
        </ul>
      </description>
    </release>
  </releases>
</component>
```

If the firmware is not redistributable you have to indicate it in in the
`.metainfo.xml` file with `<project_license>proprietary</project_license>`.
It is then **very important** you also provide a download location in the
`.metainfo.xml` file.

Questions:
----------

### Where will this data be used?

We will scrape the `.inf` and `.metainfo.xml` files when building and composing
metadata for distributions; end users will still be downloading the `.cab`
files directly from the vendor site.

### How do I know if my appdata XML is correct?

The best way to validate the data is by using the `appstream-util validate`
tool available from the [appstream-glib](https://github.com/hughsie/appstream-glib) project.

### Where do I submit the `.cab` files?

The end goal is for vendors to produce and upload the AppStream metadata
themselves using the `appstream-builder` command line tool, for example:

```sh
appstream-builder                \
    --basename=colorhug-firmware \
    --origin=hughski             \
    ColorHug*/firmware-releases/*.*.*/*.cab
```

...will produce this file: http://www.hughski.com/downloads/colorhug-firmware.xml

Please [email us](mailto://richard@hughsie.com) if you just want to upload `.cab`
files and you would like us to generate metadata for your product.

### How does fwupd know the device firmware version?

For generic USB devices you can use a firmware version extension that is used
by a few OpenHardware projects. This means the fwupd daemon can obtain the
firmware version without claiming the interface on the device and preventing
other software from using it straight away.
For closed-source devices a product-specific provider can be used, although
this isn't covered here.

To implement the firmware version extension just create an interface descriptor
with class code `0xff`, subclass code `0x46` and protocol `0x57` pointing to a
string descriptor with the firmware version.
An example commit to the ColorHug project can be found [here](https://github.com/hughski/colorhug2-firmware/commit/5e1bb64ad722a9d2d95927e305fd869b4a3a46a8).

Adding Trusted Keys
===================

Introduction:
------------

Installing a public key to `/etc/pki/fwupd` allows firmware signed with a
matching private key to be recognized as trusted, which may require less
authentication to install than for untrusted files. By default trusted firmware
can be **upgraded** (but not downgraded) without the user or administrator
password.

By default only very few keys will be installed *by default*. These are vendors
who have a proven security track record and a thorough understanding of
public-private key security.

In particular, private keys should **only** be kept on trusted hardware (or
virtual machine) that has limited network access, or networking completely
disabled. The machine and any backups also need to be kept physically secure.

Adding a New Key
----------------

If you think your key should be added by default and trusted by all users,
please open a pull request with details about your company including items such
as a day time phone number and any relevant security policies already in place.
