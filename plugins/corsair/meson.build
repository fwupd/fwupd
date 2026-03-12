cargs = ['-DG_LOG_DOMAIN="FuPluginCorsair"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += join_paths(meson.current_source_dir(), 'corsair.quirk')
plugin_builtins += static_library('fu_plugin_corsair',
  rustgen.process('fu-corsair.rs'),
  sources: [
    'fu-corsair-plugin.c',
    'fu-corsair-common.c',
    'fu-corsair-device.c',
    'fu-corsair-bp.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/corsair-katar-pro-xt.json',
  'tests/corsair-sabre-pro.json',
  'tests/corsair-sabre-rgb-pro.json',
)
