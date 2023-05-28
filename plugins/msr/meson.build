if hsi and has_cpuid
  cargs = ['-DG_LOG_DOMAIN="FuPluginMsr"']

  plugin_quirks += files('msr.quirk')

  if libsystemd.found()
    install_data(
      ['fwupd-msr.conf'],
      install_dir: systemd_modules_load_dir,
    )
  endif

  plugin_builtins += static_library(
    'fu_plugin_msr',
    sources: [
      'fu-msr-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif
