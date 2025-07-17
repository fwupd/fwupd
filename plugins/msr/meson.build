hsi or subdir_done()
has_cpuid or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginMsr"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

if libsystemd.found()
install_data(['fwupd-msr.conf'],
  install_dir: systemd_modules_load_dir,
)
endif

plugin_builtins += static_library('fu_plugin_msr',
  sources: [
    'fu-msr-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
