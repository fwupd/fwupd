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
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
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
  test('tpm-eventlog-self-test', e)
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

run_target('fuzz-tpm-eventlog',
  command: [
    join_paths(meson.source_root(), 'contrib/afl-fuzz.py'),
    '-i', join_paths(meson.current_source_dir(), 'tests'),
    '-o', join_paths(meson.current_build_dir(), 'findings'),
    fwupdtpmevlog,
  ],
)
