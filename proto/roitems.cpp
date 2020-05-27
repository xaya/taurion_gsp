/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "roitems.hpp"

#include "roconfig.hpp"

#include <glog/logging.h>

#include <map>
#include <memory>

namespace pxd
{

namespace
{

/** Suffix for original blueprints.  */
constexpr const char* SUFFIX_BP_ORIGINAL = " bpo";
/** Suffix for blueprint copies.  */
constexpr const char* SUFFIX_BP_COPY = " bpc";

/**
 * Global cache for constructed item data, where we have already done so.
 * Entries are always added to this map and never removed / destructed
 * during the entire runtime.  We store pointers rather than instances
 * so that references remain valid no matter what happens to the map.
 */
std::unordered_map<std::string, const proto::ItemData*> constructedItems;

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
LookupBase (const std::string& str, const std::string& suffix,
            std::string& baseName)
{
  if (str.size () < suffix.size ())
    return nullptr;

  const size_t baseLen = str.size () - suffix.size ();
  if (str.substr (baseLen) != suffix)
    return nullptr;

  baseName = str.substr (0, baseLen);
  return RoItemDataOrNull (baseName);
}

/**
 * Tries to construct the item data for the given type.  This returns null
 * if the item type string does not correspond to a valid constructed item,
 * and otherwise a fresh instance of the item data.
 */
std::unique_ptr<const proto::ItemData>
ConstructItemData (const std::string& item)
{
  std::string baseName;
  const proto::ItemData* base;

  base = LookupBase (item, SUFFIX_BP_ORIGINAL, baseName);
  if (base != nullptr && base->with_blueprint ())
    {
      auto res = std::make_unique<proto::ItemData> ();
      res->set_space (0);
      auto* bp = res->mutable_is_blueprint ();
      bp->set_for_item (baseName);
      bp->set_original (true);
      return res;
    }

  base = LookupBase (item, SUFFIX_BP_COPY, baseName);
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
RoItemDataOrNull (const std::string& item)
{
  {
    const auto mit = constructedItems.find (item);
    if (mit != constructedItems.end ())
      return mit->second;
  }

  {
    const auto& baseData = RoConfig ()->fungible_items ();
    const auto mit = baseData.find (item);
    if (mit != baseData.end ())
      return &mit->second;
  }

  auto newData = ConstructItemData (item);
  if (newData == nullptr)
    return nullptr;

  CHECK (constructedItems.emplace (item, newData.get ()).second);
  return newData.release ();
}

const proto::ItemData&
RoItemData (const std::string& item)
{
  const auto* ptr = RoItemDataOrNull (item);
  CHECK (ptr != nullptr) << "Unknown item: " << item;
  return *ptr;
}

} // namespace pxd
