if get_option('plugin_acpi_phat').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginAcpiPhat"']

plugin_builtin_acpi_phat = static_library('fu_plugin_acpi_phat',
  sources: [
    'fu-acpi-phat-plugin.c',
    'fu-acpi-phat.c',                 # fuzzing
    'fu-acpi-phat-health-record.c',   # fuzzing
    'fu-acpi-phat-version-element.c', # fuzzing
    'fu-acpi-phat-version-record.c',  # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_acpi_phat

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'acpi-phat-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_acpi_phat,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('acpi-phat-self-test', e, env: env)  # added to installed-tests
endif
endif
