if get_option('plugin_android_boot').require(gudev.found(),
    error_message: 'gudev is needed for plugin_android_boot').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginAndroidBoot"']

plugin_quirks += files('android-boot.quirk')
plugin_builtins += static_library('fu_plugin_android_boot',
  sources: [
    'fu-android-boot-plugin.c',
    'fu-android-boot-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
