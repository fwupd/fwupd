if get_option('gusb')
cargs = ['-DG_LOG_DOMAIN="FuPluginCcgx"']

install_data([
  'ccgx-ids.quirk',
  'ccgx.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_ccgx',
  fu_hash,
  sources : [
    'fu-plugin-ccgx.c',
    'fu-ccgx-common.c',         # fuzzing
    'fu-ccgx-firmware.c',       # fuzzing
    'fu-ccgx-hid-device.c',
    'fu-ccgx-hpi-common.c',
    'fu-ccgx-hpi-device.c',
    'fu-ccgx-dmc-device.c',
    'fu-ccgx-dmc-firmware.c',   # fuzzing
    'fu-ccgx-dmc-common.c',     # fuzzing
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
    gudev,
  ],
)
endif

if get_option('tests')
  e = executable(
    'ccgx-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-ccgx-common.c',
      'fu-ccgx-firmware.c',
      'fu-ccgx-dmc-common.c',
      'fu-ccgx-dmc-firmware.c',
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
    install : true,
    install_dir : installed_test_bindir,
  )
  test('ccgx-self-test', e)
endif
