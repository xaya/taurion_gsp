noinst_PROGRAMS = roconfig_gen
noinst_LTLIBRARIES = libpxproto.la

ROCONFIG_TEXT_PROTOS = \
  roconfig/resourcedist.pb.text \
  \
  roconfig/items/fitments.pb.text \
  roconfig/items/materials.pb.text \
  roconfig/items/prizes.pb.text \
  roconfig/items/vehicles.pb.text \
  roconfig/items/test.pb.text \
  \
  roconfig/buildings/ancient1.pb.text \
  roconfig/buildings/ancient2.pb.text \
  roconfig/buildings/ancient3.pb.text \
  roconfig/buildings/obelisk1.pb.text \
  roconfig/buildings/obelisk2.pb.text \
  roconfig/buildings/obelisk3.pb.text \
  roconfig/buildings/turrets.pb.text \
  roconfig/buildings/test.pb.text

PROTOS = \
  account.proto \
  building.proto \
  character.proto \
  combat.proto \
  config.proto \
  geometry.proto \
  inventory.proto \
  movement.proto \
  ongoing.proto \
  region.proto
EXTRA_DIST = \
  $(PROTOS) $(ROCONFIG_TEXT_PROTOS) \
  roconfig_gen_head.cpp roconfig_gen_tail.cpp

BUILT_SOURCES = $(PROTOS:.proto=.pb.h) roconfig.pb
CLEANFILES = \
  $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc) \
  roconfig_gen.cpp roconfig.pb

libpxproto_la_CXXFLAGS = $(PROTOBUF_CFLAGS)
libpxproto_la_LBADD = $(PROTOBUF_LIBS)
libpxproto_la_SOURCES = \
  roconfig.cpp \
  roitems.cpp \
  \
  roconfig_blob.s \
  $(PROTOS:.proto=.pb.cc)
noinst_HEADERS = \
  roconfig.hpp \
  roitems.hpp \
  \
  $(PROTOS:.proto=.pb.h)

roconfig_gen_CXXFLAGS = $(GLOG_CFLAGS) $(GFLAGS_CFLAGS) $(PROTOBUF_CFLAGS)
roconfig_gen_LDADD = $(GLOG_LIBS) $(GFLAGS_LIBS) $(PROTOBUF_LIBS)
roconfig_gen_SOURCES = \
  roconfig_gen.cpp \
  \
  $(PROTOS:.proto=.pb.cc)

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
  roconfig_tests.cpp \
  roitems_tests.cpp

%.pb.h %.pb.cc: $(srcdir)/%.proto
	protoc -I$(srcdir) --cpp_out=. "$<"

roconfig_gen.cpp: roconfig_gen_head.cpp $(ROCONFIG_TEXT_PROTOS) roconfig_gen_tail.cpp
	cat $^ >$@

roconfig.pb: $(builddir)/roconfig_gen
	$(builddir)/roconfig_gen --output="$@"
	touch $(srcdir)/roconfig_blob.s
