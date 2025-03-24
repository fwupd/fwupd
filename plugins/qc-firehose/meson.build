if get_option('plugin_qc_firehose')
cargs = ['-DG_LOG_DOMAIN="FuPluginQcFirehose"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('qc-firehose.quirk')
plugin_builtins += static_library('fu_plugin_qc_firehose',
  rustgen.process('fu-qc-firehose.rs'),
  sources: [
    'fu-qc-firehose-impl.c',
    'fu-qc-firehose-impl-common.c',
    'fu-qc-firehose-sahara-impl.c',
    'fu-qc-firehose-usb-device.c',
    'fu-qc-firehose-raw-device.c',
    'fu-qc-firehose-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/qc-ec25au.json',
  'tests/qc-eg25ggc.json',
  'tests/qc-em160r.json',
)
enumeration_data += files(
  'tests/qc-ec25au-setup.json',
  'tests/qc-em160r-setup.json',
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'qc-firehose-self-test',
    rustgen.process('fu-qc-firehose.rs'),
    sources: [
      'fu-self-test.c',
      'fu-qc-firehose-impl-common.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      platform_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('qc-firehose-self-test', e, env: env)
endif

endif
