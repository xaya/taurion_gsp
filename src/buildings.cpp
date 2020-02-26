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

#include "proto/config.pb.h"
#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

std::vector<HexCoord>
GetBuildingShape (const Building& b)
{
  const auto& buildings = RoConfigData ().building_types ();
  const auto mit = buildings.find (b.GetType ());
  CHECK (mit != buildings.end ())
      << "Building " << b.GetId () << " has undefined type: " << b.GetType ();

  const auto centre = b.GetCentre ();
  const auto& trafo = b.GetProto ().shape_trafo ();

  std::vector<HexCoord> res;
  res.reserve (mit->second.shape_tiles ().size ());
  for (const auto& pbTile : mit->second.shape_tiles ())
    {
      HexCoord c = CoordFromProto (pbTile);
      c = c.RotateCW (trafo.rotation_steps ()); 
      c += centre;
      res.push_back (c);
    }

  return res;
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
UpdateBuildingStats (Building& b)
{
  const auto& buildingData = RoConfigData ().building_types ();
  const auto mit = buildingData.find (b.GetType ());
  CHECK (mit != buildingData.end ())
      << "Unknown building type: " << b.GetType ();
  const auto& roData = mit->second;

  *b.MutableProto ().mutable_combat_data () = roData.combat_data ();
  b.MutableRegenData () = roData.regen_data ();
  b.MutableHP () = roData.regen_data ().max_hp ();
}

void
ProcessEnterBuildings (Database& db)
{
  const auto& buildingData = RoConfigData ().building_types ();

  BuildingsTable buildings(db);
  CharacterTable characters(db);
  auto res = characters.QueryForEnterBuilding ();

  unsigned processed = 0;
  unsigned entered = 0;
  while (res.Step ())
    {
      ++processed;
      auto c = characters.GetFromResult (res);

      if (c->GetBusy () > 0)
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

      const auto mit = buildingData.find (b->GetType ());
      CHECK (mit != buildingData.end ())
          << "Unknown building type: " << b->GetType ();

      const unsigned dist
          = HexCoord::DistanceL1 (c->GetPosition (), b->GetCentre ());
      if (dist > mit->second.enter_radius ())
        {
          /* This is probably the most common case, no log spam here.  */
          continue;
        }

      LOG (INFO)
          << "Character " << c->GetId () << " is entering " << buildingId;
      ++entered;

      c->SetBuildingId (buildingId);
      c->MutableTarget ().clear_id ();
      c->SetEnterBuilding (Database::EMPTY_ID);
      StopCharacter (*c);
      StopMining (*c);
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

  const auto& data = RoConfigData ().building_types ();
  const auto mit = data.find (b->GetType ());
  CHECK (mit != data.end ()) << "Unknown building type: " << b->GetType ();

  const auto pos
      = ChooseSpawnLocation (b->GetCentre (), mit->second.enter_radius (),
                             c.GetFaction (), rnd, dyn, ctx.Map ());

  LOG (INFO)
      << "Character " << c.GetId ()
      << " is leaving building " << b->GetId ()
      << " to location " << pos;
  c.SetPosition (pos);
}

} // namespace pxd
