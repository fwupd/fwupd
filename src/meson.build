cargs = ['-DG_LOG_DOMAIN="Fu"']

install_data(['org.freedesktop.fwupd.xml'],
  install_dir : join_paths(get_option('datadir'), 'dbus-1', 'interfaces')
)

executable(
  'fwupdmgr',
  sources : [
    'fu-device.c',
    'fu-pending.c',
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
  ],
  link_with : fwupd,
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
    '-DFU_OFFLINE_DESTDIR=""',
  ],
  install : true,
  install_dir : 'bin'
)

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
    'fu-main.c',
    'fu-debug.c',
    'fu-device.c',
    'fu-keyring.c',
    'fu-pending.c',
    'fu-plugin.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../libfwupd'),
  ],
  dependencies : [
    appstream_glib,
    giounix,
    gmodule,
    gudev,
    gusb,
    polkit,
    soup,
    sqlite,
    gpgme,
    gpgerror,
    valgrind,
  ],
  link_with : fwupd,
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
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
    colorhug_test_firmware,
    sources : [
      'fu-self-test.c',
      'fu-device.c',
      'fu-pending.c',
      'fu-keyring.c',
      'fu-plugin.c',
      'fu-test.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../libfwupd'),
    ],
    dependencies : [
      appstream_glib,
      giounix,
      gmodule,
      gudev,
      gusb,
      polkit,
      soup,
      sqlite,
      gpgme,
      gpgerror,
      valgrind,
    ],
    link_with : [
      fwupd,
    ],
    c_args : [
      cargs,
      '-DTESTDATADIR="' + testdatadir_src + ':' + testdatadir_dst + '"',
      '-DLOCALSTATEDIR="/tmp/fwupd-self-test/var"',
      '-DPLUGINBUILDDIR="' + pluginbuilddir + '"',
      '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"',
    ],
  )
  test('fu-self-test', e, is_parallel:false)
endif
