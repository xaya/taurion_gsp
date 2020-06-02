)";

DEFINE_string (out_binary, "", "Output file for the binary data");
DEFINE_string (out_text, "", "Output file for the text data");

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  gflags::SetUsageMessage ("Generate roconfig protocol buffer");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  LOG (INFO) << "Parsing hard-coded text proto...";
  pxd::proto::ConfigData pb;
  CHECK (TextFormat::ParseFromString (ROCONFIG_PROTO_TEXT, &pb));
  CHECK (TextFormat::ParseFromString (ROCONFIG_PROTO_TEXT_REGTEST,
                                      pb.mutable_regtest_merge ()));

  if (!FLAGS_out_binary.empty ())
    {
      LOG (INFO) << "Writing binary proto to output file " << FLAGS_out_binary;
      std::ofstream out(FLAGS_out_binary, std::ios::binary);
      CHECK (pb.SerializeToOstream (&out));
    }

  if (!FLAGS_out_text.empty ())
    {
      LOG (INFO) << "Writing text proto to output file " << FLAGS_out_text;
      std::ofstream out(FLAGS_out_text);
      google::protobuf::io::OstreamOutputStream zcOut(&out);
      CHECK (TextFormat::Print (pb, &zcOut));
    }

  google::protobuf::ShutdownProtobufLibrary ();
  return EXIT_SUCCESS;
}
