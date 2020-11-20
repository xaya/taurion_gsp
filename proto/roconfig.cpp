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

#include <map>
#include <memory>
#include <mutex>

extern "C"
{

/* Binary blob for the roconfig protocol buffer data.  */
extern const unsigned char blob_roconfig_start;
extern const unsigned char blob_roconfig_end;

} // extern C

namespace pxd
{

/* ************************************************************************** */

namespace
{

/** Lock for constructing and accessing the global singletons.  */
std::mutex mutInstances;

} // anonymous namespace

/**
 * Data for the singleton instance of the proto with all associated
 * extra stuff (like constructed items).
 */
struct RoConfig::Data
{

  /** Mutex for the mutable state.  */
  mutable std::recursive_mutex mut;

  /** The protocol buffer instance itself.  */
  proto::ConfigData proto;

  /**
   * Cache for constructed item data, where we have already done so.
   * Entries are always added to this map and never removed
   * during the entire runtime.  We store pointers rather than instances
   * so that references remain valid no matter what happens to the map.
   */
  mutable
  std::unordered_map<std::string, std::unique_ptr<const proto::ItemData>>
      constructedItems;

  /** Cache for constructed building data, similar to the items.  */
  mutable
  std::unordered_map<std::string, std::unique_ptr<const proto::BuildingData>>
      constructedBuildings;

};

RoConfig::Data* RoConfig::mainnet = nullptr;
RoConfig::Data* RoConfig::testnet = nullptr;
RoConfig::Data* RoConfig::regtest = nullptr;

RoConfig::RoConfig (const xaya::Chain chain)
{
  std::lock_guard<std::mutex> lock(mutInstances);

  Data** instancePtr = nullptr;
  bool mergeTestnet, mergeRegtest;
  switch (chain)
    {
    case xaya::Chain::MAIN:
      instancePtr = &mainnet;
      mergeTestnet = false;
      mergeRegtest = false;
      break;
    case xaya::Chain::TEST:
      instancePtr = &testnet;
      mergeTestnet = true;
      mergeRegtest = false;
      break;
    case xaya::Chain::REGTEST:
      instancePtr = &regtest;
      mergeTestnet = true;
      mergeRegtest = true;
      break;
    default:
      LOG (FATAL) << "Unexpected chain: " << static_cast<int> (chain);
    }
  CHECK (instancePtr != nullptr);

  if (*instancePtr == nullptr)
    {
      LOG (INFO) << "Initialising hard-coded ConfigData proto instance...";

      *instancePtr = new Data ();
      auto& pb = (*instancePtr)->proto;

      const auto* begin = &blob_roconfig_start;
      const auto* end = &blob_roconfig_end;
      CHECK (pb.ParseFromArray (begin, end - begin));

      CHECK (!pb.testnet_merge ().has_testnet_merge ());
      CHECK (!pb.testnet_merge ().has_regtest_merge ());

      CHECK (!pb.regtest_merge ().has_testnet_merge ());
      CHECK (!pb.regtest_merge ().has_regtest_merge ());

      if (mergeTestnet)
        pb.MergeFrom (pb.testnet_merge ());
      if (mergeRegtest)
        {
          pb.mutable_params ()->clear_prizes ();
          pb.MergeFrom (pb.regtest_merge ());
        }
      pb.clear_testnet_merge ();
      pb.clear_regtest_merge ();
    }

  data = *instancePtr;
  CHECK (data != nullptr);
}

const proto::ConfigData&
RoConfig::operator* () const
{
  return data->proto;
}

const proto::ConfigData*
RoConfig::operator-> () const
{
  return &(operator* ());
}

/* ************************************************************************** */

namespace
{

/** Prefixes for buildings that indicate a faction.  */
const std::pair<std::string, std::string> BUILDING_FACTION_PREFIXES[] =
  {
    {"r ", "r"},
    {"g ", "g"},
    {"b ", "b"},
  };

/**
 * Returns true if the given string starts with some prefix.
 */
bool
StartsWith (const std::string& str, const std::string& prefix)
{
  return str.substr (0, prefix.size ()) == prefix;
}

/**
 * Constructs the final building data proto for the given name.  This takes
 * processing like adding the faction from the name prefix into account.
 * May return null if the name does not correspond to a valid building.
 */
std::unique_ptr<const proto::BuildingData>
ConstructBuildingData (const RoConfig& cfg, const std::string& name)
{
  const auto mit = cfg->building_types ().find (name);
  if (mit == cfg->building_types ().end ())
    return nullptr;

  auto res = std::make_unique<proto::BuildingData> (mit->second);

  /* If the name matches a given prefix for a faction, set it in the
     construction data.  */
  if (res->has_construction ())
    {
      auto* constr = res->mutable_construction ();
      CHECK (!constr->has_faction ());

      for (const auto& fp : BUILDING_FACTION_PREFIXES)
        if (StartsWith (name, fp.first))
          {
            VLOG (1)
                << "Building type " << name
                << " is of faction " << fp.second;
            constr->set_faction (fp.second);
            break;
          }
    }

  return res;
}

} // anonymous namespace

const proto::BuildingData*
RoConfig::BuildingOrNull (const std::string& type) const
{
  std::lock_guard<std::recursive_mutex> lock(data->mut);

  const auto mit = data->constructedBuildings.find (type);
  if (mit != data->constructedBuildings.end ())
    return mit->second.get ();

  auto newData = ConstructBuildingData (*this, type);
  if (newData == nullptr)
    return nullptr;

  CHECK (data->constructedBuildings.emplace (type, newData.get ()).second);
  return newData.release ();
}

const proto::BuildingData&
RoConfig::Building (const std::string& type) const
{
  const auto* ptr = BuildingOrNull (type);
  CHECK (ptr != nullptr) << "Unknown building: " << type;
  return *ptr;
}

/* ************************************************************************** */

namespace
{

/** Suffix for original blueprints.  */
constexpr const char* SUFFIX_BP_ORIGINAL = " bpo";
/** Suffix for blueprint copies.  */
constexpr const char* SUFFIX_BP_COPY = " bpc";

/** Suffix for prize items.  */
constexpr const char* SUFFIX_PRIZE = " prize";

/** Space usage of a blueprint.  */
constexpr const unsigned BLUEPRINT_SPACE = 1;

/** Prefixes for vehicles that indicate a faction.  */
const std::pair<std::string, std::string> VEHICLE_FACTION_PREFIXES[] =
  {
    {"rv ", "r"},
    {"gv ", "g"},
    {"bv ", "b"},
  };

/**
 * Tries to strip the given suffix of an item name.  Returns true and the
 * base name (without suffix) if successful, and false otherwise (i.e. if
 * the suffix is not there).
 *
 * This method does not assume that the base name is an item by itself.
 */
bool
StripSuffix (const std::string& str, const std::string& suffix,
             std::string& baseName)
{
  if (str.size () < suffix.size ())
    return false;

  const size_t baseLen = str.size () - suffix.size ();
  if (str.substr (baseLen) != suffix)
    return false;

  baseName = str.substr (0, baseLen);
  return true;
}

/**
 * Tries to strip the given suffix of an item name, and then look up the base
 * item data.  Returns that data if it exists and the suffix is there, and
 * returns null if either the string does not have that suffix of the base item
 * does not exist.
 *
 * If the base item exists and the suffix is there, also the name of the base
 * item (i.e. string without suffix) is returned in baseName for further use.
 */
const proto::ItemData*
LookupBase (const RoConfig& cfg,
            const std::string& str, const std::string& suffix,
            std::string& baseName)
{
  if (!StripSuffix (str, suffix, baseName))
    return nullptr;

  return cfg.ItemOrNull (baseName);
}

/**
 * Tries to construct the item data for the given type.  This returns null
 * if the item type string does not correspond to a valid constructed item,
 * and otherwise a fresh instance of the item data.
 */
std::unique_ptr<const proto::ItemData>
ConstructItemData (const RoConfig& cfg, const std::string& item)
{
  std::string baseName;
  const proto::ItemData* base;

  base = LookupBase (cfg, item, SUFFIX_BP_ORIGINAL, baseName);
  if (base != nullptr && base->with_blueprint ())
    {
      auto res = std::make_unique<proto::ItemData> ();
      res->set_space (BLUEPRINT_SPACE);
      auto* bp = res->mutable_is_blueprint ();
      bp->set_for_item (baseName);
      if (base->has_faction ())
        res->set_faction (base->faction ());
      bp->set_original (true);
      return res;
    }

  base = LookupBase (cfg, item, SUFFIX_BP_COPY, baseName);
  if (base != nullptr && base->with_blueprint ())
    {
      auto res = std::make_unique<proto::ItemData> ();
      res->set_space (BLUEPRINT_SPACE);
      auto* bp = res->mutable_is_blueprint ();
      bp->set_for_item (baseName);
      if (base->has_faction ())
        res->set_faction (base->faction ());
      bp->set_original (false);
      return res;
    }

  if (StripSuffix (item, SUFFIX_PRIZE, baseName))
    {
      /* We only allow prize items for prizes that are actually there
         in our configuration map.  */
      for (const auto& p : cfg->params ().prizes ())
        {
          if (p.name () != baseName)
            continue;

          auto res = std::make_unique<proto::ItemData> ();
          res->set_space (0);
          res->mutable_prize ();
          return res;
        }
    }

  const auto& baseData = cfg->fungible_items ();
  const auto mit = baseData.find (item);
  if (mit == baseData.end ())
    return nullptr;

  auto res = std::make_unique<proto::ItemData> (mit->second);

  /* If this is a vehicle, check the name prefixes to apply a faction
     if one of them matches.  */
  if (res->has_vehicle ())
    {
      CHECK (!res->has_faction ());
      for (const auto& fp : VEHICLE_FACTION_PREFIXES)
        if (StartsWith (item, fp.first))
          {
            VLOG (1)
                << "Vehicle type " << item
                << " is of faction " << fp.second;
            res->set_faction (fp.second);
            break;
          }
    }

  return res;
}

} // anonymous namespace

const proto::ItemData*
RoConfig::ItemOrNull (const std::string& item) const
{
  std::lock_guard<std::recursive_mutex> lock(data->mut);

  const auto mit = data->constructedItems.find (item);
  if (mit != data->constructedItems.end ())
    return mit->second.get ();

  auto newData = ConstructItemData (*this, item);
  if (newData == nullptr)
    return nullptr;

  CHECK (data->constructedItems.emplace (item, newData.get ()).second);
  return newData.release ();
}

const proto::ItemData&
RoConfig::Item (const std::string& item) const
{
  const auto* ptr = ItemOrNull (item);
  CHECK (ptr != nullptr) << "Unknown item: " << item;
  return *ptr;
}

/* ************************************************************************** */

} // namespace pxd
