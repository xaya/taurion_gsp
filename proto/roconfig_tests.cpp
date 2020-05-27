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

#include "roitems.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

TEST (RoConfigTests, Parses)
{
  *RoConfig ();
}

TEST (RoConfigTests, ProtoIsSingleton)
{
  const auto* ptr1 = &(*RoConfig ());
  const auto* ptr2 = &(*RoConfig ());
  EXPECT_EQ (ptr1, ptr2);
}

TEST (RoConfigTests, HasData)
{
  EXPECT_GT (RoConfig ()->fungible_items ().size (), 0);
}

/* ************************************************************************** */

class RoConfigSanityTests : public testing::Test
{

private:

  /**
   * Checks if all keys in the given map (e.g. item types to amounts
   * needed for construction, or item types to amounts yielded from
   * refining) are valid materials.
   */
  template <typename Map>
    static bool
    IsValidMaterialMap (const Map& m)
  {
    for (const auto& entry : m)
      {
        const std::string type = entry.first;
        const auto* item = RoItemDataOrNull (type);
        if (item == nullptr)
          {
            LOG (WARNING) << "Unknown item: " << type;
            return false;
          }

        if (item->with_blueprint ()
              || !item->construction_resources ().empty ()
              || item->type_case () != proto::ItemData::TYPE_NOT_SET)
          {
            LOG (WARNING) << "Item is not a material: " << type;
            return false;
          }
      }

    return true;
  }

  /**
   * Checks if the given item type is a valid raw material.
   */
  static bool
  IsRawMaterial (const std::string& type)
  {
    const auto* item = RoItemDataOrNull (type);
    if (item == nullptr)
      {
        LOG (WARNING) << "Unknown item: " << type;
        return false;
      }

    /* FIXME: Once we actually have added in stats for refining of the
       raw materials (raw X), check that the item type has a refines entry.  */
    return true;
  }

  /**
   * Checks if the given string is a valid equipment slot.
   */
  static bool
  IsValidEquipmentSlot (const std::string& str)
  {
    return str == "high" || str == "mid" || str == "low";
  }

protected:

  /**
   * Checks if the given config instance is valid.  This verifies things
   * like that item types referenced from other item configs (e.g. what
   * something refines to) are actually valid.
   */
  static bool IsConfigValid (const RoConfig& cfg);

};

bool
RoConfigSanityTests::IsConfigValid (const RoConfig& cfg)
{
  for (const auto& entry : cfg->fungible_items ())
    {
      const auto& i = entry.second;

      if (!IsValidMaterialMap (i.construction_resources ()))
        {
          LOG (WARNING)
              << "Item construction data is invalid for " << entry.first;
          return false;
        }

      if (!IsValidMaterialMap (i.refines ().outputs ()))
        {
          LOG (WARNING) << "Refines-to data is invalid for " << entry.first;
          return false;
        }

      if (i.has_is_blueprint ())
        {
          LOG (WARNING) << "Blueprint item defined: " << entry.first;
          return false;
        }

      for (const auto& output : i.reveng ().possible_outputs ())
        {
          const auto* o = RoItemDataOrNull (output);
          if (o == nullptr || !o->is_blueprint ().original ())
            {
              LOG (WARNING)
                  << "Item " << entry.first
                  << " has reveng output " << output
                  << " which is not a blueprint original";
              return false;
            }
        }

      for (const auto& s : i.vehicle ().equipment_slots ())
        if (!IsValidEquipmentSlot (s.first))
          {
            LOG (WARNING)
                << "Vehicle type " << entry.first
                << " has invalid equipment slot " << s.first;
            return false;
          }

      if (i.has_fitment () && !IsValidEquipmentSlot (i.fitment ().slot ()))
        {
          LOG (WARNING)
              << "Fitment " << entry.first
              << " uses invalid slot " << i.fitment ().slot ();
          return false;
        }
    }

  for (const auto& entry : cfg->building_types ())
    {
      const auto& b = entry.second;
      if (!IsValidMaterialMap (b.construction ().foundation ()))
        {
          LOG (WARNING)
              << "Building foundation data is invalid for " << entry.first;
          return false;
        }
      if (!IsValidMaterialMap (b.construction ().full_building ()))
        {
          LOG (WARNING)
              << "Building construction data is invalid for " << entry.first;
          return false;
        }
    }

  for (const auto& entry : cfg->resource_dist ().base_amounts ())
    if (!IsRawMaterial (entry.first))
      {
        LOG (WARNING) << "Invalid base amounts in resource dist";
        return false;
      }
  for (const auto& area : cfg->resource_dist ().areas ())
    for (const auto& type : area.resources ())
      if (!IsRawMaterial (type))
        {
          LOG (WARNING) << "Invalid resource areas for distribution";
          return false;
        }

  return true;
}

TEST_F (RoConfigSanityTests, Valid)
{
  EXPECT_TRUE (IsConfigValid (RoConfig ()));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
