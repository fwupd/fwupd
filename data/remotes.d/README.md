Vendor Firmware
===============

These are the steps to add vendor firmware that is installed as part of an embedded image such as an OSTree or ChromeOS image:

* Change `/etc/fwupd/remotes.d/vendor.conf` to have `Enabled=true`
* Change `/etc/fwupd/remotes.d/vendor.conf` to have the correct `Title`
* Deploy the firmware to `/usr/share/fwupd/remotes.d/vendor/firmware`
* Deploy the metadata to `/usr/share/fwupd/remotes.d/vendor/vendor.xml.gz`

The metadata should be of the form:

    <?xml version="1.0" encoding="UTF-8"?>
    <components version="0.9">
      <component type="firmware">
        <id>FIXME.firmware</id>
        <name>FIXME</name>
        <summary>FIXME</summary>
        <developer_name>FIXME</developer_name>
        <project_license>FIXME</project_license>
        <description><p>FIXME</p></description>
        <url type="homepage">http://FIXME</url>
        <releases>
          <release version="FIXME" date="2017-07-27" urgency="high">
            <size type="installed">86406</size>
            <location>firmware/FIXME.cab</location>
            <checksum filename="FIXME.hex" target="content" type="sha1">96a92915c9ebaf3dd232cfc7dcc41c1c6f942877</checksum>
            <description><p>FIXME.</p></description>
          </release>
        </releases>
        <provides>
          <firmware type="flashed">FIXME</firmware>
        </provides>
      </component>
    </components>

Ideally, the metadata and firmware should be signed by either GPG or a PKCS7
certificate. If this is the case also change `Keyring=gpg` or `Keyring=pkcs7`
in `/etc/fwupd/remotes.d/vendor.conf` and ensure the correct public key or
signing certificate is installed in the `/etc/pki/fwupd` location.

Automatic metadata generation
=============================
`fwupd` and `fwupdtool` support automatically generating metadata for a remote
by configuring it to be a *directory* type. This is very convenient if you want to dynamically add firmware from multiple packages while generating the image but there are a few deficiencies:
* There will be a performance impact of starting the daemon or tool measured by O(# CAB files)
* It's not possible to verify metadata signature and any file validation should be part of the image validation.

To enable this:
* Change `/etc/fwupd/remotes.d/vendor-directory.conf` to have `Enabled=true`
* Change `/etc/fwupd/remotes.d/vendor.conf-directory` to have the correct `Title`
* Deploy the firmware to `/usr/share/fwupd/remotes.d/vendor/firmware`
* Change `MetadataURI` to that of the directory (Eg `/usr/share/fwupd/remotes.d/vendor/`)


Mirroring a Repository
======================

The LVFS currently outputs XML with absolute URI locations, e.g.
`<location>http://foo/bar.cab</location>` rather than `<location>bar.cab</location>`

This makes mirroring the master LVFS (or other private instance) somewhat tricky.
To work around this issue client remotes can specify `FirmwareBaseURI` to
replace the URI of the firmware before it is downloaded.

For mirroring the LVFS content to a new CDN, you could use:

    [fwupd Remote]
    Enabled=true
    Type=download
    Keyring=gpg
    MetadataURI=https://my.new.cdn/mirror/firmware.xml.gz
    FirmwareBaseURI=https://my.new.cdn/mirror

New instances of the LVFS can actually output a relative URL for firmware files,
e.g. `<location>bar.cab</location>` and when downloading the `MetadataURI` name
and path prefix is used in this case.
This is not enabled for the "upstream" LVFS instance as versions of fwupd older
than 1.0.3 are unable to automatically use the `MetadataURI` value for firmware
downloads.
