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
 * An instance reads the configuration as it was when the instance was
 * constructed, and everything it returns stays valid for as long as the
 * instance exists -- but not beyond it, since an admin command may have
 * activated a different configuration in the meantime.  Thus keep the
 * instance around (rather than a temporary) whenever a reference or pointer
 * obtained from it is used later on.
 */
class RoConfig
{

private:

  class Data;

  /**
   * The variants of the configuration that exist.  Which one applies is
   * determined by the chain, and each has its own compile-time merges as
   * well as its own current data.
   */
  enum class Variant
  {
    MAINNET = 0,
    TESTNET = 1,
    REGTEST = 2,
  };

  /** Number of variants, i.e. the size of the array of current data.  */
  static constexpr int NUM_VARIANTS = 3;

  /**
   * The data this instance reads.  Holding it keeps the proto and everything
   * derived from it alive for as long as the instance exists, even if newer
   * data is activated meanwhile.
   */
  std::shared_ptr<const Data> data;

  /**
   * The current data of each variant, or null before it is first used.  New
   * instances read whatever is in here at the time they are constructed.
   */
  static std::shared_ptr<const Data> current[NUM_VARIANTS];

  /** Returns the variant that applies for the given chain.  */
  static Variant VariantFor (xaya::Chain chain);

  /**
   * Builds a fresh instance of the data:  from the given stored config if it
   * is non-empty, and from the compiled-in blob with the variant's merges
   * applied otherwise.
   */
  static std::shared_ptr<const Data> Build (Variant v,
                                            const std::string& stored);

  /**
   * Activates a stored config for the given chain, passed as the serialised
   * bytes of the ConfigData held in the roconfig database table (the empty
   * string means that nothing is stored, and activates the compiled-in
   * configuration).  If they differ from the bytes the current data was
   * built from, fresh data is built and is what instances constructed from
   * now on read; passing the bytes rather than the proto is what keeps that
   * comparison cheap.
   *
   * Existing instances keep the data they were constructed with, so this is
   * safe to call while other threads read the config.  It must, however, only
   * be called with the config as of the current chain tip:  a caller working
   * off an older state snapshot would otherwise activate an outdated config
   * underneath the block that is being processed.
   */
  static void ApplyStored (xaya::Chain chain, const std::string& stored);

  /* The stored bytes are an implementation detail between this class and
     the database table wrapper, which syncs them via ApplyStored.  The
     test fixture exercises activation directly.  */
  friend class RoConfigStorage;
  friend class RoConfigStoredTests;

public:

  /**
   * Constructs a fresh instance of the wrapper class, which will give
   * access to the configuration that is current for the given chain.
   *
   * On the first call, this will also build that data from the compiled-in
   * configuration.
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

};

} // namespace pxd

#endif // PROTO_ROCONFIG_HPP
