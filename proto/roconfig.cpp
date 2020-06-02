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

};

RoConfig::Data* RoConfig::instance = nullptr;

RoConfig::RoConfig (const xaya::Chain chain)
{
  std::lock_guard<std::mutex> lock(mutInstances);

  /* FIXME: Construct different data for regtest.  */

  if (instance == nullptr)
    {
      LOG (INFO) << "Initialising hard-coded ConfigData proto instance...";
      instance = new Data ();
      const auto* begin = &blob_roconfig_start;
      const auto* end = &blob_roconfig_end;
      CHECK (instance->proto.ParseFromArray (begin, end - begin));
    }

  data = instance;
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

const proto::BuildingData*
RoConfig::BuildingOrNull (const std::string& type) const
{
  const auto& buildings = (*this)->building_types ();

  const auto mit = buildings.find (type);
  if (mit == buildings.end ())
    return nullptr;

  return &mit->second;
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
  if (str.size () < suffix.size ())
    return nullptr;

  const size_t baseLen = str.size () - suffix.size ();
  if (str.substr (baseLen) != suffix)
    return nullptr;

  baseName = str.substr (0, baseLen);
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
      res->set_space (0);
      auto* bp = res->mutable_is_blueprint ();
      bp->set_for_item (baseName);
      bp->set_original (true);
      return res;
    }

  base = LookupBase (cfg, item, SUFFIX_BP_COPY, baseName);
  if (base != nullptr && base->with_blueprint ())
    {
      auto res = std::make_unique<proto::ItemData> ();
      res->set_space (0);
      auto* bp = res->mutable_is_blueprint ();
      bp->set_for_item (baseName);
      bp->set_original (false);
      return res;
    }

  return nullptr;
}

} // anonymous namespace

const proto::ItemData*
RoConfig::ItemOrNull (const std::string& item) const
{
  {
    std::lock_guard<std::recursive_mutex> lock(data->mut);
    const auto mit = data->constructedItems.find (item);
    if (mit != data->constructedItems.end ())
      return mit->second.get ();
  }

  {
    const auto& baseData = (*this)->fungible_items ();
    const auto mit = baseData.find (item);
    if (mit != baseData.end ())
      return &mit->second;
  }

  std::lock_guard<std::recursive_mutex> lock(data->mut);

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
