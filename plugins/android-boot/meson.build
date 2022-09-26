if get_option('plugin_android_boot').require(gudev.found(),
    error_message: 'gudev is needed for plugin_android_boot').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginAndroidBoot"']

plugin_quirks += files('android-boot.quirk')

shared_module('fu_plugin_android_boot',
  fu_hash,
  sources : [
    'fu-plugin-android-boot.c',
    'fu-android-boot-device.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
endif
