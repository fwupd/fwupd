#!/bin/bash
set -e
set -x

# get the correct branch to use
if [ -z "$FWUPD_BRANCH" ]; then
    if [ -n $TRAVIS_BRANCH ]; then
        export FWUPD_BRANCH=$TRAVIS_BRANCH
    fi
    if [ -n $TRAVIS_PULL_REQUEST_BRANCH ]; then
        export FWUPD_BRANCH=$TRAVIS_PULL_REQUEST_BRANCH
    fi
fi
if [ -z "$FWUPD_BRANCH" ]; then
    GIT_BRANCH=`git rev-parse --abbrev-ref HEAD`
    if [ $GIT_BRANCH != "HEAD" ]; then
        export FWUPD_BRANCH=$GIT_BRANCH
    fi
fi
if [ -z "$FWUPD_BRANCH" ]; then
    exit 0
fi
if [ -z "$FWUPD_ROOT" ]; then
	export FWUPD_ROOT="file://$(pwd)"
fi

# install the runtimes
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install flathub runtime/org.gnome.Sdk/x86_64/3.28
flatpak install flathub runtime/org.gnome.Platform/x86_64/3.28

# copy in the branch
rm -rf build
mkdir build
envsubst < contrib/org.freedesktop.fwupd.json > build/org.freedesktop.fwupd.json

# build the repo
flatpak-builder --repo=repo --force-clean --disable-rofiles-fuse build-dir build/org.freedesktop.fwupd.json

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
