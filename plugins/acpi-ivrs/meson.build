if hsi and (host_cpu == 'x86' or host_cpu == 'x86_64')
cargs = ['-DG_LOG_DOMAIN="FuPluginAcpiIvrs"']

plugin_builtin_acpi_ivrs = static_library('fu_plugin_acpi_ivrs',
  sources: [
    'fu-acpi-ivrs-plugin.c',
    'fu-acpi-ivrs.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_acpi_ivrs

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'acpi-ivrs-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_acpi_ivrs,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('acpi-ivrs-self-test', e, env: env)  # added to installed-tests
endif
endif
