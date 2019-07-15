i18n.gettext(meson.project_name(),
  preset : 'glib',
  args: [
  '--default-domain=' + meson.project_name(),
  ]
)

if get_option('plugin_uefi')
meson.add_install_script('make-images.sh', localedir, python3.path())
endif

run_target('fix-translations',
	   command: [
	     join_paths(meson.source_root(), 'contrib/fix_translations.py'),
	     join_paths(meson.source_root(), 'po')
	   ])
