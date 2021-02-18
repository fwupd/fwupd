if get_option('plugin_tpm')
cargs = ['-DG_LOG_DOMAIN="FuPluginTpmEventlog"']

shared_module('fu_plugin_tpm_eventlog',
  fu_hash,
  sources : [
    'fu-plugin-tpm-eventlog.c',
    'fu-tpm-eventlog-common.c',
    'fu-tpm-eventlog-device.c',
    'fu-tpm-eventlog-parser.c',
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
  testdatadirs = environment()
  testdatadirs.set('G_TEST_SRCDIR', meson.current_source_dir())
  testdatadirs.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'tpm-eventlog-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-tpm-eventlog-common.c',
      'fu-tpm-eventlog-device.c',
      'fu-tpm-eventlog-parser.c',
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
  test('tpm-eventlog-self-test', e, env : testdatadirs)  # added to installed-tests
endif

fwupdtpmevlog = executable(
  'fwupdtpmevlog',
  sources : [
    'fu-tpm-eventlog.c',
    'fu-tpm-eventlog-common.c',
    'fu-tpm-eventlog-parser.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
      plugin_deps,
      tpm2tss,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  install : true,
  install_dir : bindir
)

if get_option('man')
  custom_target('fwupdtpmevlog-man',
    input : fwupdtpmevlog,
    output : 'fwupdtpmevlog.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'Debugging utility for the TPM event log',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
    ],
    install : true,
    install_dir : join_paths(mandir, 'man1'),
  )
endif

if honggfuzz.found()
  run_target('fuzz-tpm-eventlog',
    command: [
      honggfuzz,
      '--input', join_paths(meson.current_source_dir(), 'fuzzing'),
      '--output', join_paths(meson.current_build_dir(), 'fuzzing-corpus'),
      '--workspace', join_paths(meson.current_build_dir(), 'fuzzing-findings'),
      '--verifier',
      '--', fwupdtpmevlog, '___FILE___',
    ],
  )
endif
endif
