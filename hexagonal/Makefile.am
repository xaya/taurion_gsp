noinst_LTLIBRARIES = libhexagonal.la

libhexagonal_la_CXXFLAGS = $(GLOG_CFLAGS)
libhexagonal_la_LIBADD = $(GLOG_LIBS)
libhexagonal_la_SOURCES = \
  coord.cpp \
  pathfinder.cpp
noinst_HEADERS = \
  coord.hpp \
  pathfinder.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(GTEST_MAIN_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libhexagonal.la \
  $(GTEST_MAIN_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  coord_tests.cpp \
  pathfinder_tests.cpp
