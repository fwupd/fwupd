# Remotes

## Vendor Firmware

These are the steps to add vendor firmware that is installed as part of an embedded image such as an OSTree or ChromeOS image:

* Compile with `-Dvendor_metadata=true` to install `/etc/fwupd/remotes.d/vendor.conf`
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

The metadata and firmware should be signed using Jcat, ensuring the
signing certificate is installed in the `/etc/pki/fwupd` location.

## Automatic metadata generation

`fwupd` and `fwupdtool` support automatically generating metadata for a remote
by configuring it to be a *directory* type. This is very convenient if you want to dynamically add firmware from multiple packages while generating the image but there are a few deficiencies:

* There will be a performance impact of starting the daemon or tool measured by O(# CAB files)
* It's not possible to verify metadata signature and any file validation should be part of the image validation.

To enable this:

* Change `/etc/fwupd/remotes.d/vendor-directory.conf` to have `Enabled=true`
* Change `/etc/fwupd/remotes.d/vendor-directory.conf` to have the correct `Title`
* Deploy the firmware to `/usr/share/fwupd/remotes.d/vendor/firmware`
* Change `MetadataURI` to that of the directory (Eg `/usr/share/fwupd/remotes.d/vendor/`)

## Mirroring a Repository

The upstream LVFS instance will output a relative URL for firmware files, e.g.
`<location>bar.cab</location>` instead of an absolute URI location, e.g.
`<location>http://foo/bar.cab</location>`.

When setting up a mirror of the LVFS onto another CDN you just need to change
the `MetadataURI` to your local mirror and firmware downloads will use the
relative URI.
