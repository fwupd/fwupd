host_machine.system() == 'linux' or subdir_done()

plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
cargs = ['-DG_LOG_DOMAIN="FuPluginWacomRaw"']

plugin_quirks += files('wacom-raw.quirk')
plugin_builtin_wacom_raw = static_library(
  'fu_plugin_wacom_raw',
  rustgen.process('fu-wacom-raw.rs'),
  sources: [
    'fu-wacom-raw-plugin.c',
    'fu-wacom-raw-common.c',
    'fu-wacom-raw-device.c',
    'fu-wacom-raw-aes-device.c',
    'fu-wacom-raw-emr-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  dependencies: plugin_deps,
  link_with: plugin_libs,
)
plugin_builtins += plugin_builtin_wacom_raw

device_tests += files('tests/wacom-g14t.json')

if get_option('tests')
  e = executable(
    'wacom-raw-self-test',
    rustgen.process('fu-wacom-raw.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_wacom_raw,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test(
    'wacom-raw-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
