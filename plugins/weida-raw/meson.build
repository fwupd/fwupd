if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginWeidaRaw"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('weida-raw.quirk')
plugin_builtins += static_library('fu_plugin_weida_raw',
  rustgen.process('fu-weida-raw.rs'),
  sources: [
    'fu-weida-raw-common.c',
    'fu-weida-raw-device.c',
    'fu-weida-raw-firmware.c',
    'fu-weida-raw-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
