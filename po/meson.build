i18n.gettext(meson.project_name(),
  preset: 'glib',
  args: [
  '--default-domain=' + meson.project_name(),
  ]
)

run_target('fix-translations',
  command: [
    fix_translations,
    join_paths(meson.project_source_root(), 'po')
  ]
)
