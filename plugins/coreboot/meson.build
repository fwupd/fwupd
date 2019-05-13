cargs = ['-DG_LOG_DOMAIN="FuPluginCoreboot"']

install_data(['coreboot.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_coreboot',
  sources : [
    'fu-plugin-coreboot.c',
    'fu-coreboot-fmap.c',
    'fu-coreboot-cbfs.c',
    'fu-coreboot-misc.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : [
    cargs,
  ],
  dependencies : [
    plugin_deps,
  ],
)
