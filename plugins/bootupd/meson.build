host_machine.system() == 'linux' or subdir_done()
libsystemd.found() or subdir_done()
cc.has_header(
  'systemd/sd-varlink.h',
  dependencies: libsystemd,
) or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginBootupd"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_builtin_bootupd = static_library(
  'fu_plugin_bootupd',
  sources: [
    'fu-bootupd-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libsystemd,
  ],
)
plugin_builtins += plugin_builtin_bootupd

if get_option('tests')
  e = executable(
    'bootupd-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libsystemd,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_bootupd,
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
    'bootupd-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
