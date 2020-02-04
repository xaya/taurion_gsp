/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include "roconfig.hpp"

#include <glog/logging.h>

extern "C"
{

/* Binary blob for the roconfig protocol buffer data.  */
extern const unsigned char blob_roconfig_start;
extern const unsigned char blob_roconfig_end;

} // extern C

namespace pxd
{

namespace
{

/** Singleton instance of the proto.  */
proto::ConfigData instance;

/** Whether or not the proto has been initialised from the blob yet.  */
bool initialised = false;

} // anonymous namespace

const proto::ConfigData&
RoConfigData ()
{
  if (!initialised)
    {
      LOG (INFO) << "Initialising hard-coded ConfigData proto instance...";
      const auto* begin = &blob_roconfig_start;
      const auto* end = &blob_roconfig_end;
      CHECK (instance.ParseFromArray (begin, end - begin));
      initialised = true;
    }

  return instance;
}

} // namespace pxd
