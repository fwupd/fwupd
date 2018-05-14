cargs = ['-DG_LOG_DOMAIN="FuPluginEbitdo"']

install_data(['ebitdo.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_ebitdo',
  sources : [
    'fu-plugin-ebitdo.c',
    'fu-ebitdo-common.c',
    'fu-ebitdo-device.c',
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

ebitdo_static = static_library('ebitdo_static',
  sources : [
    'fu-plugin-ebitdo.c',
    'fu-ebitdo-common.c',
    'fu-ebitdo-device.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : false,
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
    'fu-ebitdo-device.c',
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
    fwupdprivate,
  ],
  c_args : cargs,
)
