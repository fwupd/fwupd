libmm_glib = dependency('mm-glib', version: '>= 1.10.0', required: get_option('plugin_modem_manager'))
libqmi_glib = dependency('qmi-glib', version: '>= 1.23.1', required: get_option('plugin_modem_manager'))
libmbim_glib = dependency('mbim-glib', version: '>= 1.22.0', required: get_option('plugin_modem_manager'))

if libmm_glib.found() and \
   libqmi_glib.found() and \
   libmbim_glib.found() and \
   get_option('plugin_modem_manager').require(gudev.found(),
    error_message: 'gudev is needed for plugin_modem_manager').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginMm"']
cargs +=['-DMM_REQUIRED_VERSION="1.10.0"']

plugin_quirks += join_paths(meson.current_source_dir(), 'modem-manager.quirk')

shared_module('fu_plugin_modem_manager',
  fu_hash,
  sources: [
    'fu-plugin-modem-manager.c',
    'fu-mm-device.c',
    'fu-qmi-pdc-updater.c',
    'fu-mbim-qdu-updater.c',
    'fu-firehose-updater.c',
    'fu-sahara-loader.c',
    'fu-mm-utils.c'
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: [
    cargs,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  dependencies: [
    plugin_deps,
    libmm_glib,
    libqmi_glib,
    libmbim_glib,
  ],
)
endif
