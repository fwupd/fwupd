.PHONY: all clean

SUBDIRS := $(shell find . -maxdepth 1 -type d ! -name ".")

all:
	@set -e; \
	for dir in $(SUBDIRS); do \
		if [ -f $$dir/Makefile ] || [ -f $$dir/makefile ] || [ -f $$dir/GNUmakefile ]; then \
			echo "===> Entering $$dir"; \
			$(MAKE) -C $$dir; \
		fi; \
	done

clean:
	@set -e; \
	for dir in $(SUBDIRS); do \
		if [ -f $$dir/Makefile ] || [ -f $$dir/makefile ] || [ -f $$dir/GNUmakefile ]; then \
			echo "===> Cleaning $$dir"; \
			$(MAKE) -C $$dir clean; \
		fi; \
	done
