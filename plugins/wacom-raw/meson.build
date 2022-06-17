if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginWacomRaw"']

plugin_quirks += join_paths(meson.current_source_dir(), 'wacom-raw.quirk')

shared_module('fu_plugin_wacom_raw',
  fu_hash,
  sources: [
    'fu-plugin-wacom-raw.c',
    'fu-wacom-common.c',
    'fu-wacom-device.c',
    'fu-wacom-aes-device.c',
    'fu-wacom-emr-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
)
endif
