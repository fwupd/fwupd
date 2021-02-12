if build_daemon
  install_data(['org.freedesktop.fwupd.xml'],
    install_dir : join_paths(datadir, 'dbus-1', 'interfaces')
  )
endif

client_src = []
systemd_src = []
daemon_dep = [
  libjcat,
  libxmlb,
  libgcab,
  giounix,
  gmodule,
  gudev,
  sqlite,
  libjsonglib,
]

client_dep = [
  gudev,
]

if get_option('gusb')
  daemon_dep += gusb
  client_dep += gusb
endif

if get_option('libarchive')
  daemon_dep += libarchive
endif

if get_option('systemd')
  systemd_src += 'fu-systemd.c'
endif
if build_daemon and get_option('polkit')
  client_src += 'fu-polkit-agent.c'
  daemon_dep += polkit
endif

daemon_src = [
  'fu-config.c',
  'fu-debug.c',
  'fu-device-list.c',
  'fu-engine.c',
  'fu-engine-helper.c',
  'fu-engine-request.c',
  'fu-history.c',
  'fu-idle.c',
  'fu-install-task.c',
  'fu-keyring-utils.c',
  'fu-plugin-list.c',
  'fu-backend.c',
  'fu-remote-list.c',
  'fu-security-attr.c',
] + systemd_src

if get_option('gudev')
  daemon_src += 'fu-udev-backend.c'
endif
if get_option('gusb')
  daemon_src += 'fu-usb-backend.c'
endif
if get_option('bluez')
  daemon_src += 'fu-bluez-backend.c'
endif

if build_daemon
fwupdmgr = executable(
  'fwupdmgr',
  sources : [
    'fu-util.c',
    'fu-history.c',
    'fu-progressbar.c',
    'fu-security-attr.c',
    'fu-util-common.c',
    client_src,
    systemd_src
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    libfwupd_deps,
    libxmlb,
    client_dep,
    sqlite,
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
    'fu-security-attr.c',
    'fu-util-common.c',
    systemd_src,
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    libfwupd_deps,
    libxmlb,
    client_dep,
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
    'fu-security-attr.c',
    'fu-util-common.c',
    systemd_src
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    libfwupd_deps,
    client_dep,
    libxmlb,
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
    'fu-progressbar.c',
    'fu-util-common.c',
    daemon_src,
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    libfwupd_deps,
    libxmlb,
    libgcab,
    client_dep,
    sqlite,
    valgrind,
  ],
  link_with : [
    fwupd,
    fwupdplugin
  ],
  install : true,
  install_dir : bindir
)

if get_option('man')
  help2man = find_program('help2man')
  if build_daemon
    custom_target('fwupdmgr-man',
      input : fwupdmgr,
      output : 'fwupdmgr.1',
      command : [
        help2man, '@INPUT@',
        '--no-info',
        '--output', '@OUTPUT@',
        '--name', 'Firmware update manager client utility',
        '--manual', 'User Commands',
        '--include', join_paths(meson.current_source_dir(), 'scripting.txt'),
        '--version-string', fwupd_version,
      ],
      install : true,
      install_dir : join_paths(mandir, 'man1'),
    )
  endif
  if get_option('agent')
    custom_target('fwupdagent-man',
      input : fwupdagent,
      output : 'fwupdagent.1',
      command : [
        help2man, '@INPUT@',
        '--no-info',
        '--output', '@OUTPUT@',
        '--name', 'Firmware updating agent',
        '--manual', 'User Commands',
        '--include', join_paths(meson.current_source_dir(), 'scripting.txt'),
        '--version-string', fwupd_version,
      ],
      install : true,
      install_dir : join_paths(mandir, 'man1'),
    )
  endif
  if build_standalone
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
endif

if build_daemon
executable(
  'fwupd',
  resources_src,
  fu_hash,
  sources : [
    'fu-main.c',
    daemon_src,
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    valgrind,
    libsystemd,
    daemon_dep,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
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
      'fu-progressbar.c',
      'fu-self-test.c',
      daemon_src,
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      daemon_dep,
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

  # for fuzzing
  fwupd_firmware_dump = executable(
    'fwupd-firmware-dump',
    sources : [
      'fu-firmware-dump.c',
      daemon_src,
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      daemon_dep,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
  )
endif

subdir('fuzzing')
