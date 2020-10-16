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

#include "buildings.hpp"
#include "prospecting.hpp"
#include "services.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/inventory.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"
#include "proto/roconfig.hpp"

#include <glog/logging.h>

namespace pxd
{

namespace
{

/**
 * Updates an item construction operation.
 */
void
UpdateItemConstruction (OngoingOperation& op, Building& b, const Context& ctx,
                        BuildingInventoriesTable& buildingInv)
{
  const auto& c = op.GetProto ().item_construction ();
  auto inv = buildingInv.Get (b.GetId (), c.account ());

  /* If this was constructed from blueprint copies, it will be done immediately
     and all items are given out.  Otherwise, we keep constructing one by
     one and schedule new updates for when the next item is done.  */
  Quantity finished;
  if (c.has_original_type ())
    finished = 1;
  else
    finished = c.num_items ();

  CHECK_LE (finished, c.num_items ());
  const Quantity remaining = c.num_items () - finished;

  LOG (INFO)
      << c.account () << " constructed "
      << finished << " " << c.output_type ()
      << " in building " << b.GetId ()
      << ", " << remaining << " units remaining in the queue";
  inv->GetInventory ().AddFungibleCount (c.output_type (), finished);

  if (remaining > 0)
    {
      CHECK (c.has_original_type ());
      const unsigned duration = GetConstructionBlocks (c.output_type (), ctx);
      op.SetHeight (ctx.Height () + duration);
      op.MutableProto ().mutable_item_construction ()
          ->set_num_items (remaining);
    }
  else if (c.has_original_type ())
    inv->GetInventory ().AddFungibleCount (c.original_type (), 1);
}

/**
 * Finishes construction of the given building.
 */
void
FinishBuildingConstruction (Building& b, const Context& ctx,
                            BuildingInventoriesTable& buildingInv)
{
  CHECK (b.GetProto ().foundation ())
      << "Building " << b.GetId () << " is not a foundation";
  const auto& roData = ctx.RoConfig ().Building (b.GetType ());
  CHECK (roData.has_construction ())
      << "Building type " << b.GetType () << " is not constructible";

  LOG (INFO)
      << "Construction of building " << b.GetId ()
      << " owned by " << b.GetOwner () << " is finished";

  auto& pb = b.MutableProto ();
  Inventory cInv(*pb.mutable_construction_inventory ());
  for (const auto& entry : roData.construction ().full_building ())
    cInv.AddFungibleCount (entry.first, -static_cast<Quantity> (entry.second));

  /* All resources not used for the actual construction go to the owner's
     account inside the new building.  */
  auto inv = buildingInv.Get (b.GetId (), b.GetOwner ());
  inv->GetInventory () += cInv;

  pb.clear_construction_inventory ();
  pb.set_foundation (false);

  UpdateBuildingStats (b, ctx.Chain ());
}

} // anonymous namespace

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

        case proto::OngoingOperation::kItemConstruction:
          CHECK (b != nullptr);
          UpdateItemConstruction (*op, *b, ctx, buildingInv);
          break;

        case proto::OngoingOperation::kBuildingConstruction:
          CHECK (b != nullptr);
          FinishBuildingConstruction (*b, ctx, buildingInv);
          break;

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
