cargs = ['-DG_LOG_DOMAIN="FuPluginTest"']

install_dummy = false
if get_option('plugin_dummy')
  install_dummy = true
endif

if bluez.allowed()
  plugin_quirks += files('test-ble.quirk')
endif

plugin_builtins += static_library('fu_plugin_test',
  sources: [
    'fu-test-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

shared_module('fu_plugin_invalid',
  fu_hash,
  sources: [
    'fu-plugin-invalid.c',
  ],
  include_directories: plugin_incdirs,
  install: install_dummy,
  install_dir: libdir_pkg,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

if bluez.allowed()
plugin_builtins += static_library('fu_plugin_test_ble',
  sources: [
    'fu-test-ble-plugin.c',
    'fu-test-ble-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
