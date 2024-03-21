if bashcomp.found()
  completions_dir = bashcomp.get_variable(pkgconfig: 'completionsdir',
    pkgconfig_define: bashcomp.version().version_compare('>= 2.10') ? ['datadir', datadir] : ['prefix', prefix],
  )

  con = configuration_data()
  con.set('localstatedir', localstatedir)
  if offline
    con.set_quoted('offline', '--offline')
  endif

  configure_file(
    input: 'fwupdtool',
    output: 'fwupdtool',
    configuration: con,
    install: true,
    install_dir: completions_dir,
  )

  if build_daemon
    configure_file(
      input: 'fwupdmgr',
      output: 'fwupdmgr',
      configuration: con,
      install: true,
      install_dir: completions_dir,
    )
  endif # build_daemon

endif # bashcomp.found()
