fwupd Release Notes
===================

Release Process
---------------

Check IFD parsing:

    ../../contrib/check-ifd-firmware.py ../../../fwupd-test-roms/

Write release entries:

    ../../contrib/generate-release.py
    # copy into ../../data/org.freedesktop.fwupd.metainfo.xml
    appstream-util appdata-to-news ../../data/org.freedesktop.fwupd.metainfo.xml > NEWS

Commit changes to git:

    # MAKE SURE THIS IS CORRECT
    export release_ver="2.1.9"
    git commit -a -m "Release fwupd ${release_ver}" --no-verify
    git tag -s -f -m "Release fwupd ${release_ver}" "${release_ver}"
    ninja dist
    git push --tags
    git push
    gpg -b -a meson-dist/fwupd-${release_ver}.tar.xz

Create release and [upload tarball](https://github.com/fwupd/fwupd/tags).

Do post release version bump in `meson.build`

Commit changes:

    git commit -a -m "trivial: post release version bump" --no-verify
    git push
