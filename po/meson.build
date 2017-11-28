i18n.gettext(meson.project_name(),
  preset : 'glib',
  args: [
  '--default-domain=' + meson.project_name(),
  ]
)

if get_option('plugin_uefi_labels')
meson.add_install_script('make-images.sh')
endif
