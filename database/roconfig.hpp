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

#ifndef DATABASE_ROCONFIG_HPP
#define DATABASE_ROCONFIG_HPP

#include "database.hpp"

#include "proto/config.pb.h"

#include <xayagame/gamelogic.hpp>

#include <memory>

namespace pxd
{

/**
 * Wrapper class around the database table holding the runtime-modified
 * configuration data.
 */
class RoConfigStorage
{

private:

  /** The underlying database handle.  */
  Database& db;

public:

  explicit RoConfigStorage (Database& d)
    : db(d)
  {}

  RoConfigStorage () = delete;
  RoConfigStorage (const RoConfigStorage&) = delete;
  void operator= (const RoConfigStorage&) = delete;

  /**
   * Returns the stored config, or null if no modified config has been
   * stored yet.
   */
  std::unique_ptr<proto::ConfigData> Get () const;

  /**
   * Stores the given config, replacing any previously stored one.
   */
  void Set (const proto::ConfigData& cfg);

  /**
   * Brings the in-memory configuration in sync with the config this table
   * holds.  The stored config is consensus state as of the processed block,
   * so every entry point working on the main database must call this first;
   * after a reorg, the restored row is picked up the same way.  A config
   * stored by block N's admin command thus takes effect from block N+1 (and
   * from state reads made once block N is processed), while block N itself
   * runs entirely on the config as of its predecessor.
   *
   * Only callers holding the libxayagame Game lock and reading the main
   * database, i.e. the chain tip, may use this.  Lock-free state reads must
   * not:  they work off a state snapshot that can lag the tip, and would
   * activate an outdated config underneath the block being processed.  They
   * also have no need to, since the instance state is refreshed (and with it
   * the config) before a new snapshot is published.
   */
  void Sync (xaya::Chain chain) const;

};

} // namespace pxd

#endif // DATABASE_ROCONFIG_HPP
