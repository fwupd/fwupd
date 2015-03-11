
polkit_rulesdir = $(datadir)/polkit-1/rules.d
dist_polkit_rules_DATA =				\
	org.freedesktop.fwupd.rules

@INTLTOOL_POLICY_RULE@
polkit_policydir = $(datadir)/polkit-1/actions
polkit_policy_in_files = org.freedesktop.fwupd.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)
#polkit_policy_DATA =					\
#	org.freedesktop.fwupd.policy

EXTRA_DIST = org.freedesktop.fwupd.policy.in
CLEANFILES =						\
	org.freedesktop.fwupd.policy

-include $(top_srcdir)/git.mk
