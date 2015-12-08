
DFU File Format - Metadata Store Proposal
=========================================

Introduction
------------

The DFU specification version 1.1 defines some target-specific data that can
optionally be included in the DFU file to ease firmware deployment.
These include items such as the runtime vendor, product and device release,
but nothing else and with no provision for extra metadata.

The DFU file specification does not specify any additional data structures
allowing vendors to include additional metadata, although does provide the
chance to include future additional data fields in the header in a backwards and
forwards compatible way by increasing the `header_len` value.

All software reading and writing DFU-format files should already be reading the
footer length from the file, rather than assuming a fixed footer length of 0x10
bytes.
This ensures that only the raw device firmware is sent to the device and not any
additional data fields added in future versions of the DFU specification.

There are valid reasons why we would want to add additional metadata into the
distributed DFU file. Reasons are listed as follows:

 * Legal compliance, to tag a file with copyright and licensing information
 * Business-specific metadata, for instance the SHA-1 git commit for the source
 * Cryptographic information such as a SHA-256 hash or a detached GPG signature

Although the original authors of the specification allowed for future additions
to the specification, they only allowed us to extend the footer by 239 bytes as
the `header_len` value is specified as just one byte, and 16 bytes are already
specified by the specification.

This would explain why some vendors are using vendor-specific file prefix data
segments, for instance the DfuSe prefix specification from ST.
This specification is not aiming to expand or standardize the various
incompatible vendor-specific prefix specifications, but tries to squeeze the
additional metadata into the existing DFU footer space which is compatible with
all existing DFU-compliant software.

Specification
-------------

An additional structure would be present after the binary firmware data, and
notionally contained within the DFU footer itself, although specified as a
seporate object.

The representation in memory and on disk would be as follows:

    uint16      signature='MD'
    uint8       number_of_keys
    uint8       key(n)_length
    ...         key(n) (no NUL)
    uint8       value(n)_length
    ...         value(n) (no NUL)
    <existing DFU footer>

If there are no metadata keys being set, it is expected that the metadata table
signature is not be written to the file, and that the footer should be again
0x10 bytes in length.

The signature of `MD` should also be checked before attempting to parse the
metadata store structure to ensure other vendor-specific extensions are not
already in use.

The key and value fields should be parsed as UTF-8, although in the pursuit of
space minimisation ASCII values are preferred where possible.

Example
-------

The following table shows an example firmware file with the payload 'DATA' set
with vendor 0x1234, product 0xABCD and no metadata table.

    neg.
    offset  description     byte
    ----------------------------
    13      firmware'D'     0x44
    12      firmware'A'     0x41
    11      firmware'T'     0x54
    10      firmware'A'     0x44
    0f      bcdDevice       0xFF
    0e      bcdDevice       0xFF
    0d      idProduct       0xCD
    0c      idProduct       0xAB
    0b      idVendor        0x34
    0a      idVendor        0x12
    09      bcdDFU          0x00
    08      bcdDFU          0x01
    07      ucDfuSig'U'     0x55
    06      ucDfuSig'F'     0x46
    05      ucDfuSig'D'     0x44
    04      bLength         0x10
    03      dwCRC           0x52
    02      dwCRC           0xB4
    01      dwCRC           0xE5
    00      dwCRC           0xCE

The following table shows a second firmware file with the same payload but
with the addition of a metadata table with a single metadata pair of `test=val`:

    neg.
    offset  description     byte
    ----------------------------
    1f      firmware'D'     0x44
    1e      firmware'A'     0x41
    1d      firmware'T'     0x54
    1c      firmware'A'     0x44
    1b      ucMdSig'M'      0x4D
    1a      ucMdSig'D'      0x44
    19      bMdLength       0x01
    18      bKeyLen         0x04
    17      KeyData't'      0x74
    16      KeyData'e'      0x65
    15      KeyData's'      0x73
    14      KeyData't'      0x74
    13      bValueLen       0x03
    12      ValueData'v'    0x76
    11      ValueData'a'    0x61
    10      ValueData'l'    0x6c
    0f      bcdDevice       0xFF
    0e      bcdDevice       0xFF
    0d      idProduct       0xCD
    0c      idProduct       0xAB
    0b      idVendor        0x34
    0a      idVendor        0x12
    09      bcdDFU          0x00
    08      bcdDFU          0x01
    07      ucDfuSig'U'     0x55
    06      ucDfuSig'F'     0x46
    05      ucDfuSig'D'     0x44
    04      bLength         0x1C
    03      dwCRC           0x1B
    02      dwCRC           0x25
    01      dwCRC           0x6D
    00      dwCRC           0xF5

Conclusions
-----------

The metadata store proposal allows us to store a small amount of metadata
inside the DFU file footer.
If the original specification had included just one more byte for the footer
length (for instance a `uint16`, rather than a `uint8`) type then I would have
proposed a key type allowing integers, IEEE floating point, and strings, and
also made the number of keys and the length of keys much larger.
Working with what we've been given, we can support a useful easy-to-parse
extension that allows us to solve some of the real-world problems vendors are
facing when trying to distribute firmware files for devices that support
in-the-field device firmware upgrading.

Several deliberate compomises have been made to this proposal due to the
restricted space available:

 * The metadata table signature is just two bytes
 * The number of keys is limited to just 59 pairs
 * Keys are limited to just 233 chars maximum
 * Values are limited to just 233 chars maximum
 * Types are limited to just strings (which can includes empty strings)
 * Strings are written without a `NUL` trailing byte
 * The metadata table uses variable offsets rather than fixed sizes

The key-value length in particular leads to some other best practices:

* A value for the 'License' key should be in SPDX format, NOT the full licence
* A value for the 'Copyright' key should just be the company name

The author is not already aware of any vendors using this additional data area,
but would be willing to work with any vendors who have implemented a similar
proprietary extension already or are planning to do so.
