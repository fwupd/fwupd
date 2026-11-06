host_machine.system() in ['linux', 'android'] or subdir_done()
# FuDellPsbDevice uses the SG_IO primitives, which are stubs without scsi/sg.h
cc.has_header('scsi/sg.h', required: false) or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginDellPsb"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
plugin_builtins += static_library(
  'fu_plugin_dell_psb',
  sources: [
    'fu-dell-psb-plugin.c',
    'fu-dell-psb-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
