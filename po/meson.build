i18n.gettext(meson.project_name(),
  preset : 'glib',
  args: [
  '--default-domain=' + meson.project_name(),
  ]
)

run_target('fix-translations',
	   command: [
	     join_paths(meson.project_source_root(), 'contrib/fix_translations.py'),
	     join_paths(meson.project_source_root(), 'po')
	   ])
