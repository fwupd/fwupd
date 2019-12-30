cargs = ['-DG_LOG_DOMAIN="FuPluginTest"']

install_dummy = false
if get_option('plugin_dummy')
  install_dummy = true
endif

shared_module('fu_plugin_test',
  fu_hash,
  sources : [
    'fu-plugin-test.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : install_dummy,
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

shared_module('fu_plugin_invalid',
  sources : [
    'fu-plugin-invalid.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : install_dummy,
  install_dir: plugin_dir,
  link_with : [
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
