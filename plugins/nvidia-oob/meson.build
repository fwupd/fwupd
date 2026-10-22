host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginNvidiaOob"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('nvidia-oob.quirk')
plugin_builtin_nvidia_oob = static_library(
  'fu_plugin_nvidia_oob',
  sources: [
    'fu-nvidia-oob-bmc.c',
    'fu-nvidia-oob-device.c',
    'fu-nvidia-oob-plugin.c',
    'fu-nvidia-oob-request.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libcurl,
  ],
)
plugin_builtins += plugin_builtin_nvidia_oob

if get_option('tests')
  install_data(
    ['tests/nvidia-oob-mock.py'],
    install_dir: join_paths(installed_test_datadir, 'tests'),
  )
  e = executable(
    'nvidia-oob-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libcurl,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_nvidia_oob,
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
  test(
    'nvidia-oob-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
