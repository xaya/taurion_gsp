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

#include "prizes.hpp"

#include <glog/logging.h>

namespace pxd
{

unsigned
Prizes::GetFound (const std::string& name)
{
  auto stmt = db.Prepare (R"(
    SELECT `found` FROM `prizes` WHERE `name` = ?1
  )");
  stmt.Bind (1, name);

  class PrizesResult : public Database::ResultType
  {};

  auto res = stmt.Query<PrizesResult> ();
  CHECK (res.Step ()) << "Prize not found in database: " << name;
  const unsigned found = res.Get<int64_t> ("found");
  CHECK (!res.Step ());

  return found;
}

void
Prizes::IncrementFound (const std::string& name)
{
  VLOG (1) << "Incrementing found counter for prize " << name << "...";

  auto stmt = db.Prepare (R"(
    UPDATE `prizes`
      SET `found` = `found` + 1
      WHERE `name` = ?1
  )");
  stmt.Bind (1, name);
  stmt.Execute ();
}

} // namespace pxd
