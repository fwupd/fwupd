fwupd
=====

This project aims to make updating firmware on Linux automatic, safe and reliable.

ColorHug Support
----------------

You need to install colord 1.2.9 which may be newer that your distribution
provides. Compile it from source https://github.com/hughsie/colord or grab the
RPMs here http://people.freedesktop.org/~hughsient/fedora/

If you don't want or need this functionality you can use the
`--disable-colorhug` option.

UEFI Support
------------

If you're wondering where to get `fwupdate` from, either compile it form source
(you might also need a newer `efivar`) from https://github.com/rhinstaller/fwupdate
or grab the RPMs here https://pjones.fedorapeople.org/fwupdate/

If you don't want or need this functionality you can use the `--disable-uefi`
option.
