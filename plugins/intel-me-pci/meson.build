hsi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginIntelMePci"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('intel-me-pci.quirk')
plugin_builtin_intel_me_pci = static_library('fu_plugin_intel_me_pci',
  sources: [
    'fu-intel-me-pci-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_intel_me_pci

enumeration_data += files(
  'tests/intel-me-setup.json',
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'intel-me-pci-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      plugin_builtin_intel_me_pci,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('intel-me-pci-self-test', e, env: env)
endif
