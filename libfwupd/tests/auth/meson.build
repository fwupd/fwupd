jcat_tool = find_program('jcat-tool', required: false)

if jcat_tool.found()
metadata_xml_gz_jcat = custom_target('metadata-xml-gz-jcat',
  input: [
    'metadata.xml.gz',
  ],
  output: 'metadata.xml.gz.jcat',
  command: [
    jcat_tool, '--basename', '--appstream-id', 'localhost', 'self-sign', '@OUTPUT@', '@INPUT@',
  ],
)
else
metadata_xml_gz_jcat = custom_target('metadata-xml-gz-jcat',
  input: [
    'metadata.xml.gz',
  ],
  output: 'metadata.xml.gz.jcat',
  command: [
    'touch', '@OUTPUT@',
  ],
)
endif
