jcat_tool = find_program(
  'jcat-tool',
  required: false,
)

if jcat_tool.found()
  firmware_xml_gz_jcat = custom_target(
    'firmware-xml-gz-jcat',
    input: ['firmware.xml.gz'],
    output: 'firmware.xml.gz.jcat',
    command: [
      jcat_tool,
      '--basename',
      '--appstream-id',
      'localhost',
      'self-sign',
      '@OUTPUT@',
      '@INPUT@',
    ],
  )
else
  firmware_xml_gz_jcat = custom_target(
    'firmware-xml-gz-jcat',
    input: ['firmware.xml.gz'],
    output: 'firmware.xml.gz.jcat',
    command: ['touch', '@OUTPUT@'],
  )
endif
