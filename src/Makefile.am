bin_PROGRAMS = pxd

pxd_CXXFLAGS = \
  $(XAYAGAME_CFLAGS) $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
pxd_LDADD = \
  $(XAYAGAME_LIBS) $(GLOG_LIBS) $(GFLAGS_LIBS) \
  -lstdc++fs
pxd_SOURCES = main.cpp \
  logic.cpp
noinst_HEADERS = \
  logic.hpp
