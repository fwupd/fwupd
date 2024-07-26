cargs = ['-DG_LOG_DOMAIN="FuPluginDellDock2"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('dell-dock2.quirk')
plugin_builtins += static_library('fu_plugin_dell_dock2',
  rustgen.process(
    'fu-dell-dock2.rs',
    'fu-dell-dock2-ec.rs',
  ),
  sources: [
    'fu-dell-dock2-common.c',
    'fu-dell-dock2-dpmux-firmware.c',
    'fu-dell-dock2-dpmux.c',
    'fu-dell-dock2-ec-firmware.c',
    'fu-dell-dock2-ec-hid.c',
    'fu-dell-dock2-ec.c',
    'fu-dell-dock2-package.c',
    'fu-dell-dock2-pd-firmware.c',
    'fu-dell-dock2-pd.c',
    'fu-dell-dock2-plugin.c',
    'fu-dell-dock2-rmm.c',
    'fu-dell-dock2-rtshub.c',
    'fu-dell-dock2-rtshub-firmware.c',
    'fu-dell-dock2-wtpd.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    gudev,
  ],
)
