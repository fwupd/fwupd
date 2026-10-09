cargs = ['-DG_LOG_DOMAIN="FuPluginDellDock"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('dell-dock.quirk')
plugin_builtin_dell_dock = static_library(
  'fu_plugin_dell_dock',
  rustgen.process('fu-dell-dock.rs'),
  sources: [
    'fu-dell-dock-plugin.c',
    'fu-dell-dock-common.c',
    'fu-dell-dock-hid.c',
    'fu-dell-dock-status.c',
    'fu-dell-dock-ec.c',
    'fu-dell-dock-hub.c',
    'fu-dell-dock-tbt.c',
    'fu-dell-dock-mst.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_dell_dock

if get_option('tests')
  e = executable(
    'dell-dock-self-test',
    rustgen.process('fu-dell-dock.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_dell_dock,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test(
    'dell-dock-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
