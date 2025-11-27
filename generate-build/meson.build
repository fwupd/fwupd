generate_metainfo = [python3, files('generate-metainfo.py')]
generate_version_script = [python3, files('generate-version-script.py')]
generate_plugins_header = [python3, files('generate-plugins-header.py')]
generate_quirk_builtin = [python3, files('generate-quirk-builtin.py')]
generate_dbus_interface = [python3, files('generate-dbus-interface.py')]
generate_man = [python3, files('generate-man.py')]
generate_index = [python3, files('generate-index.py')]
fix_translations = [python3, files('fix_translations.py')]
if umockdev_integration_tests.allowed()
  unittest_inspector = [python3, files('unittest_inspector.py')]
  install_data(
    files('unittest_inspector.py'),
    install_dir: installed_test_datadir,
  )
endif
