cargs = ['-DG_LOG_DOMAIN="FuPluginCsr"']

install_data([
  'csr-aiaiai-h05.quirk',
  'csr-aiaiai-h60.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_csr',
  sources : [
    'fu-csr-device.c',
    'fu-plugin-csr.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../dfu'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    dfu,
  ],
)

csr_static = static_library('csr_static',
  sources : [
    'fu-csr-device.c',
    'fu-plugin-csr.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../dfu'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : false,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    dfu,
  ],
)

executable(
  'fu-csr-tool',
  sources : [
    'fu-csr-device.c',
    'fu-csr-tool.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../dfu'),
    include_directories('../../libfwupd'),
    include_directories('../../src'),
  ],
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    dfu,
    fwupd,
    fwupdprivate,
  ],
  c_args : cargs,
)
