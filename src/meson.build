cargs = ['-DG_LOG_DOMAIN="Fu"']

if get_option('daemon')
  install_data(['org.freedesktop.fwupd.xml'],
    install_dir : join_paths(datadir, 'dbus-1', 'interfaces')
  )
endif

keyring_deps = []
keyring_src = []
test_deps = []

if get_option('gpg')
  keyring_src += 'fu-keyring-gpg.c'
  keyring_deps += gpgme
  keyring_deps += gpgerror
endif

if get_option('pkcs7')
  keyring_src += 'fu-keyring-pkcs7.c'
  keyring_deps += gnutls
  test_deps += colorhug_pkcs7_signature
endif

libfwupdprivate = static_library(
  'fwupdprivate',
  sources : [
    'fu-common.c',
    'fu-device.c',
    'fu-device-locker.c',
    'fu-hwids.c',
    'fu-history.c',
    'fu-plugin.c',
    'fu-progressbar.c',
    'fu-quirks.c',
    'fu-smbios.c',
    'fu-test.c',
    'fu-usb-device.c',
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
    soup,
    sqlite,
    libarchive,
    valgrind,
  ],
  c_args : [
    cargs,
    '-DSYSFSFIRMWAREDIR="/sys/firmware"',
    '-DFU_OFFLINE_DESTDIR=""',
  ],
)

if get_option('daemon')
fwupdmgr = executable(
  'fwupdmgr',
  sources : [
    'fu-util.c',
    'fu-util-common.c',
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
    soup,
    sqlite,
    libarchive,
    libjsonglib,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : [
    cargs,
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : bindir
)
endif

resources_src = gnome.compile_resources(
  'fwupd-resources',
  'fwupd.gresource.xml',
  source_dir : '.',
  c_name : 'fu'
)

fwupdtool = executable(
  'fwupdtool',
  resources_src,
  sources : [
    'fu-tool.c',
    keyring_src,
    'fu-common.c',
    'fu-common-cab.c',
    'fu-config.c',
    'fu-keyring.c',
    'fu-keyring-result.c',
    'fu-engine.c',
    'fu-hwids.c',
    'fu-debug.c',
    'fu-device.c',
    'fu-device-list.c',
    'fu-device-locker.c',
    'fu-install-task.c',
    'fu-keyring.c',
    'fu-keyring-utils.c',
    'fu-history.c',
    'fu-plugin.c',
    'fu-plugin-list.c',
    'fu-quirks.c',
    'fu-smbios.c',
    'fu-usb-device.c',
    'fu-util-common.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    keyring_deps,
    appstream_glib,
    libgcab,
    giounix,
    gmodule,
    gudev,
    gusb,
    soup,
    sqlite,
    valgrind,
    libarchive,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : [
    cargs,
    '-DSYSFSFIRMWAREDIR="/sys/firmware"',
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : join_paths(libexecdir, 'fwupd')
)

if get_option('daemon') and get_option('man')
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
    install_dir : join_paths(mandir, 'man1'),
  )
endif

if get_option('daemon')
executable(
  'fwupd',
  resources_src,
  sources : [
    keyring_src,
    'fu-common.c',
    'fu-common-cab.c',
    'fu-config.c',
    'fu-keyring.c',
    'fu-keyring-result.c',
    'fu-engine.c',
    'fu-main.c',
    'fu-hwids.c',
    'fu-debug.c',
    'fu-device.c',
    'fu-device-list.c',
    'fu-device-locker.c',
    'fu-install-task.c',
    'fu-keyring.c',
    'fu-keyring-utils.c',
    'fu-history.c',
    'fu-plugin.c',
    'fu-plugin-list.c',
    'fu-quirks.c',
    'fu-smbios.c',
    'fu-usb-device.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    keyring_deps,
    appstream_glib,
    libgcab,
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
    '-DSYSFSFIRMWAREDIR="/sys/firmware"',
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : join_paths(libexecdir, 'fwupd')
)
endif

if get_option('tests')
  testdatadir_src = join_paths(meson.source_root(), 'data', 'tests')
  testdatadir_dst = join_paths(meson.build_root(), 'data', 'tests')
  pluginbuilddir = join_paths(meson.build_root(), 'plugins', 'test')
  e = executable(
    'fu-self-test',
    resources_src,
    colorhug_test_firmware,
    builder_test_firmware,
    hwid_test_firmware,
    noreqs_test_firmware,
    test_deps,
    sources : [
      keyring_src,
      'fu-self-test.c',
      'fu-common.c',
      'fu-common-cab.c',
      'fu-config.c',
      'fu-engine.c',
      'fu-keyring.c',
      'fu-keyring-utils.c',
      'fu-hwids.c',
      'fu-device.c',
      'fu-device-list.c',
      'fu-device-locker.c',
      'fu-history.c',
      'fu-install-task.c',
      'fu-keyring.c',
      'fu-keyring-result.c',
      'fu-plugin.c',
      'fu-plugin-list.c',
      'fu-progressbar.c',
      'fu-quirks.c',
      'fu-smbios.c',
      'fu-test.c',
      'fu-usb-device.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../libfwupd'),
    ],
    dependencies : [
      keyring_deps,
      appstream_glib,
      libgcab,
      giounix,
      gmodule,
      gudev,
      gusb,
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
      '-DPLUGINBUILDDIR="' + pluginbuilddir + '"',
      '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"',
      '-DSYSFSFIRMWAREDIR="' + testdatadir_src + '"',
    ],
  )
  test('fu-self-test', e, is_parallel:false,
       env: ['FWUPD_DATADIR=' + testdatadir_src,
             'FWUPD_SYSCONFDIR=' + testdatadir_src,
             'FWUPD_PLUGINDIR=' + testdatadir_src,
             'FWUPD_LOCALSTATEDIR=/tmp/fwupd-self-test/var'])
endif

if get_option('introspection')
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
      'fu-quirks.c',
      'fu-quirks.h',
      'fu-usb-device.c',
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
