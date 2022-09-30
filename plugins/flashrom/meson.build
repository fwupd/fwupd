if get_option('plugin_flashrom').enabled() or \
   (get_option('plugin_flashrom').auto() and libflashrom.found())
cargs = ['-DG_LOG_DOMAIN="FuPluginFlashrom"']

plugin_quirks += files('flashrom.quirk')

shared_module('fu_plugin_flashrom',
  sources: [
    'fu-flashrom-device.c',
    'fu-flashrom-plugin.c',
    'fu-flashrom-cmos.c',
  ],
  include_directories: plugin_incdirs,
  install: true,
  install_dir: libdir_pkg,
  link_with: plugin_libs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies: [
    plugin_deps,
    libflashrom,
  ],
)
endif
