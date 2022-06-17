if bashcomp.found()
  completions_dir = bashcomp.get_variable(pkgconfig: 'completionsdir',
    pkgconfig_define: bashcomp.version().version_compare('>= 2.10') ? ['datadir', datadir] : ['prefix', prefix],
  )

  con = configuration_data()
  con.set('localstatedir', localstatedir)

  configure_file(
    input: 'fwupdtool',
    output: 'fwupdtool',
    configuration: con,
    install: true,
    install_dir: completions_dir,
  )

  if build_daemon
    install_data(['fwupdagent', 'fwupdmgr'],
      install_dir: completions_dir,
    )
  endif # build_daemon

endif # bashcomp.found()
