Vendor Firmware
===============

These are the steps to add vendor that is installed as part of an OSTree image:

* Change `/etc/fwupd/remotes.d/vendor.conf` to have `Enabled=true`
* Deploy the firmware to `/usr/share/fwupd/remotes.d/vendor/firmware`
* Deploy the metadata to `/usr/share/fwupd/remotes.d/vendor/vendor.xml`

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
