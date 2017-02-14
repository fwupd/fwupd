man_MANS_DIST = 					\
	dfu-tool.1					\
	fwupdmgr.1

EXTRA_DIST =						\
	dfu-tool.sgml					\
	fwupdmgr.sgml					\
	$(man_MANS_DIST)

man_MANS =						\
	$(man_MANS_DIST)

fwupdmgr.1: fwupdmgr.sgml
	$(AM_V_GEN)					\
	$(DOCBOOK2MAN) $? > /dev/null
dfu-tool.1: dfu-tool.sgml
	$(AM_V_GEN)					\
	$(DOCBOOK2MAN) $? > /dev/null

MAINTAINERCLEANFILES =					\
	manpage.links					\
	manpage.log					\
	manpage.refs					\
	$(man_MANS)

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

-include $(top_srcdir)/git.mk
