cargs = ['-DG_LOG_DOMAIN="FuPluginEbitdo"']

shared_module('fu_plugin_ebitdo',
  sources : [
    'fu-plugin-ebitdo.c',
    'fu-ebitdo-common.c',
    'fu-device-ebitdo.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

executable(
  'fu-ebitdo-tool',
  sources : [
    'fu-ebitdo-tool.c',
    'fu-ebitdo-common.c',
    'fu-device-ebitdo.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../libfwupd'),
    include_directories('../../src'),
  ],
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : cargs,
)
