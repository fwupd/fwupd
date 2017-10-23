cargs = ['-DG_LOG_DOMAIN="Fu"']

install_data(['org.freedesktop.fwupd.xml'],
  install_dir : join_paths(get_option('datadir'), 'dbus-1', 'interfaces')
)

keyring_deps = []
keyring_src = []

if get_option('enable-gpg')
  keyring_src += 'fu-keyring-gpg.c'
  keyring_deps += gpgme
  keyring_deps += gpgerror
endif

if get_option('enable-pkcs7')
  keyring_src += 'fu-keyring-pkcs7.c'
  keyring_deps += gnutls
endif

libfwupdprivate = static_library(
  'fwupdprivate',
  sources : [
    'fu-common.c',
    'fu-device.c',
    'fu-device-locker.c',
    'fu-hwids.c',
    'fu-pending.c',
    'fu-plugin.c',
    'fu-progressbar.c',
    'fu-smbios.c',
    'fu-test.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    appstream_glib,
    giounix,
    gudev,
    gusb,
    polkit,
    soup,
    sqlite,
    libarchive,
    valgrind,
  ],
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
    '-DSYSFSFIRMWAREDIR="/sys/firmware"',
    '-DFU_OFFLINE_DESTDIR=""',
  ],
)

fwupdmgr = executable(
  'fwupdmgr',
  sources : [
    'fu-util.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    appstream_glib,
    giounix,
    gudev,
    gusb,
    polkit,
    soup,
    sqlite,
    libarchive,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : get_option('bindir')
)

if get_option('enable-man')
  help2man = find_program('help2man')
  custom_target('fwupdmgr-man',
    input : fwupdmgr,
    output : 'fwupdmgr.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'fwupd',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1'),
  )
endif

resources_src = gnome.compile_resources(
  'fwupd-resources',
  'fwupd.gresource.xml',
  source_dir : '.',
  c_name : 'fu'
)

executable(
  'fwupd',
  resources_src,
  sources : [
    keyring_src,
    'fu-common.c',
    'fu-config.c',
    'fu-keyring.c',
    'fu-keyring-result.c',
    'fu-engine.c',
    'fu-main.c',
    'fu-hwids.c',
    'fu-debug.c',
    'fu-device.c',
    'fu-device-locker.c',
    'fu-keyring.c',
    'fu-pending.c',
    'fu-plugin.c',
    'fu-smbios.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    keyring_deps,
    appstream_glib,
    giounix,
    gmodule,
    gudev,
    gusb,
    polkit,
    soup,
    sqlite,
    valgrind,
    libarchive,
  ],
  link_with : fwupd,
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
    '-DPLUGINDIR="' + plugin_dir + '"',
    '-DSYSFSFIRMWAREDIR="/sys/firmware"',
    '-DSYSCONFDIR="' + default_sysconfdir + '"',
    '-DFWUPDCONFIGDIR="' + join_paths(default_sysconfdir, 'fwupd') + '"',
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : join_paths(get_option('libexecdir'), 'fwupd')
)

if get_option('enable-tests')
  testdatadir_src = join_paths(meson.source_root(), 'data', 'tests')
  testdatadir_dst = join_paths(meson.build_root(), 'data', 'tests')
  pluginbuilddir = join_paths(meson.build_root(), 'plugins', 'test')
  e = executable(
    'fu-self-test',
    resources_src,
    colorhug_test_firmware,
    colorhug_pkcs7_signature,
    builder_test_firmware,
    sources : [
      keyring_src,
      'fu-self-test.c',
      'fu-common.c',
      'fu-config.c',
      'fu-engine.c',
      'fu-keyring.c',
      'fu-hwids.c',
      'fu-device.c',
      'fu-device-locker.c',
      'fu-pending.c',
      'fu-keyring.c',
      'fu-keyring-result.c',
      'fu-plugin.c',
      'fu-progressbar.c',
      'fu-smbios.c',
      'fu-test.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../libfwupd'),
    ],
    dependencies : [
      keyring_deps,
      appstream_glib,
      giounix,
      gmodule,
      gudev,
      gusb,
      polkit,
      soup,
      sqlite,
      valgrind,
      libarchive,
    ],
    link_with : [
      fwupd,
    ],
    c_args : [
      cargs,
      '-DTESTDATADIR_SRC="' + testdatadir_src + '"',
      '-DTESTDATADIR_DST="' + testdatadir_dst + '"',
      '-DTESTDATADIR="' + testdatadir_src + ':' + testdatadir_dst + '"',
      '-DLOCALSTATEDIR="/tmp/fwupd-self-test/var"',
      '-DPLUGINBUILDDIR="' + pluginbuilddir + '"',
      '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"',
      '-DPLUGINDIR="' + testdatadir_src + '"',
      '-DSYSFSFIRMWAREDIR="' + testdatadir_src + '"',
      '-DSYSCONFDIR="' + testdatadir_src + '"',
      '-DFWUPDCONFIGDIR="' + testdatadir_src + '"',
    ],
  )
  test('fu-self-test', e, is_parallel:false)
endif

if get_option('enable-introspection')
  gir_dep = declare_dependency(sources: gir)
  gnome.generate_gir(fwupd,
    sources : [
      'fu-common.c',
      'fu-common.h',
      'fu-device.c',
      'fu-device.h',
      'fu-device-locker.c',
      'fu-device-locker.h',
      'fu-plugin.c',
      'fu-plugin.h',
    ],
    nsversion : '1.0',
    namespace : 'Fu',
    symbol_prefix : 'fu',
    identifier_prefix : 'Fu',
    export_packages : 'fu',
    include_directories : [
      include_directories('..'),
      include_directories('../libfwupd'),
    ],
    dependencies : [
      appstream_glib,
      gir_dep,
      giounix,
      gusb,
      soup,
      sqlite,
    ],
    link_with : [
      libfwupdprivate,
    ],
    includes : [
      'Gio-2.0',
      'GObject-2.0',
      'GUsb-1.0',
    ],
  )
endif
