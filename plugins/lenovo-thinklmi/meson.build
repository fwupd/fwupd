if get_option('plugin_uefi_capsule').disable_auto_if(host_machine.system() != 'linux').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoThinkLmi"']

shared_module('fu_plugin_lenovo_thinklmi',
  fu_hash,
  sources: [
    'fu-plugin-lenovo-thinklmi.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: [
    cargs,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'lenovo-thinklmi-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
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
    c_args: [
      cargs,
    ],
  )
  test('lenovo-thinklmi-self-test', e, env: env)
endif
endif
