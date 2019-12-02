cargs = ['-DG_LOG_DOMAIN="FuPluginTpmEventlog"']

shared_module('fu_plugin_tpm_eventlog',
  fu_hash,
  sources : [
    'fu-plugin-tpm-eventlog.c',
    'fu-tpm-eventlog-common.c',
    'fu-tpm-eventlog-device.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupdplugin,
    fwupd,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
    tpm2tss,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'tpm-eventlog-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-tpm-eventlog-common.c',
      'fu-tpm-eventlog-device.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    link_with : [
      fwupdplugin,
      fwupd,
    ],
    dependencies : [
      plugin_deps,
      tpm2tss,
    ],
    c_args : cargs
  )
  test('tpm-eventlog-self-test', e)
endif
