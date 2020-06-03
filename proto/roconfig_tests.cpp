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
#include <gtest/gtest.h>

namespace pxd
{
namespace
{

/* ************************************************************************** */

TEST (RoConfigTests, ConstructionWorks)
{
  *RoConfig (xaya::Chain::MAIN);
  *RoConfig (xaya::Chain::TEST);
  *RoConfig (xaya::Chain::REGTEST);
}

TEST (RoConfigTests, ProtoIsSingleton)
{
  const auto* ptr1 = &(*RoConfig (xaya::Chain::MAIN));
  const auto* ptr2 = &(*RoConfig (xaya::Chain::MAIN));
  const auto* ptr3 = &(*RoConfig (xaya::Chain::REGTEST));
  EXPECT_EQ (ptr1, ptr2);
  EXPECT_NE (ptr1, ptr3);
}

TEST (RoConfigTests, ChainDependence)
{
  const RoConfig main(xaya::Chain::MAIN);
  const RoConfig test(xaya::Chain::TEST);
  const RoConfig regtest(xaya::Chain::REGTEST);

  EXPECT_NE (main.ItemOrNull ("raw a"), nullptr);
  EXPECT_NE (test.ItemOrNull ("raw a"), nullptr);
  EXPECT_NE (regtest.ItemOrNull ("raw a"), nullptr);
  EXPECT_EQ (main.ItemOrNull ("bow"), nullptr);
  EXPECT_EQ (test.ItemOrNull ("bow"), nullptr);
  EXPECT_NE (regtest.ItemOrNull ("bow"), nullptr);

  EXPECT_NE (main.BuildingOrNull ("ancient1"), nullptr);
  EXPECT_NE (regtest.BuildingOrNull ("ancient1"), nullptr);
  EXPECT_EQ (main.BuildingOrNull ("huesli"), nullptr);
  EXPECT_NE (regtest.BuildingOrNull ("huesli"), nullptr);

  EXPECT_EQ (main->params ().prospection_expiry_blocks (), 5'000);
  EXPECT_EQ (test->params ().prospection_expiry_blocks (), 5'000);
  EXPECT_EQ (regtest->params ().prospection_expiry_blocks (), 100);

  EXPECT_EQ (main->params ().dev_addr (), "DHy2615XKevE23LVRVZVxGeqxadRGyiFW4");
  EXPECT_EQ (test->params ().dev_addr (), "dSFDAWxovUio63hgtfYd3nz3ir61sJRsXn");
  EXPECT_EQ (regtest->params ().dev_addr (),
             "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p");

  EXPECT_FALSE (main->params ().god_mode ());
  EXPECT_FALSE (test->params ().god_mode ());
  EXPECT_TRUE (regtest->params ().god_mode ());
}

TEST (RoConfigTests, Building)
{
  const RoConfig cfg(xaya::Chain::REGTEST);
  EXPECT_EQ (cfg.BuildingOrNull ("invalid building"), nullptr);
  EXPECT_GT (cfg.Building ("ancient1").enter_radius (), 0);
}

/* ************************************************************************** */

class RoItemsTests : public testing::Test
{

protected:

  RoConfig cfg;

  RoItemsTests ()
    : cfg(xaya::Chain::REGTEST)
  {}

};

TEST_F (RoItemsTests, BasicItem)
{
  EXPECT_EQ (cfg.Item ("foo").space (), 10);

  EXPECT_NE (cfg.ItemOrNull ("foo"), nullptr);
  EXPECT_EQ (cfg.ItemOrNull ("invalid item"), nullptr);
}

TEST_F (RoItemsTests, Blueprints)
{
  EXPECT_EQ (cfg.ItemOrNull ("bpo"), nullptr);
  EXPECT_EQ (cfg.ItemOrNull ("bowbpo"), nullptr);
  EXPECT_EQ (cfg.ItemOrNull ("bow bpo "), nullptr);

  EXPECT_EQ (cfg.ItemOrNull ("foo bpo"), nullptr);
  EXPECT_EQ (cfg.ItemOrNull ("foo bpc"), nullptr);

  const auto& orig = cfg.Item ("bow bpo");
  ASSERT_TRUE (orig.has_is_blueprint ());
  EXPECT_EQ (orig.space (), 0);
  EXPECT_EQ (orig.is_blueprint ().for_item (), "bow");
  EXPECT_TRUE (orig.is_blueprint ().original ());

  const auto& copy = cfg.Item ("bow bpc");
  ASSERT_TRUE (copy.has_is_blueprint ());
  EXPECT_EQ (copy.space (), 0);
  EXPECT_EQ (copy.is_blueprint ().for_item (), "bow");
  EXPECT_FALSE (copy.is_blueprint ().original ());
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
    IsValidMaterialMap (const RoConfig& cfg, const Map& m)
  {
    for (const auto& entry : m)
      {
        const std::string type = entry.first;
        const auto* item = cfg.ItemOrNull (type);
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
  IsRawMaterial (const RoConfig& cfg, const std::string& type)
  {
    const auto* item = cfg.ItemOrNull (type);
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
  if (cfg->has_testnet_merge () || cfg->has_regtest_merge ())
    {
      LOG (WARNING) << "Merge data still present";
      return false;
    }

  for (const auto& entry : cfg->fungible_items ())
    {
      const auto& i = entry.second;

      if (!IsValidMaterialMap (cfg, i.construction_resources ()))
        {
          LOG (WARNING)
              << "Item construction data is invalid for " << entry.first;
          return false;
        }

      if (!IsValidMaterialMap (cfg, i.refines ().outputs ()))
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
          const auto* o = cfg.ItemOrNull (output);
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
      if (!IsValidMaterialMap (cfg, b.construction ().foundation ()))
        {
          LOG (WARNING)
              << "Building foundation data is invalid for " << entry.first;
          return false;
        }
      if (!IsValidMaterialMap (cfg, b.construction ().full_building ()))
        {
          LOG (WARNING)
              << "Building construction data is invalid for " << entry.first;
          return false;
        }
    }

  for (const auto& entry : cfg->resource_dist ().base_amounts ())
    if (!IsRawMaterial (cfg, entry.first))
      {
        LOG (WARNING) << "Invalid base amounts in resource dist";
        return false;
      }
  for (const auto& area : cfg->resource_dist ().areas ())
    for (const auto& type : area.resources ())
      if (!IsRawMaterial (cfg, type))
        {
          LOG (WARNING) << "Invalid resource areas for distribution";
          return false;
        }

  return true;
}

TEST_F (RoConfigSanityTests, Valid)
{
  EXPECT_TRUE (IsConfigValid (RoConfig (xaya::Chain::MAIN)));
  EXPECT_TRUE (IsConfigValid (RoConfig (xaya::Chain::TEST)));
  EXPECT_TRUE (IsConfigValid (RoConfig (xaya::Chain::REGTEST)));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
