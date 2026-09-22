host_machine.system() == 'linux' or subdir_done()
libdrm_amdgpu.found() or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginAmdGpu"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('amd-gpu.quirk')
plugin_rustgen_output = rustgen.process(
  'fu-amd-gpu-atom.rs', # fuzzing
  'fu-amd-gpu-psp.rs', # fuzzing
  'fu-amd-gpu-pldm.rs', # fuzzing
)
plugin_builtin_amd_gpu = static_library(
  'fu_plugin_amd_gpu',
  plugin_rustgen_output,
  sources: [
    'fu-amd-gpu-plugin.c',
    'fu-amd-gpu-device.c',
    'fu-amd-gpu-psp-firmware.c',
    'fu-amd-gpu-psp-ish-firmware.c',
    'fu-amd-gpu-psp-l2-firmware.c',
    'fu-amd-gpu-atom-firmware.c',
    'fu-amd-gpu-pldm-firmware.c',
    'fu-amd-gpu-pldm-device.c',
    'fu-amd-gpu-uma.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_amd_gpu
enumeration_data += files(
  'tests/amd-apu-rembrandt-setup.json',
  'tests/amd-apu-strix-setup.json',
  'tests/amd-dgpu-setup.json',
)
device_tests += files(
  'tests/amd-apu-rembrandt.json',
  'tests/amd-apu-strix.json',
  'tests/amd-dgpu.json',
  'tests/amd-dgpu-navi3x.json',
)

if get_option('tests')
  e = executable(
    'fu-amd-gpu-uma-test',
    sources: 'fu-amd-gpu-uma-test.c',
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [plugin_libs, plugin_builtin_amd_gpu],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    install_tag: 'tests',
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test(
    'amd-gpu-uma-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
  test(
    'amd-gpu-pldm-self-test',
    executable(
      'fu-amd-gpu-pldm-self-test',
      plugin_rustgen_output,
      sources: 'fu-amd-gpu-pldm-self-test.c',
      include_directories: plugin_incdirs,
      dependencies: plugin_deps,
      link_with: [plugin_libs, plugin_builtin_amd_gpu],
      install: true,
      install_rpath: libdir_pkg,
      install_dir: installed_test_bindir,
      install_tag: 'tests',
    ),
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
