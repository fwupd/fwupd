cargs = ['-DG_LOG_DOMAIN="FuPluginSwap"']

shared_module('fu_plugin_swap',
  fu_hash,
  sources : [
    'fu-plugin-swap.c',
    'fu-swap.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  e = executable(
    'swap-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-swap.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
  )
  test('swap-self-test', e)
endif
