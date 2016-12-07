install_data('org.freedesktop.fwupd.rules',
             install_dir : 'share/polkit-1/rules.d')

i18n.merge_file(
  input: 'org.freedesktop.fwupd.policy.in',
  output: 'org.freedesktop.fwupd.policy',
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions')
)
