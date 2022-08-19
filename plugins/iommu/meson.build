if hsi and (host_cpu == 'x86' or host_cpu == 'x86_64')
cargs = ['-DG_LOG_DOMAIN="FuPluginIommu"']

plugin_quirks += join_paths(meson.current_source_dir(), 'iommu.quirk')

shared_module('fu_plugin_iommu',
  fu_hash,
  sources: [
    'fu-plugin-iommu.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupdplugin,
    fwupd,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)
endif
