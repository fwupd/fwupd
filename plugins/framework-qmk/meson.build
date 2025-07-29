cargs = ['-DG_LOG_DOMAIN="FuPluginFrameworkQmk"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('framework-qmk.quirk')
plugin_builtins += static_library('fu_plugin_framework_qmk',
  rustgen.process('fu-framework-qmk.rs'),
  sources: [
    'fu-framework-qmk-device.c',
    'fu-framework-qmk-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
