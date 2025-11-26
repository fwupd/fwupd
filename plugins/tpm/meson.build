hsi or subdir_done()
tpm2tss_tpm = dependency('tss2-esys', version: '>= 2.0', required: false)
tpm2tss_tpm.found() or subdir_done()
host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginTpm"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('tpm.quirk')
plugin_builtin_tpm = static_library('fu_plugin_tpm',
  rustgen.process('fu-tpm.rs'),
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
  env.set('G_DEBUG', 'fatal-criticals')
  env.set('LSAN_OPTIONS', 'suppressions=@0@'.format(join_paths(meson.project_source_root(), 'data', 'tests', 'lsan-suppressions.txt')))
  e = executable(
    'tpm-self-test',
    rustgen.process('fu-tpm.rs'),
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
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('tpm-self-test', e, env: env)

  install_data([
      'tests/tpm0/active',
      'tests/tpm0/caps',
      'tests/tpm0/enabled',
      'tests/tpm0/owned',
      'tests/tpm0/pcrs',
    ],
    install_dir: join_paths(installed_test_datadir, 'tests', 'tpm0'),
  )
  install_data([
      'tests/empty_pcr/tpm0/active',
      'tests/empty_pcr/tpm0/caps',
      'tests/empty_pcr/tpm0/enabled',
      'tests/empty_pcr/tpm0/owned',
      'tests/empty_pcr/tpm0/pcrs',
    ],
    install_dir: join_paths(installed_test_datadir, 'tests', 'empty_pcr', 'tpm0'),
  )

endif

executable(
  'fwupdtpmevlog',
  rustgen.process('fu-tpm.rs'),
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
