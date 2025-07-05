
linux_plugins = {
  'acpi-dmar': false,
  'acpi-facp': false,
  'acpi-ivrs': false,
  'acpi-phat': false,
  'amd-kria': false,
  'amd-pmc': false,
  'amd-gpu': false,
  'android-boot': false,
  'asus-hid': false,
  'ata': false,
  'bios': false,
  'bcm57xx': false,
  'bnr-dp': false,
  'elantp': false,
  'emmc': false,
  'ep963x': false,
  'focalfp': false,
  'genesys-gl32xx': false,
  'intel-amt': false,
  'intel-gsc': false,
  'intel-mchi': false,
  'intel-mkhi': false,
  'legion-hid2': false,
  'lenovo-thinklmi': false,
  'linux-display': false,
  'linux-lockdown': false,
  'linux-sleep': false,
  'linux-swap': false,
  'linux-tainted': false,
  'logind': false,
  'logitech-hidpp': false,
  'logitech-rallysystem': false,
  'logitech-scribe': false,
  'logitech-tap': false,
  'kinetic-dp': false,
  'mediatek-scaler': false,
  'modem-manager': false,
  'msr': false,
  'mtd': false,
  'nordic-hid': false,
  'parade-lspcon': false,
  'pci-psp': false,
  'powerd': false,
  'realtek-mst': false,
  'redfish': false,
  'scsi': false,
  'synaptics-mst': false,
  'thelio-io': false,
  'thunderbolt': false,
  'uf2': false,
  'upower': false,
  'wacom-raw': false,
}

plugins = plugins + linux_plugins

if host_machine.system() != 'linux'
  subdir_done()
endif

foreach plugin, enabled: linux_plugins
  subdir(plugin)
  plugin_readme_output = '@0@-README.md'.format(plugin)
  plugin_readme_targets += custom_target('doc-plugin-@0@'.format(plugin),
    input: join_paths(meson.project_source_root(), 'plugins', 'linux', plugin, 'README.md'),
    output: plugin_readme_output,
    command: ['ln', '-sf', '@INPUT0@', '@OUTPUT@'],
    build_by_default: true,
  )
  plugin_readme_outputs += '"@0@"'.format(plugin_readme_output)
endforeach
