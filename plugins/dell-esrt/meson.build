cargs = ['-DG_LOG_DOMAIN="FuPluginDellEsrt"']

shared_module('fu_plugin_dell_esrt',
  sources : [
    'fu-plugin-dell-esrt.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
  ],
  link_with : [
    libfwupdprivate,
  ],
  dependencies : [
    plugin_deps,
    libsmbios_c,
  ],
)
