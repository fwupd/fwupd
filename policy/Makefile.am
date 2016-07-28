
polkit_rulesdir = $(datadir)/polkit-1/rules.d
dist_polkit_rules_DATA =				\
	org.freedesktop.fwupd.rules

polkit_policydir = $(datadir)/polkit-1/actions
polkit_policy_DATA = org.freedesktop.fwupd.policy
org.freedesktop.fwupd.policy: org.freedesktop.fwupd.policy.in
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

EXTRA_DIST = org.freedesktop.fwupd.policy.in
CLEANFILES =						\
	org.freedesktop.fwupd.policy

-include $(top_srcdir)/git.mk
