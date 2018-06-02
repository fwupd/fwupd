#!/bin/bash
set -e
set -x

# install the runtimes
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install flathub runtime/org.gnome.Sdk/x86_64/3.28
flatpak install flathub runtime/org.gnome.Platform/x86_64/3.28

# build the repo
flatpak-builder --repo=repo --force-clean --disable-rofiles-fuse build-dir contrib/org.freedesktop.fwupd.json

# show the files that were included
tree build-dir

# build a single file bundle
flatpak build-bundle repo fwupd.flatpak org.freedesktop.fwupd

# make available as a deliverable
cp fwupd.flatpak dist

# to run from the builddir:
# sudo flatpak-builder --run build-dir org.freedesktop.fwupd.json /app/libexec/fwupd/fwupdtool get-devices

# install the single file bundle
# flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
# flatpak install fwupd.flatpak

# to run a shell in the same environment that flatpak sees:
# flatpak run --command=sh --devel org.freedesktop.fwupd

# to run fwupdtool as root:
# sudo flatpak run org.freedesktop.fwupd --verbose get-devices
