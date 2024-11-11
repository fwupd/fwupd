subdir('firmware_packager')
if get_option('qubes')
  subdir('qubes')
endif

con2 = configuration_data()
con2.set('FWUPD_VERSION', fwupd_version)

configure_file(
  input: 'fwupd.spec.in',
  output: 'fwupd.spec.in',
  configuration: con2,
)

uswid = find_program('uswid', required: false)
if uswid.found()
  custom_target('gen-sbom',
    input: 'sbom.cdx.json',
    output: 'sbom.cdx.json',
    command: [
      uswid,
      '--load', '@INPUT@',
      '--save', '@OUTPUT@',
    ],
  )
endif

if host_machine.system() == 'windows'
  configure_file(
    input: 'fwupd.wxs.in',
    output: 'fwupd.wxs',
    configuration: conf
  )
endif
