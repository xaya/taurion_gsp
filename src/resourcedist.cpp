/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include "resourcedist.hpp"

#include "protoutils.hpp"

#include <glog/logging.h>

#include <set>
#include <vector>

namespace pxd
{

namespace
{

/** L1 "core" radius where we have full chance around a centre.  */
constexpr HexCoord::IntT CORE_RADIUS = 400;

/** L1 outer radius where the chances fall off to zero.  */
constexpr HexCoord::IntT OUTER_RADIUS = 1'000;

/**
 * The base value we use for the resource chances.  The absolute value
 * does not really matter, as we just use this (with fall-off) for relative
 * weights between the resource types.  It should be large, though, so that
 * we get as precise integer fall-off arithmetic as possible.
 */
constexpr uint32_t BASE_CHANCE = 100'000'000;

} // anonymous namespace

namespace internal
{

uint32_t
FallOff (const uint32_t dist, const uint32_t val)
{
  if (dist > OUTER_RADIUS)
    return 0;
  if (dist <= CORE_RADIUS)
    return val;

  int64_t interpol = val - 1;
  interpol *= OUTER_RADIUS - dist;
  interpol /= OUTER_RADIUS - CORE_RADIUS;
  interpol += 1;

  CHECK_LE (interpol, val);
  CHECK (val == 1 || interpol < val);
  CHECK_GE (interpol, 1);

  return interpol;
}

} // namespace internal

namespace
{

/**
 * Data about one potentially available resource type.
 */
struct AvailableResource
{

  /** The resource type itself.  */
  std::string type;

  /** The area's centre (used for ordering).  */
  HexCoord centre;

  /** The L1 distance to the centre (for fall off).  */
  HexCoord::IntT dist;

  /** The actual weight, i.e. "fall off chance".  */
  uint32_t chance;

  /**
   * We sort AvailableResource entries by type and then by coordinate as
   * a tie-breaker.
   */
  struct Comparator
  {
    bool
    operator() (const AvailableResource& a, const AvailableResource& b)
    {
      if (a.type != b.type)
        return a.type < b.type;

      return a.centre < b.centre;
    }
  };

};

} // anonymous namespace

void
DetectResource (const HexCoord& pos, const proto::ResourceDistribution& rd,
                xaya::Random& rnd,
                std::string& type, Inventory::QuantityT& amount)
{
  VLOG (1) << "Detecting prospected resources at " << pos << "...";

  /* As the first step, we look for all available resource types (i.e. where
     we have a non-zero fall-off chance).  We sort the entries by resource
     type and coordinate, which gives us "unique" entries (by those two
     keys).  This ensures a deterministic order beyond what order the areas
     and resource types are in in the config proto.  */
  std::set<AvailableResource, AvailableResource::Comparator> availableSet;
  for (const auto& a : rd.areas ())
    {
      AvailableResource cur;
      cur.centre = CoordFromProto (a.centre ());
      cur.dist = HexCoord::DistanceL1 (pos, cur.centre);
      cur.chance = internal::FallOff (cur.dist, BASE_CHANCE);
      if (cur.chance == 0)
        continue;

      for (const auto& res : a.resources ())
        {
          cur.type = res;
          VLOG (2)
              << "Available: " << cur.type
              << " with dist " << cur.dist << " and chance " << cur.chance
              << " from centre " << cur.centre;
          const auto ins = availableSet.insert (cur);
          CHECK (ins.second)
              << "Duplicate resource type " << cur.type
              << " available at " << pos;
        }
    }
  VLOG (1) << "Number of available resources: " << availableSet.size ();

  const std::vector<AvailableResource> available(availableSet.begin (),
                                                 availableSet.end ());

  /* If there is nothing available, just return zero Agarite.  */
  if (available.empty ())
    {
      type = "raw a";
      amount = 0;
      return;
    }

  /* Pick the resource type by weight from the available ones.  */
  std::vector<uint32_t> weights;
  for (const auto& av : available)
    weights.push_back (av.chance);
  const auto ind = rnd.SelectByWeight (weights);
  const auto& picked = available[ind];
  type = picked.type;
  VLOG (1) << "Picked resource type: " << type;

  /* Determine the amount we find.  This is based on the type, a fall-off
     and a random choice.  */
  const auto mit = rd.base_amounts ().find (type);
  CHECK (mit != rd.base_amounts ().end ())
      << "Base amount for " << type << " is not defined";
  const auto baseAmount = internal::FallOff (picked.dist, mit->second);
  VLOG (1) << "Base amount: " << baseAmount;
  amount = baseAmount + rnd.NextInt (baseAmount + 1);
  VLOG (1) << "Chosen actual amount: " << amount;
}

} // namespace pxd
