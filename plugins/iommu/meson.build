if not hsi
  subdir_done()
endif
if host_cpu not in ['x86', 'x86_64']
  subdir_done()
endif

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
