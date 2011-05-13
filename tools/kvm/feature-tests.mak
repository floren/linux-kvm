# This is pulled from tools/perf/feature-tests.mak in the Linux kernel source tree

define SOURCE_VNC
#include <rfb/rfb.h>
int main(int argc, char** argv)
{
	return rfbGetScreen(&argc, argv, 10, 10, 8, 3, 4);
}
endef


# try-cc
# Usage: option = $(call try-cc, source-to-build, cc-options)
try-cc = $(shell sh -c						  \
	'TMP="$(OUTPUT)$(TMPOUT).$$$$";				  \
	 echo "$(1)" |						  \
	 $(CC) -x c - $(2) -o "$$TMP" > /dev/null 2>&1 && echo y; \
	 rm -f "$$TMP"')
