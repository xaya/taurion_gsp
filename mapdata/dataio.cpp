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

template <>
  void
  Write<int16_t> (std::ostream& out, const int16_t val)
{
  const uint16_t withoutSign = static_cast<uint16_t> (val);
  out.put (withoutSign & 0xFF);
  out.put (withoutSign >> 8);
}

} // namespace pxd
