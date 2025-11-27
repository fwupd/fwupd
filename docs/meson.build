plugin_readme_targets = []
plugin_readme_outputs = []
foreach plugin, enabled : plugins
  plugin_readme_output = '@0@-README.md'.format(plugin)
  plugin_readme_targets += custom_target(
    'doc-plugin-@0@'.format(plugin),
    input: join_paths(meson.project_source_root(), 'plugins', plugin, 'README.md'),
    output: plugin_readme_output,
    command: ['ln', '-sf', '@INPUT0@', '@OUTPUT@'],
    build_by_default: true,
  )
  plugin_readme_outputs += '"@0@"'.format(plugin_readme_output)
endforeach


if build_standalone
  if get_option('man')
    custom_target(
      'fwupd.conf.5',
      input: 'fwupd.conf.md',
      output: 'fwupd.conf.5',
      command: [
        generate_man,
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '--replace',
        'PACKAGE_VERSION',
        fwupd_version,
        '--replace',
        'SYSCONFDIR',
        sysconfdir,
        '--replace',
        'plugin_uefi_capsule',
        '@0@'.format(allow_uefi),
        '--replace',
        'plugin_msr',
        '@0@'.format(has_cpuid),
        '--replace',
        'plugin_redfish',
        '@0@'.format(host_machine.system() == 'linux'),
        '--replace',
        'P2pPolicy',
        '@0@'.format(get_option('p2p_policy')),
        '--defines',
        join_paths(meson.project_source_root(), 'plugins', 'msr', 'fu-msr-plugin.c'),
        '--defines',
        join_paths(meson.project_source_root(), 'plugins', 'redfish', 'fu-redfish-plugin.c'),
        '--defines',
        join_paths(meson.project_source_root(), 'plugins', 'thunderbolt', 'fu-thunderbolt-plugin.c'),
        '--defines',
        join_paths(
          meson.project_source_root(),
          'plugins',
          'uefi-capsule',
          'fu-uefi-capsule-plugin.c',
        ),
        '--defines',
        join_paths(meson.project_source_root(), 'src', 'fu-engine-config.c'),
      ],
      install: true,
      install_tag: 'man',
      install_dir: join_paths(mandir, 'man5'),
    )
    custom_target(
      'fwupd-remotes.d.5',
      input: 'fwupd-remotes.d.md',
      output: 'fwupd-remotes.d.5',
      command: [
        generate_man,
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '--replace',
        'PACKAGE_VERSION',
        fwupd_version,
        '--replace',
        'SYSCONFDIR',
        sysconfdir,
        '--replace',
        'LOCALSTATEDIR',
        localstatedir,
        '--defines',
        join_paths(meson.project_source_root(), 'src', 'fu-remote.c'),
      ],
      install: true,
      install_tag: 'man',
      install_dir: join_paths(mandir, 'man5'),
    )
  endif
  if build_docs
    md_targets += custom_target(
      'fwupd.conf.md',
      input: 'fwupd.conf.md',
      output: 'fwupd.conf.md',
      command: [
        generate_man,
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '--replace',
        'PACKAGE_VERSION',
        fwupd_version,
        '--replace',
        'SYSCONFDIR',
        sysconfdir,
        '--replace',
        'plugin_uefi_capsule',
        '@0@'.format(allow_uefi),
        '--replace',
        'plugin_msr',
        '@0@'.format(has_cpuid),
        '--replace',
        'plugin_redfish',
        '@0@'.format(host_machine.system() == 'linux'),
        '--defines',
        join_paths(meson.project_source_root(), 'plugins', 'msr', 'fu-msr-plugin.c'),
        '--defines',
        join_paths(meson.project_source_root(), 'plugins', 'redfish', 'fu-redfish-plugin.c'),
        '--defines',
        join_paths(meson.project_source_root(), 'plugins', 'thunderbolt', 'fu-thunderbolt-plugin.c'),
        '--defines',
        join_paths(
          meson.project_source_root(),
          'plugins',
          'uefi-capsule',
          'fu-uefi-capsule-plugin.c',
        ),
        '--defines',
        join_paths(meson.project_source_root(), 'src', 'fu-engine-config.c'),
        '--md',
      ],
    )
    md_targets += custom_target(
      'fwupd-remotes.d.md',
      input: 'fwupd-remotes.d.md',
      output: 'fwupd-remotes.d.md',
      command: [
        generate_man,
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '--replace',
        'PACKAGE_VERSION',
        fwupd_version,
        '--replace',
        'SYSCONFDIR',
        sysconfdir,
        '--replace',
        'LOCALSTATEDIR',
        localstatedir,
        '--defines',
        join_paths(meson.project_source_root(), 'libfwupd', 'fwupd-remote.c'),
        '--md',
      ],
    )
    man_md += ['"fwupd.conf.md"', '"fwupd-remotes.d.md"']
  endif
endif

if build_docs
  toml_conf = configuration_data()
  docgen_version = meson.project_version()
  toml_conf.set('version', docgen_version)
  toml_conf.set('plugin_readme_outputs', ','.join(plugin_readme_outputs))
  toml_conf.set('man_md', ','.join(man_md))

  fwupd_toml = configure_file(
    input: 'fwupd.toml.in',
    output: 'fwupd.toml',
    configuration: toml_conf,
  )

  fwupdplugin_toml = configure_file(
    input: 'fwupdplugin.toml.in',
    output: 'fwupdplugin.toml',
    configuration: toml_conf,
  )

  custom_target(
    'doc-fwupd',
    input: [fwupd_toml, fwupd_gir[0]],
    output: 'libfwupd',
    command: [
      gidocgen_app,
      'generate',
      '--quiet',
      '--add-include-path=@0@'.format(meson.current_build_dir() / '../libfwupd'),
      '--config=@INPUT0@',
      '--output-dir=@OUTPUT@',
      '--no-namespace-dir',
      '--content-dir=@0@'.format(meson.current_source_dir()),
      '@INPUT1@',
    ],
    depends: [fwupd_gir[0]],
    build_by_default: true,
    install: true,
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'doc'),
  )

  subdir('hsi-tests.d')
  hsi_md = custom_target(
    'generate-hsi-spec',
    input: hsi_test_jsons,
    output: 'hsi.md',
    command: [python3, files('generate-hsi-spec.py', 'hsi.md.in'), '@OUTPUT@', '@INPUT@'],
  )
  custom_target(
    'generate-oval',
    input: hsi_test_jsons,
    output: 'oval.xml',
    command: [
      python3,
      files('generate-oval.py'),
      '--version',
      fwupd_version,
      '--schema-version',
      '5.11.3',
      '@OUTPUT@',
      '@INPUT@',
    ],
  )

  custom_target(
    'doc-fwupdplugin',
    input: [fwupdplugin_toml, fwupdplugin_gir[0]],
    output: 'libfwupdplugin',
    command: [
      gidocgen_app,
      'generate',
      '--quiet',
      '--add-include-path=@0@'.format(meson.current_build_dir() / '../libfwupd'),
      '--add-include-path=@0@'.format(meson.current_build_dir() / '../libfwupdplugin'),
      '--config=@INPUT0@',
      '--output-dir=@OUTPUT@',
      '--no-namespace-dir',
      '--content-dir=@0@'.format(meson.current_build_dir()),
      '--content-dir=@0@'.format(meson.current_source_dir()),
      '--content-dir=@0@'.format(meson.current_build_dir() / '../src'),
      '--content-dir=@0@'.format(meson.current_build_dir() / '../plugins/uefi-dbx'),
      '--content-dir=@0@'.format(meson.current_build_dir() / '../data/motd'),
      '--content-dir=@0@'.format(meson.current_build_dir() / '../plugins/uefi-capsule'),
      '@INPUT1@',
    ],
    depends: [fwupdplugin_gir[0], hsi_md, plugin_readme_targets, md_targets],
    build_by_default: true,
    install: true,
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'doc'),
  )
  man_cmd = []
  foreach man : man_md
    man_cmd += '-m @0@'.format(man)
  endforeach
  custom_target(
    'index.html',
    input: 'index.html',
    output: 'index.html',
    command: [generate_index, '@INPUT@', '-o', '@OUTPUT@', man_cmd],
    install: true,
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'doc', 'fwupd'),
  )
  if hsi
    install_data(
      ['hsi.html'],
      install_tag: 'doc',
      install_dir: join_paths(datadir, 'doc', 'fwupd'),
    )
  endif
  install_data(
    ['urlmap_fwupd.js'],
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'doc', 'libfwupd'),
  )
  install_data(
    ['urlmap_fwupdplugin.js'],
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'doc', 'libfwupdplugin'),
  )
  #make devhelp work
  install_symlink(
    'libfwupd',
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'doc', 'fwupd'),
    pointing_to: join_paths('..', 'libfwupd'),
  )
  install_symlink(
    'libfwupdplugin',
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'doc', 'fwupd'),
    pointing_to: join_paths('..', 'libfwupdplugin'),
  )
endif
