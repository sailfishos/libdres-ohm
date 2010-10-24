EXTRA_DIST = autogen.sh build-aux/git-version-gen
SUBDIRS    = src include ohm-plugin tests doc

pkgconfigdir   = ${libdir}/pkgconfig
pkgconfig_DATA = src/libdres.pc

MAINTAINTERCLEANFILES = Makefile.in

# see build-aux/git-version-gen
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

clean-local:
	find . -name \*~ -exec rm -f {} \;

