hsi or subdir_done()
host_cpu in ['x86', 'x86_64'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginIommu"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_iommu',
  sources: [
    'fu-iommu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: [
    fwupdplugin,
    fwupd,
  ],
  c_args: cargs,
  dependencies: plugin_deps,
)
