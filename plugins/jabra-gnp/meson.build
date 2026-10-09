cargs = ['-DG_LOG_DOMAIN="FuPluginJabraGnp"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('jabra-gnp.quirk')
plugin_builtin_jabra_gnp = static_library(
  'fu_plugin_jabra_gnp',
  rustgen.process('fu-jabra-gnp.rs'),
  sources: [
    'fu-jabra-gnp-child-device.c',
    'fu-jabra-gnp-common.c',
    'fu-jabra-gnp-device.c',
    'fu-jabra-gnp-firmware.c',
    'fu-jabra-gnp-image.c',
    'fu-jabra-gnp-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_jabra_gnp

device_tests += files('tests/jabra-evolve2-75.json')

if get_option('tests')
  e = executable(
    'jabra-gnp-self-test',
    rustgen.process('fu-jabra-gnp.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_jabra_gnp,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test(
    'jabra-gnp-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
