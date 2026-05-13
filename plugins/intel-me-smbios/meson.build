hsi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginIntelMeSmbios"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_builtin_intel_me_smbios = static_library('fu_plugin_intel_me_smbios',
  rustgen.process('fu-intel-me-smbios.rs'),
  sources: [
    'fu-intel-me-smbios-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_intel_me_smbios

if get_option('tests')
  subdir('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'intel-me-smbios-self-test',
    rustgen.process('fu-intel-me-smbios.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      plugin_builtin_intel_me_smbios,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('intel-me-smbios-self-test', e, env: env)
endif
