install_data(
  ['fwupdmgr.fish'],
  install_tag: 'runtime',
  install_dir: join_paths(datadir, 'fish', 'vendor_completions.d'),
)
if tui.allowed()
  install_data(
    'fwupdtui.fish',
    install_tag: 'runtime',
    install_dir: join_paths(datadir, 'fish', 'vendor_completions.d'),
  )
endif
