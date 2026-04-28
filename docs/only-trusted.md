---
title: Signing Test Firmware Payloads
---

## Firmware

In the normal vendor update flow, firmware is optionally signed and encrypted by the vendor, and
then uploaded to the LVFS wrapped in a cabinet archive with a small XML metadata file to describe
how the firmware should be matched to hardware.

Upon uploading, the LVFS signs the firmware and metadata XML contained in the archive and adds it to
a `.jcat` file also included in the image.
The original firmware is never modified, which is why the Jcat file exists as both a detached
checksum (SHA-256 and SHA-512) and a detached signature.
The LVFS uses several PKCS#7 signatures in the Jcat file.

The keys in `/etc/pki/fwupd` and `/etc/pki/fwupd-metadata` are used for per-system trust and are
currently used for "did the firmware update *come from* somewhere I trust" rather than "verify the
vendor signed the update" -- on the logic the "signed the update" is probably already covered by
a signature on the payload that the device verifies.
Notably, The LVFS both verifies the vendor OEM → ODM → IHV relationships and assigns restrictions
on what devices each legal entity can upload for.

There's no way to separate the keys so that you could say "only use this certificate for
per-system-trust when the DMI vendor of the device is Dell" and there's no way to do key rotation
or revocation.
The trusted certificate mechanism was not really designed for any keys except the static LVFS.

## JCat Format

JCat is a gzip-compressed JSON catalog file, which can be used to store GPG, PKCS-7, SHA-256 and
SHA-512 checksums for each file.
This provides equivalent functionality to the catalog files supported in Microsoft Windows.

Each JSON file is logically divided into three structures:

### FwupdJcatBlob

The 'signature' blob, which can be a proper detached signature like PKCS-7 or just a checksum like
SHA-256.

### FwupdJcatItem

Items roughly approximate single files, and can have multiple JcatBlobs assigned.
In a firmware archive there are two items, with IDs `firmware.bin` and `firmware.metainfo.xml`.

### FwupdJcatFile

The container which contains one or multiple `JcatItem`s.

## Self Signing

Jcat files can be signed using a certificate and key that are automatically generated on your local
computer.
This means you can only verify the Jcat archive on the same computer (and probably the same user)
that you use to sign the archive.

It does however mean you can skip manually generating a secret key and public key pair.
If you do upload the public certificate up to a web service (for instance the LVFS) it does mean it
can verify your signatures.

    $ fwupdtool jcat-self-sign firmware.jcat firmware.bin
    $ fwupdtool jcat-info firmware.jcat
    FwupdJcatFile:
      Version:               0.1
      FwupdJcatItem:
        ID:                  firmware.bin
        FwupdJcatBlob:
          Kind:              pkcs7
          Flags:             is-utf8
          Timestamp:         2020-03-05T12:06:42Z
          Size:              0x2d9
          Data:              -----BEGIN PKCS7-----
                             MIIB9wYJKoZIhvcNAQcCoIIB6DCCAeQCAQExDTALBglghkgBZQMEAgEwCwYJKoZI
                             ...
                             oDd2UcfqgdQnihpYf0NaPDYhpcP5r7dmH1XN
                             -----END PKCS7-----

## Public Key Signing

Jcat can of course sign the archive with proper keys too.
Here we will generate a private and public key ourselves, but you should probably talk to your IT
department security team and ask them how to get a user certificate that's been signed by the
corporate CA certificate.

Lets create our own certificate authority (CA) and issue a per-user key for local testing.
Never use these in any kind of production system!

    $ ../../contrib/build-certs.py
    $ ls ACME-CA.* rhughes*
    ACME-CA.key  ACME-CA.pem  rhughes.csr  rhughes.key  rhughes.pem  rhughes_signed.pem

Then we can actually use both files:

    $ fwupdtool jcat-sign firmware.jcat firmware.bin rhughes_signed.pem rhughes.key
    FwupdJcatFile:
      Version:               0.1
      FwupdJcatItem:
        ID:                  firmware.bin
        FwupdJcatBlob:
          Kind:              pkcs7
          Flags:             is-utf8
          Timestamp:         2020-03-05T12:16:30Z
          Size:              0x373
          Data:              -----BEGIN PKCS7-----
                             MIICZwYJKoZIhvcNAQcCoIICWDCCAlQCAQExDTALBglghkgBZQMEAgEwCwYJKoZI
                             ...
                             8jggo0FbhDSs8frXhr1BHKBktOPKEbA3sETxlbHViYt6oldpi1uszV0kHA==
                             -----END PKCS7-----

Lets verify this new signature:

    $ fwupdtool jcat-verify firmware.jcat
    firmware.bin:
        FAILED pkcs7: failed to verify data for O=ACME Corp.,CN=ACME CA: Public key signature verification has failed. [-89]
        FAILED: Validation failed
    Validation failed

Ahh, of course; we need to tell Jcat to load our generated CA certificate:

    $ fwupdtool jcat-verify firmware.jcat --public-keys .
    firmware.bin:
        PASSED pkcs7: O=ACME Corp.,CN=ACME CA

We can then check the result using

    $ fwupdtool jcat-export firmware.jcat
    Wrote ./firmware.bin-com.redhat.rhughes.p7b
    $ certtool --p7-verify --infile firmware.bin-com.redhat.rhughes.p7b --load-data firmware.bin --load-ca-certificate=ACME-CA.pem
    Loaded CAs (1 available)
    eContent Type: 1.2.840.113549.1.7.1
    Signers:
        Signer's issuer DN: O=ACME Corp.,CN=ACME CA
        Signer's serial: 4df758978d0601c6500ab6f266963916d8b7ab33
        Signature Algorithm: RSA-SHA256
        Signature status: ok

## Large Payloads

It may be impractical to load the entire binary into RAM for verification.
For this use case, jcat supports signing the *checksum of the payload* as the target rather than the
payload itself.

    $ fwupdtool jcat-self-sign firmware.jcat firmware.bin sha256
    $ fwupdtool jcat-sign firmware.jcat firmware.bin rhughes_signed.pem rhughes.key pkcs7 sha256
    $ fwupdtool jcat-info firmware.jcat
    FwupdJcatFile:
      Version:               0.1
      FwupdJcatItem:
        ID:                  firmware.bin
        FwupdJcatBlob:
          Kind:              sha256
          Flags:             is-utf8
          Timestamp:         2023-12-15T16:38:11Z
          Size:              0x40
          Data:              a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447
        FwupdJcatBlob:
          Kind:              pkcs7
          Target:            sha256
          Flags:             is-utf8
          Timestamp:         2023-12-15T16:38:15Z
          Size:              0xdcc
          Data:              -----BEGIN PKCS7-----
                             MIIKCwYJKoZIhvcNAQcCoIIJ/DCCCfgCAQExDTALBglghkgBZQMEAgEwCwYJKoZI
                             ...
                             Zjb6fuKL5Rr/ouoImn+x1cYJyqRMmCxpLG9GrXR9Ag==
                             -----END PKCS7-----

    $ fwupdtool jcat-verify firmware.jcat --public-keys ACME-CA.pem
    firmware.bin:
        PASSED pkcs7: O=ACME Corp.,CN=ACME CA

NOTE: Only JCat v2.0.0 and newer supports the *checksum of the payload* functionality, and you
should also add signatures **without** using target if you need to support older versions.
Additionally, older JCat versions deduplicate the blobs by just the blob kind, so you want to make
sure that the signature added with target is added **before** the signature added without.

## Testing

Download a firmware from the LVFS, and decompress with `gcab -x` -- we can now
validate the signatures are valid:

    certtool --p7-verify --infile=firmware.bin.p7b --load-ca-certificate=/etc/pki/fwupd/LVFS-CA.pem --load-data=firmware.bin

Lets create a Jcat file with a single checksum:

    $ fwupdtool jcat-self-sign test.jcat firmware.bin sha256
    $ fwupdtool jcat-info test.jcat
    FwupdJcatFile:
      Version:               0.1
      FwupdJcatItem:
        ID:                  firmware.bin
        FwupdJcatBlob:
          Kind:              sha256
          Flags:             is-utf8
          Timestamp:         2020-03-04T13:59:57Z
          Size:              0x40
          Data:              bd598c9019baee65373da1963fbce7478d6e9e8963bd837d12896f53b03be83e

Now we can import both existing signatures into a Jcat file, and then validate
it again.

    $ fwupdtool jcat-import test.jcat firmware.bin firmware.bin.asc
    $ fwupdtool jcat-import test.jcat firmware.bin firmware.bin.p7b
    $ fwupdtool jcat-info test.jcat
    FwupdJcatFile:
      Version:               0.1
      FwupdJcatItem:
        ID:                  firmware.bin
        FwupdJcatBlob:
          Kind:              sha256
          Flags:             is-utf8
          Timestamp:         2020-03-04T13:59:57Z
          Size:              0x40
          Data:              bd598c9019baee65373da1963fbce7478d6e9e8963bd837d12896f53b03be83e
        FwupdJcatBlob:
          Kind:              gpg
          Flags:             is-utf8
          Timestamp:         2020-03-04T14:00:30Z
          Size:              0x1ea
          Data:              -----BEGIN PGP SIGNATURE-----
                             Version: GnuPG v2.0.22 (GNU/Linux)

                             iQEcBAABAgAGBQJeVoylAAoJEEim2A5FOLrCagQIAIb6uDCzwUBBoZRqRzekxf0E
    ...
                             =0GGy
                             -----END PGP SIGNATURE-----

        FwupdJcatBlob:
          Kind:              pkcs7
          Flags:             is-utf8
          Timestamp:         2020-03-04T14:00:34Z
          Size:              0x8c0
          Data:              -----BEGIN PKCS7-----
                             MIIGUgYJKoZIhvcNAQcCoIIGQzCCBj8CAQExDTALBglghkgBZQMEAgEwCwYJKoZI
    ...
                             EYOqoEV8PaVQZW3ndWEaQfyo6MgZ/WqpO6Gv2zTx1CXk0APIGG8=
                             -----END PKCS7-----

    $ fwupdtool jcat-verify test.jcat --public-keys /etc/pki/fwupd
    firmware.bin:
        PASSED sha256: OK
        PASSED gpg: 3FC6B804410ED0840D8F2F9748A6D80E4538BAC2
        PASSED pkcs7: O=Linux Vendor Firmware Project,CN=LVFS CA

## Security

Unlike Microsoft catalog files which are a signed manifest of hashes, a Jcat file is a manifest of
signatures.
This means it's possible (and positively encouraged) to modify the `.jcat` file to add new
signatures or replace existing ones.

This means Jcat does not verify that the jcat file itself has not been modified, only that the
individual files and signatures themselves have not been changed.

If you require some trust in that file A was signed at the same time, or by the same person as file
B then the best way to do this is to embed a checksum (e.g. SHA-256) into one file and then verify
it in the client software.

For instance, when installing firmware we need to know if a metadata file was provided by the LVFS
with the vendor firmware file. To do this, we add the SHA-256 checksum of the `firmware.bin` in the
`firmware.metainfo.xml` file itself, and then add both files to a Jcat archive.
The client software (e.g. fwupd) then needs to check the firmware checksum as an additional step of
verifying the signatures in the Jcat file.

## Back to Firmware

If the intent is to use a test key to sign the firmware files and get installed purely offline with
an unmodified fwupd package (without uploading to the LVFS) then the following instructions can be
modified to suit.

First, lets verify that an existing firmware binary and metainfo file without a Jcat signature
refuses to install when packaged into a cabinet archive:

    $ fwupdtool build-cabinet firmware.cab firmware.bin firmware.metainfo.xml
    $ fwupdmgr install firmware.cab --allow-reinstall
    Decompressing…           [ -                                     ]
    firmware signature missing or not trusted; set OnlyTrusted=false in /etc/fwupd/fwupd.conf ONLY if you are a firmware developer

Let's use the test certificates we generated above.

Please do not use the unmodified `ACME-CA.pem` or `rhughes_signed.pem` files for signing any cabinet
archives you're going to redistribute anywhere (even internally), otherwise it is going to be very
confusing to debug *which* `rhughes_signed.pem` is being used.

Lets now use the signed user key to create a Jcat file and also add a SHA256 checksum:

    fwupdtool jcat-sign firmware.jcat firmware.bin rhughes_signed.pem rhughes.key
    fwupdtool jcat-self-sign firmware.jcat firmware.bin sha256

We can then create the new firmware archive, this time with the self-signed Jcat file as well.

    fwupdtool build-cabinet firmware.cab firmware.bin firmware.metainfo.xml firmware.jcat

Now we need to install the **CA** certificate to the system-wide system store.
If fwupd is running in a prefix then you need to use that instead, e.g. `/home/emily/root/etc/pki/fwupd/`.

    $ sudo cp ACME-CA.pem /etc/pki/fwupd/
    [sudo] password for emily: foobarbaz

Then, the firmware should install **without** needing to change `OnlyTrusted` in `fwupd.conf`.

    $ fwupdmgr install firmware.cab --allow-reinstall
    Writing…                 [***************************************]
    Successfully installed firmware

Vendors are allowed to sign the Jcat with their own user certificate if desired, although please
note that maintaining a certificate authority is a serious business including HSMs, time-limited
and *revokable* user-certificates -- and typically lots of legal paperwork.

Shipping the custom vendor CA certificate in the fwupd project is **not possible**, or a good idea,
secure or practical -- or how fwupd and LVFS were designed to be used. So please do not ask.

That said, if a vendor included the `.jcat` in the firmware cabinet archive, the LVFS will
**append** its own signature rather than replace it -- which may make testing the archive easier.

## Debugging

Using `sudo fwupdtool get-details firmware.cab --verbose --verbose` should indicate why the
certificate isn't being trusted, e.g.

    FuCabinet            processing file: firmware.metainfo.xml
    FuCabinet            processing release: 1.2.3
    FuCabinet            failed to verify payload firmware.bin: checksums were required, but none supplied

This indicates that the `fwupdtool jcat-self-sign firmware.jcat firmware.bin sha256` step was
missed as the JCat file does not have any supported checksums.
