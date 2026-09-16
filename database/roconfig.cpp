/*
    GSP for the Taurion blockchain game
    Copyright (C) 2026  Autonomous Worlds Ltd

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

#include "lazyproto.hpp"

#include "proto/roconfig.hpp"

#include <glog/logging.h>

#include <string>
#include <utility>

namespace pxd
{

namespace
{

struct StorageResult : public Database::ResultType
{
  RESULT_COLUMN (pxd::proto::ConfigData, data, 1);
};

/**
 * Reads the stored row as a lazy proto, which is empty if there is no
 * stored config.  Syncing only needs the serialised bytes, so it can
 * avoid parsing them.
 */
LazyProto<proto::ConfigData>
ReadStored (Database& db)
{
  auto stmt = db.Prepare (R"(
    SELECT `data`
      FROM `roconfig`
      WHERE `id` = 1
  )");

  auto res = stmt.Query<StorageResult> ();
  if (!res.Step ())
    {
      LazyProto<proto::ConfigData> empty;
      empty.SetToDefault ();
      return empty;
    }

  auto data = res.GetProto<StorageResult::data> ();
  CHECK (!res.Step ());

  return data;
}

} // anonymous namespace

std::unique_ptr<proto::ConfigData>
RoConfigStorage::Get () const
{
  auto stored = ReadStored (db);
  if (stored.IsEmpty ())
    return nullptr;

  return std::make_unique<proto::ConfigData> (std::move (stored.Mutable ()));
}

void
RoConfigStorage::Set (const proto::ConfigData& cfg)
{
  std::string bytes;
  CHECK (cfg.SerializeToString (&bytes));
  /* An empty serialisation is how the absence of a stored config is
     expressed, so it must never be what a row holds.  */
  CHECK (!bytes.empty ()) << "Empty roconfig data";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `roconfig`
      (`id`, `data`) VALUES (1, ?1)
  )");
  const LazyProto<proto::ConfigData> lp(std::move (bytes));
  stmt.BindProto (1, lp);
  stmt.Execute ();
}

void
RoConfigStorage::Sync (const xaya::Chain chain) const
{
  RoConfig::ApplyStored (chain, ReadStored (db).GetSerialised ());
}

} // namespace pxd
