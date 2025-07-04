nvme_header = cc.has_header('linux/nvme_ioctl.h', required: false)

if nvme_header and host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginNvme"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

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

device_tests += files(
  'tests/lenovo-pcsn720.json',
)

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
