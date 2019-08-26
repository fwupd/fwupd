if get_option('daemon')
  install_data(['org.freedesktop.fwupd.xml'],
    install_dir : join_paths(datadir, 'dbus-1', 'interfaces')
  )
endif

keyring_deps = []
keyring_src = []
test_deps = []
libfwupdprivate_src = [
  'fu-archive.c',
  'fu-chunk.c',
  'fu-common.c',
  'fu-common-cab.c',
  'fu-common-guid.c',
  'fu-common-version.c',
  'fu-device.c',
  'fu-device-locker.c',
  'fu-firmware.c',
  'fu-firmware-common.c',
  'fu-firmware-image.c',
  'fu-hwids.c',
  'fu-history.c',
  'fu-ihex-firmware.c',
  'fu-io-channel.c',
  'fu-plugin.c',
  'fu-progressbar.c',
  'fu-quirks.c',
  'fu-smbios.c',
  'fu-srec-firmware.c',
  'fu-test.c',
  'fu-udev-device.c',
  'fu-usb-device.c',
]

if get_option('gpg')
  keyring_src += 'fu-keyring-gpg.c'
  keyring_deps += gpgme
  keyring_deps += gpgerror
endif

if get_option('pkcs7')
  keyring_src += 'fu-keyring-pkcs7.c'
  keyring_deps += gnutls
  if get_option('tests')
    test_deps += colorhug_pkcs7_signature
  endif
endif

if get_option('systemd')
  libfwupdprivate_src += 'fu-systemd.c'
endif

libfwupdprivate = static_library(
  'fwupdprivate',
  sources : [
    libfwupdprivate_src,
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    giounix,
    gudev,
    gusb,
    libarchive,
    libgcab,
    libjsonglib,
    libxmlb,
    soup,
    sqlite,
    valgrind,
  ],
  link_with : [
    fwupd,
  ],
  c_args : [
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
    libxmlb,
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
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : bindir
)
endif

if get_option('agent')
fwupdagent = executable(
  'fwupdagent',
  sources : [
    'fu-agent.c',
    'fu-util-common.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    libxmlb,
    giounix,
    gudev,
    gusb,
    soup,
    libjsonglib,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  install : true,
  install_dir : join_paths(libexecdir, 'fwupd')
)
endif

if get_option('systemd')
fwupdagent = executable(
  'fwupdoffline',
  sources : [
    'fu-offline.c',
    'fu-util-common.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    giounix,
    gudev,
    gusb,
    libxmlb,
    soup,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : [
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : join_paths(libexecdir, 'fwupd')
)
endif

resources_src = gnome.compile_resources(
  'fwupd-resources',
  'fwupd.gresource.xml',
  source_dir : '.',
  c_name : 'fu'
)

fu_hash = custom_target(
  'fu-hash.h',
  input : libfwupdprivate_src,
  output : 'fu-hash.h',
  command : [python3.path(),
             join_paths(meson.current_source_dir(), 'fu-hash.py'),
             '@OUTPUT@', '@INPUT@']
)

fwupdtool = executable(
  'fwupdtool',
  resources_src,
  fu_hash,
  sources : [
    'fu-tool.c',
    keyring_src,
    libfwupdprivate_src,
    'fu-config.c',
    'fu-debug.c',
    'fu-device-list.c',
    'fu-engine.c',
    'fu-idle.c',
    'fu-install-task.c',
    'fu-keyring.c',
    'fu-keyring-result.c',
    'fu-keyring-utils.c',
    'fu-plugin-list.c',
    'fu-util-common.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    keyring_deps,
    libxmlb,
    libgcab,
    giounix,
    gmodule,
    gudev,
    gusb,
    soup,
    sqlite,
    valgrind,
    libarchive,
    libjsonglib,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : [
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
  fu_hash,
  sources : [
    keyring_src,
    libfwupdprivate_src,
    'fu-config.c',
    'fu-debug.c',
    'fu-device-list.c',
    'fu-engine.c',
    'fu-idle.c',
    'fu-install-task.c',
    'fu-keyring.c',
    'fu-keyring-result.c',
    'fu-keyring-utils.c',
    'fu-main.c',
    'fu-plugin-list.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    keyring_deps,
    libxmlb,
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
    libjsonglib,
  ],
  link_with : fwupd,
  c_args : [
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
    fu_hash,
    sources : [
      keyring_src,
      libfwupdprivate_src,
      'fu-config.c',
      'fu-device-list.c',
      'fu-engine.c',
      'fu-idle.c',
      'fu-install-task.c',
      'fu-keyring.c',
      'fu-keyring-result.c',
      'fu-keyring-utils.c',
      'fu-plugin-list.c',
      'fu-self-test.c',
      'fu-smbios.c',
      'fu-test.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../libfwupd'),
    ],
    dependencies : [
      keyring_deps,
      libxmlb,
      libgcab,
      giounix,
      gmodule,
      gudev,
      gusb,
      soup,
      sqlite,
      valgrind,
      libarchive,
      libjsonglib,
    ],
    link_with : [
      fwupd,
    ],
    c_args : [
      '-DTESTDATADIR_SRC="' + testdatadir_src + '"',
      '-DTESTDATADIR_DST="' + testdatadir_dst + '"',
      '-DTESTDATADIR="' + testdatadir_src + ':' + testdatadir_dst + '"',
      '-DPLUGINBUILDDIR="' + pluginbuilddir + '"',
      '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"',
    ],
  )
  test('fu-self-test', e, is_parallel:false, timeout:180)
endif

if get_option('introspection')
  gir_dep = declare_dependency(sources: gir)
  gnome.generate_gir(fwupd,
    sources : [
      'fu-archive.c',
      'fu-archive.h',
      'fu-chunk.c',
      'fu-chunk.h',
      'fu-common.c',
      'fu-common-guid.c',
      'fu-common-guid.h',
      'fu-common-version.c',
      'fu-common-version.h',
      'fu-common.h',
      'fu-device.c',
      'fu-device.h',
      'fu-device-locker.c',
      'fu-device-locker.h',
      'fu-firmware.c',
      'fu-firmware.h',
      'fu-firmware-common.c',
      'fu-firmware-common.h',
      'fu-firmware-image.c',
      'fu-firmware-image.h',
      'fu-io-channel.c',
      'fu-plugin.c',
      'fu-plugin.h',
      'fu-quirks.c',
      'fu-quirks.h',
      'fu-udev-device.c',
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
      libxmlb,
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
