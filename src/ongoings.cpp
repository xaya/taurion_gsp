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

#include "ongoings.hpp"

#include "prospecting.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/inventory.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"

#include <glog/logging.h>

namespace pxd
{

void
ProcessAllOngoings (Database& db, xaya::Random& rnd, const Context& ctx)
{
  LOG (INFO) << "Processing ongoing operations for height " << ctx.Height ();

  BuildingsTable buildings(db);
  BuildingInventoriesTable buildingInv(db);
  CharacterTable characters(db);
  OngoingsTable ongoings(db);
  RegionsTable regions(db, ctx.Height ());

  auto res = ongoings.QueryForHeight (ctx.Height ());
  while (res.Step ())
    {
      auto op = ongoings.GetFromResult (res);

      /* The query returns all entries with height less-or-equal to the
         current one, but there shouldn't be any with less (as they should have
         been processed already last block).  Enforce this.  */
      CHECK_EQ (op->GetHeight (), ctx.Height ());

      CharacterTable::Handle c;
      if (op->GetCharacterId () != Database::EMPTY_ID)
        c = characters.GetById (op->GetCharacterId ());

      BuildingsTable::Handle b;
      if (op->GetBuildingId () != Database::EMPTY_ID)
        b = buildings.GetById (op->GetBuildingId ());

      switch (op->GetProto ().op_case ())
        {
        case proto::OngoingOperation::kProspection:
          CHECK (c != nullptr);
          FinishProspecting (*c, db, regions, rnd, ctx);
          c->MutableProto ().clear_ongoing ();
          break;

        case proto::OngoingOperation::kArmourRepair:
          CHECK (c != nullptr);
          LOG (INFO) << "Finished armour repair of character " << c->GetId ();
          c->MutableHP ().set_armour (c->GetRegenData ().max_hp ().armour ());
          c->MutableProto ().clear_ongoing ();
          break;

        case proto::OngoingOperation::kBlueprintCopy:
          {
            CHECK (b != nullptr);
            const auto& cp = op->GetProto ().blueprint_copy ();
            LOG (INFO)
                << "Finished blue-print copy of " << cp.account ()
                << " in building " << b->GetId ();
            auto inv = buildingInv.Get (b->GetId (), cp.account ());
            inv->GetInventory ().AddFungibleCount (cp.original_type (), 1);
            inv->GetInventory ().AddFungibleCount (cp.copy_type (),
                                                   cp.num_copies ());
            break;
          }

        default:
          LOG (FATAL)
              << "Unexpected operation case: " << op->GetProto ().op_case ();
        }

      if (c != nullptr)
        CHECK (!c->IsBusy ());
    }

  ongoings.DeleteForHeight (ctx.Height ());
}

} // namespace pxd
