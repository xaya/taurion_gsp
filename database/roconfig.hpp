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
#include "proto/roconfig.hpp"

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
   * Returns the configuration as of the database state:  the stored config
   * if there is one, and the compiled-in configuration of the given chain
   * otherwise.  The stored config is consensus state as of the processed
   * block, so a config stored by block N's admin command takes effect from
   * block N+1 (and from state reads made once block N is processed), while
   * block N itself runs entirely on the config as of its predecessor.
   */
  RoConfig Load (xaya::Chain chain) const;

};

} // namespace pxd

#endif // DATABASE_ROCONFIG_HPP
