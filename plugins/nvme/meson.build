nvme_header = cc.has_header('linux/nvme_ioctl.h', required: get_option('plugin_nvme'))

if nvme_header and \
   get_option('plugin_nvme').require(gudev.found(),
    error_message: 'gudev is needed for plugin_nvme').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginNvme"']

plugin_quirks += join_paths(meson.current_source_dir(), 'nvme.quirk')

shared_module('fu_plugin_nvme',
  fu_hash,
  sources: [
    'fu-plugin-nvme.c',
    'fu-nvme-common.c',
    'fu-nvme-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_DATADIR_QUIRKS', meson.current_source_dir())
  e = executable(
    'nvme-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
      'fu-nvme-common.c',
      'fu-nvme-device.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    install: true,
    install_dir: installed_test_bindir,
  )
  test('nvme-self-test', e, env: env)  # added to installed-tests
endif
endif
