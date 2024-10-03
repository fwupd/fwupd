cargs = ['-DG_LOG_DOMAIN="FuPluginDellK2"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('dell-k2.quirk')
plugin_builtins += static_library('fu_plugin_dell_k2',
  rustgen.process(
    'fu-dell-k2.rs',
    'fu-dell-k2-ec.rs',
  ),
  sources: [
    'fu-dell-k2-dpmux-firmware.c',
    'fu-dell-k2-dpmux.c',
    'fu-dell-k2-ec-hid.c',
    'fu-dell-k2-ec.c',
    'fu-dell-k2-ilan-firmware.c',
    'fu-dell-k2-ilan.c',
    'fu-dell-k2-package.c',
    'fu-dell-k2-pd-firmware.c',
    'fu-dell-k2-pd.c',
    'fu-dell-k2-plugin.c',
    'fu-dell-k2-rmm.c',
    'fu-dell-k2-rtshub.c',
    'fu-dell-k2-rtshub-firmware.c',
    'fu-dell-k2-wtpd.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
