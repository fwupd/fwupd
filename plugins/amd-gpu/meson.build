if host_machine.system() == 'linux' and libdrm_amdgpu.found()
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
enumeration_data += files('tests/amd-apu-rembrandt-setup.json',
                          'tests/amd-apu-strix-setup.json',
                          'tests/amd-dgpu-setup.json')
device_tests += files('tests/amd-apu-rembrandt.json',
                      'tests/amd-apu-strix.json',
                      'tests/amd-dgpu.json',
                      'tests/amd-dgpu-navi3x.json',
)
endif
