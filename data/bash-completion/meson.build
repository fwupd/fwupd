if bashcomp.found()
  completions_dir = bashcomp.get_variable(pkgconfig : 'completionsdir',
    pkgconfig_define: bashcomp.version().version_compare('>= 2.10') ? ['datadir', datadir] : ['prefix', prefix],
  )

  install_data(['fwupdtool'],
    install_dir : completions_dir,
  )

  if build_daemon
    install_data(['fwupdagent', 'fwupdmgr'],
      install_dir : completions_dir,
    )
  endif # build_daemon

endif # bashcomp.found()
