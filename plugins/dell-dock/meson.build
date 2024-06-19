if libusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginDellDock"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('dell-dock.quirk')
plugin_builtins += static_library('fu_plugin_dell_dock',
  rustgen.process(
    'fu-dell-dock.rs',        # fuzzing
    'fu-dell-dock-i2c-ec.rs', # fuzzing
    'fu-dell-dock-ec-v2.rs',  # fuzzing
  ),
  sources: [
    'fu-dell-dock-plugin.c',
    'fu-dell-dock-common.c',
    'fu-dell-dock-hid.c',
    'fu-dell-dock-hid-v2.c',
    'fu-dell-dock-status.c',
    'fu-dell-dock-hub.c',
    'fu-dell-dock-i2c-tbt.c',
    'fu-dell-dock-i2c-mst.c',
    'fu-dell-dock-i2c-ec.c',
    'fu-dell-dock-ec-v2.c',
    'fu-dell-dock-i2c-pd.c',
    'fu-dell-dock-i2c-pd-firmware.c',
    'fu-dell-dock-i2c-wtpd.c',
    'fu-dell-dock-i2c-dpmux.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    gudev,
  ],
)
endif
