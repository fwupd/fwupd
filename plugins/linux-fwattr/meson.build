allow_uefi or subdir_done()
host_machine.system() == 'linux' or subdir_done()

if get_option('tests')
  subdir('tests')
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxFwattr"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_builtin_linux_fwattr = static_library(
  'fu_plugin_linux_fwattr',
  sources: [
    'fu-linux-fwattr-plugin.c',
    'fu-linux-fwattr-setting.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_linux_fwattr

if get_option('tests')
  e = executable(
    'linux-fwattr-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_linux_fwattr,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test(
    'linux-fwattr-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
