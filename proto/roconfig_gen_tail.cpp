)";

DEFINE_string (output, "", "Output file for the binary roconfig proto data");

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

  if (!FLAGS_output.empty ())
    {
      LOG (INFO) << "Writing binary proto to output file " << FLAGS_output;
      std::ofstream out(FLAGS_output, std::ios::binary);
      CHECK (pb.SerializeToOstream (&out));
      out.close ();
    }

  google::protobuf::ShutdownProtobufLibrary ();
  return EXIT_SUCCESS;
}
