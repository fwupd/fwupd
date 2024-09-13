if get_option('tests')
subdir('tests')
endif

client_src = []
systemd_src = []
engine_dep = [
  libarchive,
  libjcat,
  libxmlb,
  giounix,
  gmodule,
  libusb,
  libjsonglib,
  polkit,
  sqlite,
  cbor,
  fwupdplugin_rs_dep,
]

if get_option('passim').allowed()
  engine_dep += passim
endif

client_dep = [
  libcurl,
  libjcat,
  libjsonglib,
  libxmlb,
  sqlite,
  fwupdplugin_rs_dep,
]
if libsystemd.found()
  systemd_src += 'fu-systemd.c'
endif
if polkit.found()
  client_src += 'fu-polkit-agent.c'
endif

fwupd_engine_src = [
  'fu-cabinet.c',
  'fu-debug.c',
  'fu-device-list.c',
  'fu-engine.c',
  'fu-engine-config.c',
  'fu-engine-helper.c',
  'fu-engine-request.c',
  'fu-history.c',
  'fu-idle.c',
  'fu-polkit-authority.c',
  'fu-release.c',
  'fu-engine-requirements.c',
  'fu-release-common.c',
  'fu-plugin-list.c',
  'fu-remote.c',
  'fu-remote-list.c',
  'fu-security-attr-common.c',
  'fu-usb-backend.c',
  'fu-client.c',
  'fu-client-list.c',
] + systemd_src

if giounix.found()
  fwupd_engine_src += 'fu-unix-seekable-input-stream.c'
endif
if gudev.found()
  fwupd_engine_src += 'fu-udev-backend.c'
endif
if bluez.allowed()
  fwupd_engine_src += 'fu-bluez-backend.c'
endif

# include event message file
if host_machine.system() == 'windows'
  windmc = find_program('windmc')
  fwupd_rc = custom_target('fwupd-rc',
    input: 'fwupd-windows.mc',
    output: 'fwupd-windows.rc',
    command: [
      windmc, '@INPUT@', '--rcdir', meson.current_build_dir(),
    ],
  )
  windows = import('windows')
  fwupd_engine_src += windows.compile_resources(fwupd_rc)
endif

fwupdutil = library(
  'fwupdutil',
  sources: [
    'fu-console.c',
    'fu-util-bios-setting.c',
    'fu-util-common.c',
    systemd_src,
  ],
  install: true,
  install_rpath: libdir_pkg,
  install_dir: libdir_pkg,
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies: [
    client_dep,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
)

dbus_interface = custom_target('fwupd-generate-dbus-interface',
  input : 'org.freedesktop.fwupd.xml',
  output : 'org.freedesktop.fwupd.xml',
  command : [
    generate_dbus_interface,
    '@INPUT@',
    '@OUTPUT@',
  ],
  install: build_daemon,
  install_dir: join_paths(datadir, 'dbus-1', 'interfaces'),
)

if build_daemon
fwupdmgr = executable(
  'fwupdmgr',
  sources: [
    'fu-util.c',
    client_src,
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies: [
    libfwupd_deps,
    client_dep,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
    fwupdutil,
  ],
  install: true,
  install_rpath: libdir_pkg,
  install_dir: bindir
)
endif

resources_src = gnome.compile_resources(
  'fwupd-resources',
  'fwupd.gresource.xml',
  c_name: 'fu',
  dependencies: [dbus_interface],
)

# generate a header file that allows us to instantiate the plugins without copy-pasting or
# duplicating the meson build logic in the engine
plugin_names = []
foreach lib : plugin_builtins
  plugin_names += lib.full_path()
endforeach
plugins_hdr = custom_target('fwupd-generate-plugins-header',
  output : 'fu-plugin-builtin.h',
  command : [
    generate_plugins_header,
    '@OUTPUT@',
    '.',
    ','.join(plugin_names),
  ],
)

# build all the plugins and engine into one installed library
fwupdengine_rs = custom_target('fu-engine-rs',
  input: 'fu-engine.rs',
  output: ['fu-engine-struct.c', 'fu-engine-struct.h'],
  command: [
    python3,
    join_paths(meson.project_source_root(), 'libfwupdplugin', 'rustgen.py'),
    '@INPUT@', '@OUTPUT0@', '@OUTPUT1@',
  ],
)
fwupdengine = library(
  'fwupdengine',
  fwupdengine_rs,
  resources_src,
  plugins_hdr,
  sources: fwupd_engine_src,
  install: true,
  install_rpath: libdir_pkg,
  install_dir: libdir_pkg,
  include_directories: plugin_incdirs,
  dependencies: [
    engine_dep,
  ],
  link_whole: [
    plugin_builtins,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
)

fwupdtool = executable(
  'fwupdtool',
  fwupdengine_rs,
  plugins_hdr,
  export_dynamic: true,
  sources: [
    'fu-tool.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies: [
    libfwupd_deps,
    libarchive,
    client_dep,
    valgrind,
  ],
  link_with: [
    fwupdengine,
    fwupdutil,
    plugin_libs,
  ],
  install: true,
  install_rpath: libdir_pkg,
  install_dir: bindir
)

if build_daemon
  if get_option('man')
    custom_target('fwupdmgr.1',
      input: 'fwupdmgr.md',
      output: 'fwupdmgr.1',
      command: [
        generate_man, '@INPUT@', '-o', '@OUTPUT@',
        '--replace', 'PACKAGE_VERSION', fwupd_version,
      ],
      install: true,
      install_dir: join_paths(mandir, 'man1'),
    )
  endif
  if build_docs
      md_targets += custom_target('fwupdmgr.md',
        input: 'fwupdmgr.md',
        output: 'fwupdmgr.md',
        command: [
          generate_man, '@INPUT@', '-o', '@OUTPUT@',
          '--replace', 'PACKAGE_VERSION', fwupd_version,
          '--md',
        ],
      )
      man_md += ['"fwupdmgr.md"']
  endif
endif
if build_standalone
  if get_option('man')
    custom_target('fwupdtool.1',
      input: 'fwupdtool.md',
      output: 'fwupdtool.1',
      command: [
        generate_man, '@INPUT@', '-o', '@OUTPUT@',
        '--replace', 'PACKAGE_VERSION', fwupd_version,
      ],
      install: true,
      install_dir: join_paths(mandir, 'man1'),
    )
  endif
  if build_docs
    md_targets += custom_target('fwupdtool.md',
      input: 'fwupdtool.md',
      output: 'fwupdtool.md',
      command: [
        generate_man, '@INPUT@', '-o', '@OUTPUT@',
        '--replace', 'PACKAGE_VERSION', fwupd_version,
        '--md',
      ],
    )
    man_md += ['"fwupdtool.md"']
  endif
endif

if build_daemon

# the StartServiceCtrlDispatcherA design is so different use a different source file
if host_machine.system() == 'windows'
  daemon_loader_src = 'fu-main-windows.c'
else
  daemon_loader_src = 'fu-main.c'
endif

fwupddaemon_rs = custom_target('fu-daemon-rs',
  input: 'fu-daemon.rs',
  output: ['fu-daemon-struct.c', 'fu-daemon-struct.h'],
  command: [
    python3,
    join_paths(meson.project_source_root(), 'libfwupdplugin', 'rustgen.py'),
    '@INPUT@', '@OUTPUT0@', '@OUTPUT1@',
  ],
)
executable(
  'fwupd',
  fwupdengine_rs,
  fwupddaemon_rs,
  plugins_hdr,
  sources: [
    daemon_loader_src,
    'fu-daemon.c',
    'fu-dbus-daemon.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies: [
    valgrind,
    libsystemd,
    engine_dep,
  ],
  link_with: [
    fwupdengine,
    plugin_libs,
  ],
  install: true,
  install_rpath: libdir_pkg,
  install_dir: daemon_dir
)

endif

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'fu-self-test',
    fwupdengine_rs,
    colorhug_test_firmware,
    hwid_test_firmware,
    multiple_rels_test_firmware,
    noreqs_test_firmware,
    plugins_hdr,
    firmware_xml_gz_jcat,
    sources: [
      'fu-self-test.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      engine_dep,
    ],
    link_with: [
      fwupdengine,
      fwupdutil,
      plugin_libs,
    ],
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('fu-self-test', e, is_parallel: false, timeout: 180, env: env)
endif
