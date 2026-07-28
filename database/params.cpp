/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

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

#include "params.hpp"

#include <glog/logging.h>

namespace pxd
{

namespace
{

/** Result type for parameter value lookups.  */
struct ParamValueResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, value, 1);
};

} // anonymous namespace

int64_t
ParamsTable::Get (const std::string& name, const int64_t fallback) const
{
  auto stmt = db.Prepare (R"(
    SELECT `value`
      FROM `parameters`
      WHERE `name` = ?1
  )");
  stmt.Bind (1, name);

  auto res = stmt.Query<ParamValueResult> ();
  if (!res.Step ())
    return fallback;
  const int64_t value = res.Get<ParamValueResult::value> ();
  CHECK (!res.Step ());
  return value;
}

void
ParamsTable::Set (const std::string& name, const int64_t value)
{
  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `parameters`
      (`name`, `value`) VALUES (?1, ?2)
  )");
  stmt.Bind (1, name);
  stmt.Bind (2, value);
  stmt.Execute ();
}

void
ParamsTable::Remove (const std::string& name)
{
  auto stmt = db.Prepare (R"(
    DELETE FROM `parameters`
      WHERE `name` = ?1
  )");
  stmt.Bind (1, name);
  stmt.Execute ();
}

} // namespace pxd
