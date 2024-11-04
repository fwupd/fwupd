if enable_udev and libdrm_amdgpu.found() and get_option('plugin_amdgpu').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginAmdGpu"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('amd-gpu.quirk')
plugin_builtins += static_library('fu_plugin_amd_gpu',
  rustgen.process(
    'fu-amd-gpu-atom.rs',          # fuzzing
    'fu-amd-gpu-psp.rs',           # fuzzing
  ),
  sources: [
    'fu-amd-gpu-plugin.c',
    'fu-amd-gpu-device.c',
    'fu-amd-gpu-psp-firmware.c',
    'fu-amd-gpu-atom-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
enumeration_data += files('tests/amd-apu-setup.json', 'tests/amd-dgpu-setup.json')
device_tests += files('tests/amd-apu.json', 'tests/amd-dgpu.json')
endif
