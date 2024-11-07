if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginDellKestrel"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('dell-kestrel.quirk')
plugin_builtins += static_library('fu_plugin_dell_kestrel',
  rustgen.process(
    'fu-dell-kestrel-ec-hid.rs',
    'fu-dell-kestrel-ec.rs',
    'fu-dell-kestrel-rtshub.rs',
  ),
  sources: [
    'fu-dell-kestrel-dpmux.c',
    'fu-dell-kestrel-ec-hid.c',
    'fu-dell-kestrel-ec.c',
    'fu-dell-kestrel-ilan.c',
    'fu-dell-kestrel-package.c',
    'fu-dell-kestrel-pd.c',
    'fu-dell-kestrel-plugin.c',
    'fu-dell-kestrel-rmm.c',
    'fu-dell-kestrel-rtshub.c',
    'fu-dell-kestrel-rtshub-firmware.c',
    'fu-dell-kestrel-wtpd.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
