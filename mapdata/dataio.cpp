/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
