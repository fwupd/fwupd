# Maintainer: Bruno Pagani <archange@archlinux.org>
# Maintainer: Filipe Laíns (FFY00) <lains@archlinux.org>
# Maintainer: Frederik Schwan <freswa at archlinux dot org>
# Contributor: Mirco Tischler <mt-ml at gmx dot de>

pkgbase=fwupd
pkgname=(
  fwupd
  fwupd-docs
)
pkgver=1.9.18
pkgrel=2
pkgdesc="Simple daemon to allow session software to update firmware"
arch=(x86_64)
url='https://github.com/fwupd/fwupd'
license=(LGPL-2.1-or-later)
depends=(
  bash
  bluez
  curl
  flashrom
  fwupd-efi
  gcc-libs
  glib2
  glibc
  gnutls
  hicolor-icon-theme
  json-glib
  libarchive
  libcbor
  libgudev
  libgusb
  libjcat
  libmbim
  libmm-glib
  libqmi
  libxmlb
  passim
  polkit
  protobuf-c
  python
  shared-mime-info
  sqlite
  systemd-libs
  tpm2-tss
  xz
  zlib
)
makedepends=(
  bash-completion
  gi-docgen
  gnu-efi-libs
  gobject-introspection
  libdrm
  meson
  noto-fonts
  noto-fonts-cjk
  pandoc
  python-cairo
  python-dbus
  python-gobject
  python-pillow
  vala
  valgrind
)
checkdepends=(umockdev)
source=(
  "https://github.com/fwupd/fwupd/releases/download/${pkgver}/${pkgname}-${pkgver}.tar.xz"{,.asc}
  fwupd.sysusers
  more-allow-syscall-fixes.patch::https://github.com/fwupd/fwupd/pull/7171.patch
)
sha512sums=('d5e0f3a8c741240f389628e2ed81f1e6ec21689bdcd39f692222114a2d495925a0f0720b10fbb0087362c0b734f4f0abfd8e105eff4695e41e645395e5587a50'
            'SKIP'
            '637203080b55eda74a659f58c853a9a723a2dad5da70915b2b0e036c6145a649468ebec700cc83975d9cb5378b9dced8b3a3b26bdbcc75ddc774837355e75deb'
            '2d7066874f93406f033da6ec0cff89ef15d4026892c501f22721ff6991fc338d6f7fdbdff648978f7e762b4f7375679a417f78911196060e5efeba973d700e32')
b2sums=('7dd1691e2db600085a61a545961730a14d74e3f8b3a66fd90092658e19336a8e11861202db063d64c936cac9ab12ebf7dcdd3a8dcd7ff01b05f0a860d8b4ba68'
        'SKIP'
        'e65ca7da22a20a40882cfc1fe4479643f9a38c90a4f2c3e71e6e5e3de1d6db212a0f17d600097619fe3cdb0a9b860422f8b0b9a9d45441518e51a7eb12a918bb'
        '57213d3142f0d04ce8243f0c9292fe3c139c06c3740059ff9d12fcba6964d58cf9a2b5fbcd226e117707ad101f69be80fccfc7de94d5a67e3b7d8e4c6a5b4909')
validpgpkeys=(163EB50119225DB3DF8F49EA17ACBA8DFA970E17) # Richard Hughes <richard@hughsie.com>

prepare() {
  cd ${pkgname}-${pkgver}

  # Unbreak syscall filtering check
  # https://github.com/fwupd/fwupd/pull/7171
  patch -Np1 -i ../more-allow-syscall-fixes.patch
}

build() {
  local meson_options=(
    -D docs=enabled
    -D efi_binary=false
    -D launchd=disabled
    -D supported_build=enabled
    -D systemd_unit_user=fwupd
  )

  arch-meson ${pkgname}-${pkgver} build "${meson_options[@]}"
  meson compile -C build
}

check() {
  meson test -C build --print-errorlogs
}

_pick() {
  local p="$1" f d; shift
  for f; do
    d="$srcdir/$p/${f#$pkgdir/}"
    mkdir -p "$(dirname "$d")"
    mv "$f" "$d"
    rmdir -p --ignore-fail-on-non-empty "$(dirname "$f")"
  done
}

package_fwupd() {
  depends+=(
    libarchive.so
    libcbor.so
    libcurl.so
    libgudev-1.0.so
    libjson-glib-1.0.so
    libmm-glib.so
    libpassim.so
    libprotobuf-c.so
    libqmi-glib.so
  )
  optdepends=(
    'python-dbus: Firmware packaging tools'
    'python-gobject: Firmware packaging tools'
    'udisks2: UEFI firmware upgrade support'
  )
  provides=(libfwupd.so)
  backup=(
    'etc/fwupd/fwupd.conf'
    'etc/fwupd/remotes.d/lvfs-testing.conf'
    'etc/fwupd/remotes.d/lvfs.conf'
    'etc/fwupd/remotes.d/vendor-directory.conf'
  )

  meson install -C build --destdir "${pkgdir}"
  # Add fwupd user https://bugs.archlinux.org/task/79653
  install -D -m644 fwupd.sysusers "${pkgdir}"/usr/lib/sysusers.d/fwupd.conf
  # Remove the tests
  rm -r "${pkgdir}"/usr/{lib,share}/installed-tests/
  # Conflicts with the dbxtool package
  mv "${pkgdir}"/usr/bin/{,fwupd-}dbxtool
  mv "${pkgdir}"/usr/share/man/man1/{,fwupd-}dbxtool.1
  # Remove msr module-load config as it is built-in
  rm "${pkgdir}"/usr/lib/modules-load.d/fwupd-msr.conf
  rmdir "${pkgdir}"/usr/lib/modules-load.d

  _pick docs "${pkgdir}"/usr/share/doc/{,fwupd/}{libfwupdplugin,libfwupd}
}

package_fwupd-docs() {
  pkgdesc="Simple daemon to allow session software to update firmware - documentation"
  depends=()
  mv docs/* "${pkgdir}"
}
