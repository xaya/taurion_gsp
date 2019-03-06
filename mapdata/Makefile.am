noinst_LTLIBRARIES = libmapdata.la
noinst_PROGRAMS = procmap

COMPRESSED = obstacledata.dat.xz
UNCOMPRESSED = $(COMPRESSED:%.xz=%)
BLOBS = obstacles.bin

EXTRA_DIST = $(COMPRESSED)

BUILT_SOURCES = tiledata.cpp
CLEANFILES = $(UNCOMPRESSED) tiledata.cpp $(BLOBS)

libmapdata_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS)
libmapdata_la_LIBADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS)
libmapdata_la_SOURCES = \
  basemap.cpp \
  tiledata.cpp \
  \
  blobs.s
noinst_HEADERS = \
  basemap.hpp \
  tiledata.hpp \
  \
  dataio.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GTEST_MAIN_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libmapdata.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GTEST_MAIN_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  basemap_tests.cpp \
  \
  dataio.cpp

procmap_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
procmap_LDADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
procmap_SOURCES = procmap.cpp \
  dataio.cpp

$(UNCOMPRESSED): %: %.xz
	xz -dc $< >$@

tiledata.cpp $(BLOBS): obstacledata.dat procmap$(EXEEXT)
	$(builddir)/procmap$(EXEEKT) \
	  --obstacle_input=obstacledata.dat \
	  --code_output=tiledata.cpp \
	  --obstacle_output=obstacles.bin
	touch $(srcdir)/blobs.s
