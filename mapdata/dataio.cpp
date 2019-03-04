#include "dataio.hpp"

#include <glog/logging.h>

namespace pxd
{

template <>
  uint16_t
  Read<uint16_t> (std::istream& in)
{
  uint16_t res = 0;
  res |= in.get ();
  res |= (in.get () << 8);

  CHECK (!in.eof ()) << "Unexpected EOF while reading input file";
  return res;
}

template <>
  int16_t
  Read<int16_t> (std::istream& in)
{
  return static_cast<int16_t> (Read<uint16_t> (in));
}

template <>
  uint32_t
  Read<uint32_t> (std::istream& in)
{
  uint32_t res = 0;
  res |= Read<uint16_t> (in);
  res |= (static_cast<uint32_t> (Read<uint16_t> (in)) << 16);

  return res;
}

template <>
  int32_t
  Read<int32_t> (std::istream& in)
{
  return static_cast<int32_t> (Read<uint32_t> (in));
}

void
WriteInt24 (std::ostream& out, uint32_t val)
{
  for (int i = 0; i < 3; ++i)
    {
      out.put (val & 0xFF);
      val >>= 8;
    }
  CHECK_EQ (val, 0) << "Writing integer too large for 24 bits";
}

} // namespace pxd
