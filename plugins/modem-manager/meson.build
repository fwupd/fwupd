libmm_glib = dependency('mm-glib', version: '>= 1.18.0', required: get_option('plugin_modem_manager'))
libqmi_glib = dependency('qmi-glib', version: '>= 1.30.0', required: get_option('plugin_modem_manager'))
libmbim_glib = dependency('mbim-glib', version: '>= 1.26.0', required: get_option('plugin_modem_manager'))

if libmm_glib.found() and \
   libqmi_glib.found() and \
   libmbim_glib.found() and \
   get_option('plugin_modem_manager').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginMm"']
cargs +=['-DMM_REQUIRED_VERSION="1.10.0"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('modem-manager.quirk')

shared_module('fu_plugin_modem_manager',
  rustgen.process(
    'fu-cinterion-fdl-updater.rs',
  ),
  sources: [
    'fu-mm-plugin.c',
    'fu-mm-device.c',
    'fu-qmi-pdc-updater.c',
    'fu-mbim-qdu-updater.c',
    'fu-firehose-updater.c',
    'fu-sahara-loader.c',
    'fu-cinterion-fdl-updater.c',
  ],
  include_directories: plugin_incdirs,
  install: true,
  install_rpath: libdir_pkg,
  install_dir: libdir_pkg,
  c_args: cargs,
  link_with: plugin_libs,
  dependencies: [
    plugin_deps,
    libmm_glib,
    libqmi_glib,
    libmbim_glib,
  ],
)
endif
