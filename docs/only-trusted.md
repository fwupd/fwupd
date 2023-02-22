---
title: Signing Test Firmware Payloads
---

## Introduction

In the normal vendor update flow, firmware is optionally signed and encrypted by the vendor, and
then uploaded to the LVFS wrapped in a cabinet archive with a small XML metadata file to describe
how the firmware should be matched to hardware.

Upon uploading, the LVFS signs the firmware and metadata XML contained in the archive and adds it to
a `.jcat` file also included in the image.
The original firmware is never modified, which is why the [Jcat](https://github.com/hughsie/libjcat)
file exists as both a detached checksum (SHA-1, SHA-256 and SHA-512) and a detached signature.
The LVFS can add either GPG or PKCS#7 signatures in the Jcat file, and currently does *both* for
maxmum compatibility with how client systems have been configured.

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

If the intent is to use a test key to sign the firmware files and get installed purely offline with
an unmodified fwupd package (without uploading to the LVFS) then the following instructions can be
modified to suit.

First, lets verify that an existing firmware binary and metainfo file without a Jcat signature
refuses to install when packaged into a cabinet archive:

    $ gcab -c firmware.cab firmware.bin firmware.metainfo.xml 
    $ fwupdmgr install firmware.cab --allow-reinstall
    Decompressing…           [ -                                     ]
    firmware signature missing or not trusted; set OnlyTrusted=false in /etc/fwupd/daemon.conf ONLY if you are a firmware developer

Let's download a script that can generate some test certificates -- feel free to copy the commands
used and of course you need to modify the details of both the CA and user certificate.

Please do not use the unmodified `ACME-CA.pem` or `rhughes_signed.pem` files for signing any cabinet
archives you're going to redistribute anywhere (even internally), otherwise it is going to be very
confusing to debug *which* `rhughes_signed.pem` is being used.

    $ wget https://raw.githubusercontent.com/hughsie/libjcat/main/contrib/build-certs.py
    $ python ./build-certs.py 
    Signing certificate...
    $ ls ACME* rhughes*
    ACME-CA.key  ACME-CA.pem  rhughes.csr  rhughes.key  rhughes.pem  rhughes_signed.pem

We now have a CA key from ACME, and a user key signed by the CA key, along with a CSR and the two
private keys.

Lets now use the signed user key to create a Jcat file and also add a SHA256 checksum:

    $ jcat-tool --appstream-id com.redhat.rhughes sign firmware.jcat firmware.bin rhughes_signed.pem rhughes.key
    $ jcat-tool self-sign firmware.jcat firmware.bin --kind sha256
    $ jcat-tool info firmware.jcat 
    JcatFile:
      Version:               0.1
      JcatItem:
        ID:                  firmware.bin
        JcatBlob:
          Kind:              pkcs7
          Flags:             is-utf8
          AppstreamId:       com.redhat.rhughes
          Timestamp:         2023-02-22T10:24:25Z
          Size:              0xdcc
          Data:              -----BEGIN PKCS7-----
                             MIIKCwYJKoZIhvcNAQcCoIIJ/DCCCfgCAQExDTALBglghkgBZQMEAgEwCwYJKoZI
    ...
                             ysAcwqcDY7+k9TWB8V2MeZCHg6/aF4Oj3R16Nvag3w==
                             -----END PKCS7-----
                             
        JcatBlob:
          Kind:              sha256
          Flags:             is-utf8
          Timestamp:         2023-02-22T10:30:19Z
          Size:              0x40
          Data:              fce1847b0599bb19cd913d02268f15107691a79221ce16822b4c931cd1bda2c5

We can then create the new firmware archive, this time with the self-signed Jcat file as well.

    gcab -c firmware.cab firmware.bin firmware.metainfo.xml firmware.jcat 

Now we need to install the **CA** certificate to the system-wide system store.
If fwupd is running in a prefix then you need to use that instead, e.g. `/home/emily/root/etc/pki/fwupd/`.

    $ sudo cp ACME-CA.pem /etc/pki/fwupd/
    [sudo] password for emily: foobarbaz

Then, the firmware should install **without** needing to change `OnlyTrusted` in `daemon.conf`.

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
