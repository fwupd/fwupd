host_machine.system() == 'linux' or subdir_done()
host_cpu in ['x86', 'x86_64'] or subdir_done()
allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginAmdAfc"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

amd_afc_struct = rustgen.process('fu-amd-afc.rs')
amd_afc_sources = files(
  'fu-amd-afc.c',
  'fu-amd-afc-hii.c',
  'fu-amd-afc-hii-strings.c',
)

plugin_builtin_amd_afc = static_library(
  'fu_plugin_amd_afc',
  amd_afc_struct,
  amd_afc_sources,
  sources: ['fu-amd-afc-plugin.c'],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_amd_afc

if get_option('tests')
  e = executable(
    'amd-afc-self-test',
    rustgen.process('fu-amd-afc.rs'),
    sources: ['fu-self-test.c'],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_amd_afc,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test('amd-afc-self-test', e)

  if cc.has_argument('-fsanitize=fuzzer')
    amd_afc_fuzzer_lib = static_library(
      'fu_plugin_amd_afc_fuzzer',
      amd_afc_struct,
      amd_afc_sources,
      include_directories: plugin_incdirs,
      dependencies: plugin_deps,
      c_args: cargs + ['-fsanitize=fuzzer-no-link'],
    )
    executable(
      'amd-afc-fuzzer',
      sources: ['fu-fuzzer.c'],
      include_directories: plugin_incdirs,
      dependencies: plugin_deps + [
        cc.find_library(
          'm',
          required: true,
        ),
      ],
      link_with: [
        fwupd,
        fwupdplugin,
        amd_afc_fuzzer_lib,
      ],
      c_args: cargs + ['-fsanitize=fuzzer'],
      link_args: ['-fsanitize=fuzzer'],
    )
  endif
endif
