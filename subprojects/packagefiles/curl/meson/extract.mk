.PHONY: tests/libtest tests/server tests/unit

define extract_noinst_progs
$$(foreach prog,$$(noinst_PROGRAMS),
   $$(info $$(prog)
      $$(filter-out %.h,
         $$(call nodist_$$(prog)_SOURCES)
         $$(call $$(prog)_SOURCES)
      )
      $$(call $$(prog)_CPPFLAGS) $$(call $$(prog)_LDADD)
))
endef
extract_noinst_progs := $(strip $(extract_noinst_progs))

define extract_rule
$(eval include $1/Makefile.inc)
@$(eval $2)$(eval $3)
endef

tests/libtest:
	$(call extract_rule,$@,TESTUTIL_LIBS = @TESTUTIL_LIBS@,$(extract_noinst_progs))

tests/server:
	$(call extract_rule,$@,,$(extract_noinst_progs))

tests/unit:
	$(call extract_rule,$@,,$$(foreach prog,$$(UNITPROGS),$$(info $$(prog) $$(prog).c $$(filter-out %.h,$$(UNITFILES)))))
