host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechRallysystem"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('logitech-rallysystem.quirk')

plugin_builtins += static_library('fu_plugin_logitech_rallysystem',
  rustgen.process('fu-logitech-rallysystem.rs'),
  sources: [
    'fu-logitech-rallysystem-plugin.c',
    'fu-logitech-rallysystem-audio-device.c',
    'fu-logitech-rallysystem-tablehub-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
