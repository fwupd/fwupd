tpm2tss_tpm = dependency('tss2-esys', version: '>= 2.0', required: get_option('plugin_tpm'))

if hsi and \
   tpm2tss_tpm.found() and \
   get_option('plugin_tpm').require(gudev.found(),
    error_message: 'gudev is needed for plugin_tpm').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginTpm"']

plugin_quirks += join_paths(meson.current_source_dir(), 'tpm.quirk')

plugin_tpm = shared_module('fu_plugin_tpm',
  fu_hash,
  sources: [
    'fu-plugin-tpm.c',
    'fu-tpm-device.c',
    'fu-tpm-v1-device.c',
    'fu-tpm-v2-device.c',
    'fu-tpm-eventlog-common.c',
    'fu-tpm-eventlog-parser.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
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

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'tpm-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
      'fu-tpm-device.c',
      'fu-tpm-v1-device.c',
      'fu-tpm-v2-device.c',
      'fu-tpm-eventlog-common.c',
      'fu-tpm-eventlog-parser.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      plugin_deps,
      tpm2tss_tpm,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    c_args: cargs
  )
  test('tpm-self-test', e, env: env)
endif

executable(
  'fwupdtpmevlog',
  fu_hash,
  sources: [
    'fu-tpm-eventlog.c',
    'fu-tpm-eventlog-common.c',
    'fu-tpm-eventlog-parser.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies: [
      plugin_deps,
      tpm2tss_tpm,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
)

endif
