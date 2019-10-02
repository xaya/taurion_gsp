)";

/** Singleton instance of the proto.  */
proto::ConfigData instance;

/** Whether or not the proto has been initialised from the text yet.  */
bool initialised = false;

} // anonymous namespace

const proto::ConfigData&
RoConfigData ()
{
  if (!initialised)
    {
      LOG (INFO) << "Initialising hard-coded ConfigData proto instance...";
      CHECK (TextFormat::ParseFromString (ROCONFIG_PROTO_TEXT, &instance));
      initialised = true;
    }

  return instance;
}

} // namespace pxd
