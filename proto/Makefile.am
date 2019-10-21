noinst_LTLIBRARIES = libpxproto.la

ROCONFIG_TEXT_PROTOS = \
  roconfig/items.pb.text

PROTOS = \
  character.proto \
  combat.proto \
  config.proto \
  geometry.proto \
  inventory.proto \
  movement.proto \
  region.proto
EXTRA_DIST = \
  $(PROTOS) $(ROCONFIG_TEXT_PROTOS) \
  roconfig_head.cpp roconfig_tail.cpp

BUILT_SOURCES = $(PROTOS:.proto=.pb.h)
CLEANFILES = $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc) roconfig.cpp

libpxproto_la_CXXFLAGS = $(PROTOBUF_CFLAGS)
libpxproto_la_LIBADD = $(PROTOBUF_LIBS)
libpxproto_la_SOURCES = \
  roconfig.cpp \
  \
  $(PROTOS:.proto=.pb.cc)
noinst_HEADERS = \
  roconfig.hpp \
  \
  $(PROTOS:.proto=.pb.h)

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GTEST_MAIN_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS) $(PROTOBUF_CFLAGS)
tests_LDADD = \
  $(builddir)/libpxproto.la \
  $(GTEST_MAIN_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS) $(PROTOBUF_LIBS)
tests_SOURCES = \
  roconfig_tests.cpp

%.pb.h %.pb.cc: $(srcdir)/%.proto
	protoc -I$(srcdir) --cpp_out=. "$<"

roconfig.cpp: roconfig_head.cpp $(ROCONFIG_TEXT_PROTOS) roconfig_tail.cpp
	cat $^ >$@
