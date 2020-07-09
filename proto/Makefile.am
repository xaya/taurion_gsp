noinst_PROGRAMS = roconfig_gen
noinst_LTLIBRARIES = libpxproto.la

ROCONFIG_TEXT_PROTOS = \
  roconfig/initialbuildings.pb.text \
  roconfig/params.pb.text \
  roconfig/resourcedist.pb.text \
  roconfig/safezones.pb.text \
  \
  roconfig/items/artefacts.pb.text \
  roconfig/items/fitments.pb.text \
  roconfig/items/materials.pb.text \
  roconfig/items/vehicles_others.pb.text \
  roconfig/items/vehicles_starter.pb.text \
  \
  roconfig/buildings/ancient1.pb.text \
  roconfig/buildings/ancient2.pb.text \
  roconfig/buildings/ancient3.pb.text \
  roconfig/buildings/obelisk1.pb.text \
  roconfig/buildings/obelisk2.pb.text \
  roconfig/buildings/obelisk3.pb.text \
  \
  roconfig/buildings/r_c1.pb.text \
  roconfig/buildings/r_c2.pb.text \
  roconfig/buildings/r_c3.pb.text \
  roconfig/buildings/r_c4.pb.text \
  roconfig/buildings/r_c5.pb.text \
  roconfig/buildings/r_c6.pb.text \
  roconfig/buildings/r_c7.pb.text \
  roconfig/buildings/r_c8.pb.text \
  roconfig/buildings/r_c9.pb.text \
  roconfig/buildings/r_c10.pb.text \
  roconfig/buildings/r_c11.pb.text \
  roconfig/buildings/r_cc.pb.text \
  roconfig/buildings/r_cf.pb.text \
  roconfig/buildings/r_et.pb.text \
  roconfig/buildings/r_ep.pb.text \
  roconfig/buildings/r_rf.pb.text \
  roconfig/buildings/r_r.pb.text \
  roconfig/buildings/r_rt.pb.text \
  roconfig/buildings/r_ss.pb.text \
  roconfig/buildings/r_vb.pb.text \
  \
  roconfig/buildings/g_c1.pb.text \
  roconfig/buildings/g_c2.pb.text \
  roconfig/buildings/g_c3.pb.text \
  roconfig/buildings/g_c4.pb.text \
  roconfig/buildings/g_c5.pb.text \
  roconfig/buildings/g_c6.pb.text \
  roconfig/buildings/g_c7.pb.text \
  roconfig/buildings/g_c8.pb.text \
  roconfig/buildings/g_c9.pb.text \
  roconfig/buildings/g_c10.pb.text \
  roconfig/buildings/g_cc.pb.text \
  roconfig/buildings/g_cf.pb.text \
  roconfig/buildings/g_et.pb.text \
  roconfig/buildings/g_ep.pb.text \
  roconfig/buildings/g_rf.pb.text \
  roconfig/buildings/g_r.pb.text \
  roconfig/buildings/g_rt.pb.text \
  roconfig/buildings/g_ss.pb.text \
  roconfig/buildings/g_vb.pb.text \
  \
  roconfig/buildings/b_c1.pb.text \
  roconfig/buildings/b_c2.pb.text \
  roconfig/buildings/b_c3.pb.text \
  roconfig/buildings/b_c4.pb.text \
  roconfig/buildings/b_c5.pb.text \
  roconfig/buildings/b_c6.pb.text \
  roconfig/buildings/b_c7.pb.text \
  roconfig/buildings/b_c8.pb.text \
  roconfig/buildings/b_c9.pb.text \
  roconfig/buildings/b_c10.pb.text \
  roconfig/buildings/b_c11.pb.text \
  roconfig/buildings/b_cc.pb.text \
  roconfig/buildings/b_cf.pb.text \
  roconfig/buildings/b_et.pb.text \
  roconfig/buildings/b_ep.pb.text \
  roconfig/buildings/b_rf.pb.text \
  roconfig/buildings/b_r.pb.text \
  roconfig/buildings/b_rt.pb.text \
  roconfig/buildings/b_ss.pb.text \
  roconfig/buildings/b_vb.pb.text \
  \
  roconfig/testnet.pb.text

ROCONFIG_TEXT_PROTOS_REGTEST = \
  roconfig/test_params.pb.text \
  roconfig/test_resourcedist.pb.text \
  roconfig/test_safezones.pb.text \
  roconfig/buildings/test.pb.text \
  roconfig/items/test.pb.text

PROTOS = \
  account.proto \
  building.proto \
  character.proto \
  combat.proto \
  config.proto \
  geometry.proto \
  inventory.proto \
  modifier.proto \
  movement.proto \
  ongoing.proto \
  region.proto
EXTRA_DIST = \
  $(PROTOS) $(ROCONFIG_TEXT_PROTOS) $(ROCONFIG_TEXT_PROTOS_REGTEST) \
  roconfig_gen_head.cpp roconfig_gen_mid.cpp roconfig_gen_tail.cpp

BUILT_SOURCES = \
  $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=_pb2.py) \
  roconfig_gen.cpp \
  roconfig.pb.text roconfig.pb
CLEANFILES = \
  $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc) $(PROTOS:.proto=_pb2.py) \
  roconfig_gen.cpp roconfig.pb.text roconfig.pb

libpxproto_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) $(PROTOBUF_CFLAGS)
libpxproto_la_LIBADD = $(XAYAGAME_LIBS) $(PROTOBUF_LIBS)
libpxproto_la_SOURCES = \
  roconfig.cpp \
  \
  roconfig_blob.s \
  $(PROTOS:.proto=.pb.cc)
noinst_HEADERS = \
  roconfig.hpp \
  \
  $(PROTOS:.proto=.pb.h)

roconfig_gen_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS) $(PROTOBUF_CFLAGS)
roconfig_gen_LDADD = $(GLOG_LIBS) $(GFLAGS_LIBS) $(PROTOBUF_LIBS)
roconfig_gen_SOURCES = \
  roconfig_gen.cpp \
  \
  $(PROTOS:.proto=.pb.cc)

noinst_PYTHON = \
  __init__.py \
  $(PROTOS:.proto=_pb2.py)

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

%.pb.h %.pb.cc: $(top_srcdir)/proto/%.proto
	protoc -I$(top_srcdir) --cpp_out=$(top_builddir) "$<"
%_pb2.py: $(top_srcdir)/proto/%.proto
	protoc -I$(top_srcdir) --python_out=$(top_builddir) "$<"

roconfig_gen.cpp: roconfig_gen_head.cpp $(ROCONFIG_TEXT_PROTOS) roconfig_gen_mid.cpp $(ROCONFIG_TEXT_PROTOS_REGTEST) roconfig_gen_tail.cpp
	cat $^ >$@

roconfig.pb roconfig.pb.text: $(builddir)/roconfig_gen
	$(builddir)/roconfig_gen \
          --out_binary="$(builddir)/roconfig.pb" \
          --out_text="$(builddir)/roconfig.pb.text"
	touch $(srcdir)/roconfig_blob.s
