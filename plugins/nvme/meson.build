nvme_header = cc.has_header('linux/nvme_ioctl.h', required: get_option('plugin_nvme'))

if nvme_header and \
   get_option('plugin_nvme').require(gudev.found(),
    error_message: 'gudev is needed for plugin_nvme').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginNvme"']

plugin_quirks += files('nvme.quirk')
plugin_builtin_nvme = static_library('fu_plugin_nvme',
  sources: [
    'fu-nvme-plugin.c',
    'fu-nvme-common.c',
    'fu-nvme-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_nvme

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_DATADIR_QUIRKS', meson.current_source_dir())
  e = executable(
    'nvme-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_nvme,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('nvme-self-test', e, env: env)  # added to installed-tests
endif
endif
