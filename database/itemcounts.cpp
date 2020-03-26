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

#include "itemcounts.hpp"

#include <glog/logging.h>

namespace pxd
{

namespace
{

struct ItemCountsResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, found, 1);
};

} // anonymous namespace

unsigned
ItemCounts::GetFound (const std::string& name)
{
  auto stmt = db.Prepare (R"(
    SELECT `found`
      FROM `item_counts`
      WHERE `name` = ?1
  )");
  stmt.Bind (1, name);

  auto res = stmt.Query<ItemCountsResult> ();
  if (!res.Step ())
    return 0;

  const unsigned found = res.Get<ItemCountsResult::found> ();
  CHECK (!res.Step ());

  return found;
}

void
ItemCounts::IncrementFound (const std::string& name)
{
  VLOG (1) << "Incrementing found counter for item " << name << "...";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `item_counts`
      (`name`, `found`)
      VALUES (?1, ?2)
  )");
  stmt.Bind (1, name);
  stmt.Bind (2, GetFound (name) + 1);
  stmt.Execute ();
}

} // namespace pxd
