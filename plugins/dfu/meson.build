cargs = ['-DG_LOG_DOMAIN="FuPluginDfu"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('dfu.quirk')

# do not use structgen as these files are used in the dfu-csr plugin too...
dfu_rs = custom_target('fu-dfu-rs',
  input: 'fu-dfu.rs',
  output: ['fu-dfu-struct.c', 'fu-dfu-struct.h'],
  command: [
    python3,
    join_paths(meson.project_source_root(), 'libfwupdplugin', 'rustgen.py'),
    '@INPUT@', '@OUTPUT0@', '@OUTPUT1@',
  ],
)
plugin_builtin_dfu = static_library('fu_plugin_dfu',
  dfu_rs,
  sources: [
    'fu-dfu-plugin.c',
    'fu-dfu-common.c',
    'fu-dfu-device.c',
    'fu-dfu-sector.c',
    'fu-dfu-target.c',
    'fu-dfu-target-stm.c',
    'fu-dfu-target-avr.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libm,
  ],
  link_with: plugin_libs,
)
plugin_builtins += plugin_builtin_dfu

enumeration_data += files('tests/dfu-gd32vf103-setup.json')
device_tests += files(
  'tests/dfu-gd32vf103.json',
  'tests/fwupd-a3bu-xplained.json',
  'tests/fwupd-at90usbkey.json',
  'tests/realtek-rts5855.json',
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'fu-dfu-self-test',
    dfu_rs,
    sources: [
      'fu-dfu-self-test.c'
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libm,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_dfu,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('fu-dfu-self-test', e, env: env)  # added to installed-tests
endif

plugindfu_incdir = include_directories('.')
