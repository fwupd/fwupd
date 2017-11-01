cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']

smbios_deps = []
if libsmbios_c.found() and libsmbios_c.version().version_compare('>=2.3.0')
  smbios_deps += libsmbios_c
endif

shared_module('fu_plugin_dell',
  sources : [
    'fu-plugin-dell.c',
    'fu-dell-smi.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
      cargs,
      '-DLOCALSTATEDIR="' + localstatedir + '"',
    ],
  dependencies : [
    plugin_deps,
    gudev,
    efivar,
    smbios_deps,
    fwup,
  ],
)

if get_option('enable-tests')
  cargs += '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'dell-self-test',
    sources : [
      'fu-self-test.c',
      'fu-dell-smi.c',
      'fu-plugin-dell.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      gudev,
      efivar,
      fwup,
      sqlite,
      gudev,
      smbios_deps,
      valgrind,
    ],
    link_with : [
      fwupd,
      libfwupdprivate,
    ],
    c_args : [
      cargs,
      '-DLOCALSTATEDIR="/tmp/fwupd-self-test/var"',
    ],
  )
  test('dell-self-test', e)
endif
