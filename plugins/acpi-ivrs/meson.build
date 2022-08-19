if hsi and (host_cpu == 'x86' or host_cpu == 'x86_64')
cargs = ['-DG_LOG_DOMAIN="FuPluginAcpiIvrs"']

shared_module('fu_plugin_acpi_ivrs',
  fu_hash,
  sources: [
    'fu-plugin-acpi-ivrs.c',
    'fu-acpi-ivrs.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'acpi-ivrs-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
      'fu-acpi-ivrs.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    install: true,
    install_dir: installed_test_bindir,
  )
  test('acpi-ivrs-self-test', e, env: env)  # added to installed-tests
endif
endif
