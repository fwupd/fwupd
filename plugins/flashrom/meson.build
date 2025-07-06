if not allow_flashrom
  subdir_done()
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginFlashrom"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('flashrom.quirk')

shared_module('fu_plugin_flashrom',
  sources: [
    'fu-flashrom-device.c',
    'fu-flashrom-plugin.c',
    'fu-flashrom-cmos.c',
  ],
  include_directories: plugin_incdirs,
  install: true,
  install_rpath: libdir_pkg,
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
