host_machine.system() == 'linux' or subdir_done()

subdir('tests')

cargs = ['-DG_LOG_DOMAIN="FuPluginAcpiSbom"']
plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_builtin_acpi_sbom = static_library('fu_plugin_acpi_sbom',
  rustgen.process('fu-acpi-sbom.rs'),
  sources: [
    'fu-acpi-sbom-entry.c',
    'fu-acpi-sbom-plugin.c',
    'fu-acpi-sbom-table.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_acpi_sbom

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'acpi-sbom-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_acpi_sbom,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('acpi-sbom-self-test', e, env: env)
endif
