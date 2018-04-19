install_data('org.freedesktop.fwupd.rules',
             install_dir : join_paths(datadir, 'polkit-1', 'rules.d'))

#newer polkit has the ITS rules included
if polkit.version().version_compare('>0.113')
  i18n.merge_file(
    input: 'org.freedesktop.fwupd.policy.in',
    output: 'org.freedesktop.fwupd.policy',
    install: true,
    install_dir: join_paths(datadir, 'polkit-1', 'actions') ,
    type: 'xml',
    po_dir: join_paths(meson.source_root(), 'po')
  )
#older polkit is missing ITS rules and will fail
else
  i18n.merge_file(
    input: 'org.freedesktop.fwupd.policy.in',
    output: 'org.freedesktop.fwupd.policy',
    install: true,
    install_dir: join_paths(datadir, 'polkit-1', 'actions') ,
    type: 'xml',
    data_dirs: join_paths(meson.source_root(), 'policy'),
    po_dir: join_paths(meson.source_root(), 'po')
  )
endif
