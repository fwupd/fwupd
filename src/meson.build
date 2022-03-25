if get_option('tests')
subdir('tests')
endif

client_src = []
systemd_src = []
daemon_dep = [
  libarchive,
  libjcat,
  libxmlb,
  libgcab,
  giounix,
  gmodule,
  gudev,
  gusb,
  libjsonglib,
  polkit,
  sqlite,
  cbor,
]

client_dep = [
  gudev,
  gusb,
  sqlite,
]
if libsystemd.found()
  systemd_src += 'fu-systemd.c'
endif
if polkit.found()
  client_src += 'fu-polkit-agent.c'
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
  'fu-release.c',
  'fu-keyring-utils.c',
  'fu-plugin-list.c',
  'fu-remote-list.c',
  'fu-security-attr.c',
] + systemd_src

if gudev.found()
  daemon_src += 'fu-udev-backend.c'
endif
if gusb.found()
  daemon_src += 'fu-usb-backend.c'
endif
if bluez.allowed()
  daemon_src += 'fu-bluez-backend.c'
endif

if build_daemon
if introspection.allowed()
  install_data(['org.freedesktop.fwupd.xml'],
    install_dir : join_paths(datadir, 'dbus-1', 'interfaces')
  )
endif
fwupdmgr = executable(
  'fwupdmgr',
  fu_hash,
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
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  install : true,
  install_dir : bindir
)

# for compatibility
if get_option('compat_cli')
meson.add_install_script(
  'sh', '-c',
  'ln -fs fwupdmgr @0@@1@'.format(
      '${DESTDIR}',
      join_paths(get_option('prefix'), get_option('bindir'), 'fwupdagent')))
endif
endif

if offline.allowed()
fwupdoffline = executable(
  'fwupdoffline',
  fu_hash,
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
  if build_daemon
    configure_file(
      input : 'fwupdmgr.1',
      output : 'fwupdmgr.1',
      configuration : conf,
      install: true,
      install_dir: join_paths(mandir, 'man1'),
    )
    configure_file(
      input : 'fwupdagent.1',
      output : 'fwupdagent.1',
      configuration : conf,
      install: true,
      install_dir: join_paths(mandir, 'man1'),
    )
  endif
  if build_standalone
    configure_file(
      input : 'fwupdtool.1',
      output : 'fwupdtool.1',
      configuration : conf,
      install: true,
      install_dir: join_paths(mandir, 'man1'),
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
  test_deps += colorhug_test_firmware
  test_deps += hwid_test_firmware
  test_deps += multiple_rels_test_firmware
  test_deps += noreqs_test_firmware
  test_deps += builder_test_firmware
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
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
    ],
  )
  test('fu-self-test', e, is_parallel:false, timeout:180, env : env)
endif
