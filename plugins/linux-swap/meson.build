if hsi
cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxSwap"']

shared_module('fu_plugin_linux_swap',
  fu_hash,
  sources: [
    'fu-plugin-linux-swap.c',
    'fu-linux-swap.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  e = executable(
    'linux-swap-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
      'fu-linux-swap.c',
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
  test('linux-swap-self-test', e)  # added to installed-tests
endif
endif
