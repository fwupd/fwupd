cargs = ['-DG_LOG_DOMAIN="FuPluginTest"']

install_dummy = false
if get_option('plugin_dummy')
  install_dummy = true
endif

if bluez.allowed()
  plugin_quirks += join_paths(meson.current_source_dir(), 'test-ble.quirk')
endif

shared_module('fu_plugin_test',
  fu_hash,
  sources: [
    'fu-plugin-test.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: install_dummy,
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

shared_module('fu_plugin_invalid',
  fu_hash,
  sources: [
    'fu-plugin-invalid.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: install_dummy,
  install_dir: plugin_dir,
  link_with: [
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)

if bluez.allowed()
shared_module('fu_plugin_test_ble',
  fu_hash,
  sources: [
    'fu-plugin-test-ble.c',
    'fu-test-ble-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: install_dummy,
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
endif
