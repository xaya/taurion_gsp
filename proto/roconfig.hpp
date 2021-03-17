/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include "account.pb.h"
#include "config.pb.h"

#include <xayagame/gamelogic.hpp>

#include <set>

namespace pxd
{

/**
 * A light wrapper class around the read-only ConfigData proto.  It allows
 * access to the proto data itself as well as provides some helper methods
 * for accessing the data on a higher level (e.g. specifically for items
 * or buildings).
 */
class RoConfig
{

private:

  class Data;

  /**
   * A reference to the singleton instance that actually holds all the
   * global state wrapped by this instance.
   */
  const Data* data;

  /**
   * The global singleton data instance for mainnet or null when it is not yet
   * initialised.  This is never destructed.
   */
  static Data* mainnet;

  /** The singleton instance for testnet.  */
  static Data* testnet;

  /** The singleton instance for regtest.  */
  static Data* regtest;

public:

  /**
   * Constructs a fresh instance of the wrapper class, which will give
   * access to the underlying data.
   *
   * On the first call, this will also instantiate and set up the underlying
   * singleton instance with the real data.
   */
  explicit RoConfig (xaya::Chain chain);

  RoConfig (const RoConfig&) = delete;
  void operator= (const RoConfig&) = delete;

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

  /**
   * Looks up and returns the config data for a particular skill.
   */
  const proto::SkillData& Skill (proto::SkillType type) const;

  /**
   * Returns all skill types from the config, for situations that need
   * to iterate them.
   */
  std::set<proto::SkillType> AllSkillTypes () const;

};

} // namespace pxd

#endif // PROTO_ROCONFIG_HPP
