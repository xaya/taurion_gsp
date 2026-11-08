/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2026  Autonomous Worlds Ltd

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

#ifndef PROTO_ROCONFIG_HPP
#define PROTO_ROCONFIG_HPP

#include "config.pb.h"

#include <xayagame/gamelogic.hpp>

#include <memory>
#include <string>

namespace pxd
{

/**
 * A light wrapper class around the read-only ConfigData proto.  It allows
 * access to the proto data itself as well as provides some helper methods
 * for accessing the data on a higher level (e.g. specifically for items
 * or buildings).
 *
 * An instance reads either the compiled-in configuration of a chain or one
 * stored in the game state (see RoConfigStorage), and everything it returns
 * stays valid for as long as the instance (or a copy of it) exists.  Thus
 * keep the instance around (rather than a temporary) whenever a reference or
 * pointer obtained from it is used later on.
 */
class RoConfig
{

public:

  /**
   * The data an instance reads:  the proto with everything derived from it.
   * It is defined in roconfig.cpp and exposed here only so that the code
   * there caching the data can name it.
   */
  class Data;

private:

  /**
   * The data this instance reads.  Its proto never changes once built, and
   * it is shared between all instances (and copies) for the same config.
   */
  std::shared_ptr<const Data> data;

  /**
   * Constructs an instance for a stored config, passed as the serialised
   * bytes of the ConfigData held in the roconfig database table.  The empty
   * string means that nothing is stored, and selects the compiled-in
   * configuration of the given chain.
   */
  RoConfig (xaya::Chain chain, const std::string& stored);

  /* The stored bytes are an implementation detail between this class and
     the database table wrapper, which loads them for the game state.  */
  friend class RoConfigStorage;
  friend class RoConfigStoredTests;

public:

  /**
   * Constructs an instance for the compiled-in configuration of the given
   * chain.  On the first call, this will also build that data.
   */
  explicit RoConfig (xaya::Chain chain);

  /**
   * Exposes the actual protocol buffer.
   */
  const proto::ConfigData& operator* () const;

  /**
   * Exposes the actual protocol buffer's fields directly.
   */
  const proto::ConfigData* operator-> () const;

  /**
   * Looks up and returns the configuration data for the given type of item
   * (or null if there is no such item).  This automatically "constructs" some
   * things (e.g. blueprints, tech levels) instead of just looking data up
   * in the real roconfig proto.  It should always be used instead of a direct
   * access for items.
   */
  const proto::ItemData* ItemOrNull (const std::string& item) const;

  /**
   * Looks up item data, asserting that the item exists.
   */
  const proto::ItemData& Item (const std::string& item) const;

  /**
   * Looks up the data for a building type and returns it.  If the building
   * does not exist, returns null.
   */
  const proto::BuildingData* BuildingOrNull (const std::string& type) const;

  /**
   * Looks up building data and asserts it exists.
   */
  const proto::BuildingData& Building (const std::string& type) const;

};

} // namespace pxd

#endif // PROTO_ROCONFIG_HPP
