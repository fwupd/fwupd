if build_daemon
  install_data(['org.freedesktop.fwupd.xml'],
    install_dir : join_paths(datadir, 'dbus-1', 'interfaces')
  )
endif

keyring_deps = []
keyring_src = []
systemd_src = []

if get_option('gpg')
  keyring_src += 'fu-keyring-gpg.c'
  keyring_deps += gpgme
  keyring_deps += gpgerror
endif

if get_option('pkcs7')
  keyring_src += 'fu-keyring-pkcs7.c'
  keyring_deps += gnutls
endif

if get_option('systemd')
  systemd_src += 'fu-systemd.c'
endif

if build_daemon
fwupdmgr = executable(
  'fwupdmgr',
  sources : [
    'fu-util.c',
    'fu-history.c',
    'fu-progressbar.c',
    'fu-util-common.c',
    systemd_src
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
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
    fwupdplugin,
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
    systemd_src,
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
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
    fwupdplugin,
  ],
  install : true,
  install_dir : bindir
)
endif

if get_option('systemd')
fwupdoffline = executable(
  'fwupdoffline',
  sources : [
    'fu-history.c',
    'fu-offline.c',
    'fu-util-common.c',
    systemd_src
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    giounix,
    gudev,
    gusb,
    libxmlb,
    soup,
    sqlite,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
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

fwupdtool = executable(
  'fwupdtool',
  resources_src,
  fu_hash,
  export_dynamic : true,
  sources : [
    'fu-tool.c',
    keyring_src,
    'fu-config.c',
    'fu-debug.c',
    'fu-device-list.c',
    'fu-engine.c',
    'fu-engine-helper.c',
    'fu-history.c',
    'fu-idle.c',
    'fu-install-task.c',
    'fu-keyring.c',
    'fu-keyring-result.c',
    'fu-keyring-utils.c',
    'fu-plugin-list.c',
    'fu-progressbar.c',
    'fu-remote-list.c',
    'fu-util-common.c',
    systemd_src
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
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
    fwupdplugin
  ],
  install : true,
  install_dir : bindir
)

if build_daemon and get_option('man')
  help2man = find_program('help2man')
  custom_target('fwupdmgr-man',
    input : fwupdmgr,
    output : 'fwupdmgr.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'Firmware update manager client utility',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
    ],
    install : true,
    install_dir : join_paths(mandir, 'man1'),
  )
  custom_target('fwupdagent-man',
    input : fwupdagent,
    output : 'fwupdagent.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'Firmware updating agent',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
    ],
    install : true,
    install_dir : join_paths(mandir, 'man1'),
  )
endif
if get_option('man')
  custom_target('fwupdtool-man',
    input : fwupdtool,
    output : 'fwupdtool.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'Standalone firmware update utility',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
    ],
    install : true,
    install_dir : join_paths(mandir, 'man1'),
  )
endif

if build_daemon
executable(
  'fwupd',
  resources_src,
  fu_hash,
  sources : [
    keyring_src,
    'fu-config.c',
    'fu-debug.c',
    'fu-device-list.c',
    'fu-engine.c',
    'fu-engine-helper.c',
    'fu-history.c',
    'fu-idle.c',
    'fu-install-task.c',
    'fu-keyring.c',
    'fu-keyring-result.c',
    'fu-keyring-utils.c',
    'fu-main.c',
    'fu-plugin-list.c',
    'fu-remote-list.c',
    systemd_src
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
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
  link_with : [
    fwupd,
    fwupdplugin
  ],
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
    test_deps,
    fu_hash,
    sources : [
      keyring_src,
      'fu-config.c',
      'fu-device-list.c',
      'fu-engine.c',
      'fu-engine-helper.c',
      'fu-history.c',
      'fu-idle.c',
      'fu-install-task.c',
      'fu-keyring.c',
      'fu-keyring-result.c',
      'fu-keyring-utils.c',
      'fu-plugin-list.c',
      'fu-progressbar.c',
      'fu-remote-list.c',
      'fu-self-test.c',
      systemd_src
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
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
      fwupdplugin
    ],
    c_args : [
      '-DTESTDATADIR_SRC="' + testdatadir_src + '"',
      '-DTESTDATADIR_DST="' + testdatadir_dst + '"',
      '-DPLUGINBUILDDIR="' + pluginbuilddir + '"',
    ],
  )
  test('fu-self-test', e, is_parallel:false, timeout:180)
endif

if get_option('tests')
  # for fuzzing
  fwupd_firmware_dump = executable(
    'fwupd-firmware-dump',
    sources : [
      'fu-firmware-dump.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      gio,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
endif

if get_option('tests')
  subdir('fuzzing')
endif
