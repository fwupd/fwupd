man_MANS_DIST = 					\
	fwupdmgr.1

EXTRA_DIST =						\
	fwupdmgr.sgml					\
	$(man_MANS_DIST)

man_MANS =						\
	$(man_MANS_DIST)

fwupdmgr.1: fwupdmgr.sgml
	$(AM_V_GEN)					\
	docbook2man $? > /dev/null

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
