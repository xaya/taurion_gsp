ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -Im4

SUBDIRS = hexagonal proto database mapdata src gametest

build-tests:
	$(MAKE) -C hexagonal build-tests
	$(MAKE) -C proto build-tests
	$(MAKE) -C database build-tests
	$(MAKE) -C mapdata build-tests
	$(MAKE) -C src build-tests
