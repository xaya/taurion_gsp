noinst_LTLIBRARIES = libmapdata.la
noinst_PROGRAMS = procmap

EXTRA_DIST = obstacledata.dat.xz

BUILT_SOURCES = obstacles.cpp
CLEANFILES = obstacledata.dat obstacles.cpp

libmapdata_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS)
libmapdata_la_LIBADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS)
libmapdata_la_SOURCES = \
  basemap.cpp
noinst_HEADERS = \
  basemap.hpp \
  \
  dataio.hpp

procmap_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
procmap_LDADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
procmap_SOURCES = procmap.cpp \
  dataio.cpp

obstacledata.dat: obstacledata.dat.xz
	xz -dc $< >$@

obstacles.cpp: obstacledata.dat procmap$(EXEEXT)
	$(builddir)/procmap$(EXEEKT) \
	  --obstacle_input=$< \
	  --code_output=$@
