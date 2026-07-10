.PHONY: curl_exe_src curl_lib_src

define rewrite_rule
$(eval include $2)
$(eval old := $(shell meson rewrite target $1 info 2>&1 | grep -v '^Unable to evaluate subdir' | jq -r '.target[].sources | join(" ")'))
$(eval new := $3)
meson rewrite target $1 add $(filter-out $(old),$(new))
meson rewrite target $1 rm $(filter-out $(new),$(old))
endef

curl_exe_src:
	$(call rewrite_rule,curl_exe,src/Makefile.inc,$$(filter-out tool_hugehelp.c %.h,$$(CURL_FILES)))

curl_lib_src:
	$(call rewrite_rule,curl_lib,lib/Makefile.inc,$$(filter-out %.h,$$(CSOURCES)))
