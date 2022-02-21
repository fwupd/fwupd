if get_option('docs') == 'docgen'
  toml_conf = configuration_data()
  docgen_version = source_version
  if git.found() and source_version != fwupd_version
    docgen_version = run_command(git, 'branch', '--show-current').stdout().strip()
  endif
  toml_conf.set('version', docgen_version)

  fwupd_toml = configure_file(
    input: 'fwupd.toml.in',
    output: 'fwupd.toml',
    configuration: toml_conf
  )

  fwupdplugin_toml = configure_file(
    input: 'fwupdplugin.toml.in',
    output: 'fwupdplugin.toml',
    configuration: toml_conf
  )

  custom_target('doc-fwupd',
    input: [
      fwupd_toml,
      fwupd_gir[0],
    ],
    output: 'libfwupd',
    command: [
      gidocgen,
      'generate',
      '--quiet',
      '--add-include-path=@0@'.format(meson.current_build_dir() / '../libfwupd'),
      '--config=@INPUT0@',
      '--output-dir=@OUTPUT@',
      '--no-namespace-dir',
      '--content-dir=@0@'.format(meson.current_source_dir()),
      '@INPUT1@',
    ],
    depends: [
      fwupd_gir[0],
    ],
    build_by_default: true,
    install: true,
    install_dir: join_paths(datadir, 'doc', 'fwupd'),
  )

  custom_target('doc-fwupdplugin',
    input: [
      fwupdplugin_toml,
      fwupdplugin_gir[0],
    ],
    output: 'libfwupdplugin',
    command: [
      gidocgen,
      'generate',
      '--quiet',
      '--add-include-path=@0@'.format(meson.current_build_dir() / '../libfwupd'),
      '--add-include-path=@0@'.format(meson.current_build_dir() / '../libfwupdplugin'),
      '--config=@INPUT0@',
      '--output-dir=@OUTPUT@',
      '--no-namespace-dir',
      '--content-dir=@0@'.format(meson.current_source_dir()),
      '@INPUT1@',
    ],
    depends: [
      fwupdplugin_gir[0],
    ],
    build_by_default: true,
    install: true,
    install_dir: join_paths(datadir, 'doc', 'fwupd'),
  )

 install_data(['index.html', 'hsi.html'],
    install_dir : join_paths(datadir, 'doc', 'fwupd')
  )
 install_data(['urlmap_fwupd.js'],
    install_dir : join_paths(datadir, 'doc', 'fwupd', 'libfwupd')
  )
 install_data(['urlmap_fwupdplugin.js'],
    install_dir : join_paths(datadir, 'doc', 'fwupd', 'libfwupdplugin')
  )

elif get_option('docs') == 'gtkdoc'
  gnome.gtkdoc(
    'fwupd',
    src_dir : [
      join_paths(meson.project_source_root(), 'libfwupd'),
      join_paths(meson.project_source_root(), 'libfwupdplugin'),
      join_paths(meson.build_root(), 'libfwupd'),
      join_paths(meson.build_root(), 'libfwupdplugin'),
      join_paths(meson.current_source_dir()),
    ],
    main_xml : 'fwupd-docs.xml',
    install : true
  )
endif
