cargs = ['-DG_LOG_DOMAIN="FuPluginFocalMoc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('focal-moc.quirk')
plugin_builtin_focal_moc = static_library('fu_plugin_focal_moc',
  rustgen.process('fu-focal-moc.rs'),
  sources: [
    'fu-focal-moc-common.c',
    'fu-focal-moc-device.c',
    'fu-focal-moc-firmware.c',
    'fu-focal-moc-plugin.c',
    'fu-focal-moc-signed-device.c',
    'fu-focal-moc-transport-impl.c',
    'fu-focal-moc-transport.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_focal_moc

device_tests += files('tests/focal-moc.json')

if get_option('tests')
  install_data(['tests/focal-moc.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  e = executable(
    'focal-moc-self-test',
    rustgen.process('fu-focal-moc.rs'),
    sources: [
      'fu-focal-moc-test-device.c',
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    c_args: cargs + [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_focal_moc,
    ],
    install: true,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    install_rpath: libdir_pkg,
  )
  test(
    'focal-moc-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
