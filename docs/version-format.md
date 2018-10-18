Version Formats
===============

In some circumstances fwupd has to convert from a unsigned integer version
number into something that has either been used in documentation or has been
defined in some specification.
A good example here is the UEFI ESRT table, which specifies a `uint32_t` for
the version but does not specify how this should be formatted for the user.

As is typical in underspecified specifications, vendors have converted the
integer in different ways. For instance, Dell uses version strings like 1.2.3
and Microsoft use versions like 1.2.3.4.

The fwudp daemon can match specific devices and apply the correct version style
using quirk files. The version format can also be specified in the firmware
`metainfo.xml` file so that the new version is correctly shown, and so that it
matches on the LVFS website.

The current version formats supported by fwupd and the LVFS are:

 * `plain`:     Use plain integer version numbers with no dots, e.g. `AABBCCDD`
 * `quad`:      Use Dell-style `AA.BB.CC.DD` version numbers
 * `triplet`:   Use Microsoft-style `AA.BB.CCDD` version numbers
 * `pair`:      Use two `AABB.CCDD` version numbers
 * `bcd`:       Use binary coded decimal notation
 * `intel-me`:  Use Intel ME-style notation (`aaa+11.bbbbb.CC.DDDD`)
 * `intel-me2`: Use alternate Intel ME-style-style `A.B.CC.DDDD` notation

These can be specified in quirk files like this:

    # Vendor Modelname
    [Guid=5b92717b-2cad-4a96-a13b-9d65781df8bf]
    VersionFormat = intel-me2

...or in metainfo.xml files like this:

    <custom>
      <value key="LVFS::VersionFormat">intel-me2</value>
    </custom>

Runtime requirements
--------------------

Versions of fwupd `< 1.2.0` can only support firmware updates with key values
`LVFS::VersionFormat` of `quad` and `triplet`. Additionally, on older versions
no quirk `VersionFormat` device fixups are supported.

If want to use one of the additional version formats you should depend on a
specific version of fwupd in the firmware file:

    <requires>
      <id compare="ge" version="1.2.0">org.freedesktop.fwupd</id>
    </requires>

This is not *strictly* required, as the integer value can be used for update
calculations if the version is specified in hex (e.g. `0x12345678`) in the
`<release>` tag, although the user might get a bit confused if the update
version does not match the update description.
