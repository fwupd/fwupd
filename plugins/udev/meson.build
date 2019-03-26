cargs = ['-DG_LOG_DOMAIN="FuPluginUdev"']

shared_module('fu_plugin_udev',
  fu_hash,
  sources : [
    'fu-plugin-udev.c',
    'fu-rom.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

executable(
  'fu-rom-tool',
  fu_hash,
  sources : [
    'fu-rom-tool.c',
    'fu-rom.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  dependencies : [
    plugin_deps,
    libjsonglib,
  ],
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
)

if get_option('tests')
  cargs += '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'udev-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-rom.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : cargs
  )
  test('udev-self-test', e)
endif
