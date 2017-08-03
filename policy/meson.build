install_data('org.freedesktop.fwupd.rules',
             install_dir : 'share/polkit-1/rules.d')

#meson 0.41.0 added support for data_dirs argument
if meson.version().version_compare('>=0.41.0')
  #newer polkit has the ITS rules included
  if polkit.version().version_compare('>0.113')
    i18n.merge_file(
      input: 'org.freedesktop.fwupd.policy.in',
      output: 'org.freedesktop.fwupd.policy',
      install: true,
      install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions') ,
      type: 'xml',
      po_dir: join_paths(meson.source_root(), 'po')
    )
  #older polkit is missing ITS rules and will fail
  else
    i18n.merge_file(
      input: 'org.freedesktop.fwupd.policy.in',
      output: 'org.freedesktop.fwupd.policy',
      install: true,
      install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions') ,
      type: 'xml',
      data_dirs: join_paths(meson.source_root(), 'policy'),
      po_dir: join_paths(meson.source_root(), 'po')
    )
  endif
#older polkit and older meson, need to do some custom targets
#see https://github.com/hughsie/fwupd/issues/107
else
  envbin = find_program('env')
  gettext_data_dir = 'GETTEXTDATADIRS=' + join_paths(meson.source_root(), 'policy')
  custom_target('org.freedesktop.fwupd.policy',
    input: 'org.freedesktop.fwupd.policy.in',
    output: 'org.freedesktop.fwupd.policy',
    command: [envbin, gettext_data_dir, 'msgfmt', '--xml',
              '--template', '@INPUT@',
              '-d', join_paths(meson.source_root(), 'po'),
              '-o', '@OUTPUT@'],
    install: true,
    install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions')
   )
endif
