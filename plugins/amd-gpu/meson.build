if gudev.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginAmdGpu"']

  plugin_quirks += files('amd-gpu.quirk')
  plugin_builtins += static_library(
    'fu_plugin_amd_gpu',
    sources: [
      'fu-amd-gpu-plugin.c',
      'fu-amd-gpu-device.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif
