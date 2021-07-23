if bashcomp.found()
  completions_dir = bashcomp.get_pkgconfig_variable('completionsdir',
    define_variable: bashcomp.version().version_compare('>= 2.10') ? ['datadir', datadir] : ['prefix', prefix],
  )

# replace @libexecdir@
fwupdtool_path = join_paths(libexecdir, 'fwupd')
con2 = configuration_data()
con2.set('libexecdir', fwupdtool_path)
configure_file(
  input : 'fwupdtool.in',
  output : 'fwupdtool',
  configuration : con2,
  install: true,
  install_dir: completions_dir)

if build_daemon
install_data(['fwupdagent'],
  install_dir : completions_dir,
)
configure_file(
  input : 'fwupdmgr.in',
  output : 'fwupdmgr',
  configuration : con2,
  install: true,
  install_dir: completions_dir)
endif # build_daemon

endif # bashcomp.found()
