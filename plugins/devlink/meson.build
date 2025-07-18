if host_machine.system() == 'linux'
  cargs = ['-DG_LOG_DOMAIN="FuPluginDevlink"']
  plugins += {meson.current_source_dir().split('/')[-1]: true}

  # Find libmnl dependency
  libmnl = dependency('libmnl', required: false)
  if not libmnl.found()
    libmnl = cc.find_library('mnl', required: false)
  endif
  if not libmnl.found()
    warning('libmnl not found, devlink plugin will be disabled')
    subdir_done()
  endif

  plugin_builtin_devlink = static_library('fu_plugin_devlink',
    sources: [
      'fu-devlink-plugin.c',
      'fu-devlink-backend.c',
      'fu-devlink-device.c',
      'fu-devlink-component.c',
      'fu-devlink-netlink.c',
    ],
    include_directories: plugin_incdirs,
    link_with: [
      fwupdplugin,
      fwupd,
    ],
    c_args: cargs,
    dependencies: [
      plugin_deps,
      valgrind,
      libmnl,
    ],
  )
  plugin_builtins += plugin_builtin_devlink

  if get_option('tests')
    env = environment()
    env.set('G_TEST_SRCDIR', meson.current_source_dir())
    env.set('G_TEST_BUILDDIR', meson.current_build_dir())
    e = executable(
      'devlink-self-test',
      sources: [
        'fu-self-test.c',
      ],
      include_directories: plugin_incdirs,
      dependencies: [
        plugin_deps,
        valgrind,
        libmnl,
      ],
      link_with: [
        plugin_builtin_devlink,
        fwupdplugin,
        fwupd,
      ],
      install: true,
      install_rpath: libdir_pkg,
      install_dir: installed_test_bindir,
    )
    test('devlink-self-test', e, env: env)
  endif
endif
