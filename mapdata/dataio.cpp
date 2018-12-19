#include "dataio.hpp"

#include <glog/logging.h>

namespace pxd
{

int16_t
ReadInt16 (std::istream& in)
{
  int res = 0;
  res |= in.get ();
  res |= (in.get () << 8);

  CHECK (!in.eof ()) << "Unexpected EOF while reading input file";
  return static_cast<int16_t> (res);
}

void
WriteInt16 (std::ostream& out, const int16_t val)
{
  const uint16_t withoutSign = static_cast<uint16_t> (val);
  out.put (withoutSign & 0xFF);
  out.put (withoutSign >> 8);
}

} // namespace pxd
