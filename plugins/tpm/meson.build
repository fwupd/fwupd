tpm2tss_tpm = dependency('tss2-esys', version: '>= 2.0', required: get_option('plugin_tpm'))

if hsi and \
   tpm2tss_tpm.found() and \
   get_option('plugin_tpm').require(gudev.found(),
    error_message: 'gudev is needed for plugin_tpm').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginTpm"']

plugin_quirks += files('tpm.quirk')

plugin_builtin_tpm = static_library('fu_plugin_tpm',
  sources: [
    'fu-tpm-plugin.c',
    'fu-tpm-device.c',
    'fu-tpm-v1-device.c',
    'fu-tpm-v2-device.c',
    'fu-tpm-eventlog-common.c',
    'fu-tpm-eventlog-parser.c',
  ],
  include_directories: plugin_incdirs,
  link_with: [
    fwupdplugin,
    fwupd,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
    tpm2tss_tpm,
  ],
)
plugin_builtins += plugin_builtin_tpm

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'tpm-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      tpm2tss_tpm,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_tpm,
    ],
    c_args: cargs
  )
  test('tpm-self-test', e, env: env)
endif

executable(
  'fwupdtpmevlog',
  sources: [
    'fu-tpm-eventlog.c',
  ],
  include_directories: plugin_incdirs,
  dependencies: [
      plugin_deps,
      tpm2tss_tpm,
  ],
  link_with: [
    plugin_libs,
    plugin_builtin_tpm,
  ],
)

endif
