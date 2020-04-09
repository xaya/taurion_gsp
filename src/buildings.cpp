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

#include "buildings.hpp"

#include "mining.hpp"
#include "movement.hpp"
#include "protoutils.hpp"
#include "spawn.hpp"

#include "mapdata/regionmap.hpp"
#include "proto/config.pb.h"
#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

std::vector<HexCoord>
GetBuildingShape (const std::string& type,
                  const proto::ShapeTransformation& trafo,
                  const HexCoord& pos)
{
  const auto& roConfig = RoConfigData ().building_types ();
  const auto mit = roConfig.find (type);
  CHECK (mit != roConfig.end ()) << "Building has undefined type: " << type;
  const auto& roData = mit->second;

  std::vector<HexCoord> res;
  res.reserve (roData.shape_tiles ().size ());
  for (const auto& pbTile : roData.shape_tiles ())
    {
      HexCoord c = CoordFromProto (pbTile);
      c = c.RotateCW (trafo.rotation_steps ()); 
      c += pos;
      res.push_back (c);
    }

  return res;
}

std::vector<HexCoord>
GetBuildingShape (const Building& b)
{
  return GetBuildingShape (b.GetType (), b.GetProto ().shape_trafo (),
                           b.GetCentre ());
}

bool
CanPlaceBuilding (const std::string& type,
                  const proto::ShapeTransformation& trafo,
                  const HexCoord& pos,
                  const DynObstacles& dyn, const Context& ctx)
{
  RegionMap::IdT region = RegionMap::OUT_OF_MAP;
  for (const auto& c : GetBuildingShape (type, trafo, pos))
    {
      if (!ctx.Map ().IsPassable (c))
        {
          VLOG (1) << "Position " << c << " is not passable in the base map";
          return false;
        }
      if (!dyn.IsFree (c))
        {
          VLOG (1) << "Position " << c << " has a dynamic obstacle";
          return false;
        }

      const auto curRegion = ctx.Map ().Regions ().GetRegionId (c);
      CHECK_NE (curRegion, RegionMap::OUT_OF_MAP);

      if (region == RegionMap::OUT_OF_MAP)
        region = curRegion;
      else if (region != curRegion)
        {
          VLOG (1)
              << "Position " << c << " has region " << curRegion
              << ", while other parts are on region " << region;
          return false;
        }
    }

  return true;
}

void
InitialiseBuildings (Database& db)
{
  LOG (INFO) << "Adding initial ancient buildings to the map...";
  BuildingsTable tbl(db);

  auto h = tbl.CreateNew ("obelisk1", "", Faction::ANCIENT);
  h->SetCentre (HexCoord (-125, 810));
  UpdateBuildingStats (*h);
  h.reset ();

  h = tbl.CreateNew ("obelisk2", "", Faction::ANCIENT);
  h->SetCentre (HexCoord (-1'301, 902));
  UpdateBuildingStats (*h);
  h.reset ();

  h = tbl.CreateNew ("obelisk3", "", Faction::ANCIENT);
  h->SetCentre (HexCoord (-637, -291));
  UpdateBuildingStats (*h);
  h.reset ();
}

void
MaybeStartBuildingConstruction (Building& b, OngoingsTable& ongoings,
                                const Context& ctx)
{
  CHECK (b.GetProto ().has_foundation ());
  if (b.GetProto ().has_ongoing_construction ())
    return;

  const auto& roData = b.RoConfigData ();
  CHECK (roData.has_construction ());

  const Inventory cInv(b.GetProto ().construction_inventory ());
  for (const auto& entry : roData.construction ().full_building ())
    if (entry.second > cInv.GetFungibleCount (entry.first))
      return;

  auto op = ongoings.CreateNew ();
  op->SetHeight (ctx.Height () + roData.construction ().blocks ());
  CHECK_GT (op->GetHeight (), ctx.Height ());
  op->SetBuildingId (b.GetId ());
  op->MutableProto ().mutable_building_construction ();
  b.MutableProto ().set_ongoing_construction (op->GetId ());

  LOG (INFO)
      << "Started construction of building " << b.GetId ()
      << ": ongoing ID " << op->GetId ();
}

void
UpdateBuildingStats (Building& b)
{
  const auto& roData = b.RoConfigData ();
  const proto::BuildingData::AllCombatData* data;

  if (b.GetProto ().foundation ())
    data = &roData.foundation ();
  else
    data = &roData.full_building ();

  *b.MutableProto ().mutable_combat_data () = data->combat_data ();
  b.MutableRegenData () = data->regen_data ();
  b.MutableHP () = data->regen_data ().max_hp ();
}

void
EnterBuilding (Character& c, const Building& b, DynObstacles& dyn)
{
  dyn.RemoveVehicle (c.GetPosition (), c.GetFaction ());
  c.SetBuildingId (b.GetId ());
  c.ClearTarget ();
  c.SetEnterBuilding (Database::EMPTY_ID);
  StopCharacter (c);
  StopMining (c);
}

void
ProcessEnterBuildings (Database& db, DynObstacles& dyn)
{
  BuildingsTable buildings(db);
  CharacterTable characters(db);
  auto res = characters.QueryForEnterBuilding ();

  unsigned processed = 0;
  unsigned entered = 0;
  while (res.Step ())
    {
      ++processed;
      auto c = characters.GetFromResult (res);

      if (c->IsBusy ())
        {
          LOG (WARNING)
              << "Busy character " << c->GetId () << " can't enter building";
          continue;
        }

      const auto buildingId = c->GetEnterBuilding ();
      CHECK_NE (buildingId, Database::EMPTY_ID);

      auto b = buildings.GetById (buildingId);

      /* The building might have been destroyed in the mean time.  In this case
         we just cancel the intent.  */
      if (b == nullptr)
        {
          LOG (WARNING)
              << "Character " << c->GetId ()
              << " wants to enter non-existing building " << buildingId;
          c->SetEnterBuilding (Database::EMPTY_ID);
          continue;
        }

      const unsigned dist
          = HexCoord::DistanceL1 (c->GetPosition (), b->GetCentre ());
      if (dist > b->RoConfigData ().enter_radius ())
        {
          /* This is probably the most common case, no log spam here.  */
          continue;
        }

      LOG (INFO)
          << "Character " << c->GetId () << " is entering " << buildingId;
      ++entered;
      EnterBuilding (*c, *b, dyn);
    }

  LOG (INFO)
      << "Processed " << processed
      << " characters with 'enter building' intent, "
      << entered << " were able to enter";
}

void
LeaveBuilding (BuildingsTable& buildings, Character& c,
               xaya::Random& rnd, DynObstacles& dyn, const Context& ctx)
{
  CHECK (c.IsInBuilding ());
  auto b = buildings.GetById (c.GetBuildingId ());
  CHECK (b != nullptr);

  const auto pos
      = ChooseSpawnLocation (b->GetCentre (),
                             b->RoConfigData ().enter_radius (),
                             c.GetFaction (), rnd, dyn, ctx.Map ());

  LOG (INFO)
      << "Character " << c.GetId ()
      << " is leaving building " << b->GetId ()
      << " to location " << pos;
  c.SetPosition (pos);
  dyn.AddVehicle (pos, c.GetFaction ());
}

} // namespace pxd
