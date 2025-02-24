cargs = ['-DG_LOG_DOMAIN="FuPluginBnrDp"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('bnr-dp.quirk')
plugin_builtins += static_library(
    'fu_plugin_bnr_dp',
    rustgen.process('fu-bnr-dp.rs'),
    sources: [
        'fu-bnr-dp-common.c',
        'fu-bnr-dp-device.c',
        'fu-bnr-dp-firmware.c',
        'fu-bnr-dp-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
)
