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

#include "banking.hpp"

#include "database/account.hpp"
#include "database/character.hpp"
#include "database/inventory.hpp"

#include <glog/logging.h>

#include <vector>

namespace pxd
{

namespace
{

/**
 * Processes banking for a single character (for which we already know that
 * it is in a banking area).
 */
void
ProcessBanking (AccountsTable& accounts, const Context& ctx, Character& c)
{
  if (c.GetInventory ().IsEmpty ())
    return;

  const auto& nm = c.GetOwner ();
  LOG (INFO)
      << "Banking non-empty inventory of character " << c.GetId ()
      << " at " << c.GetPosition () << " for user " << nm;

  auto a = accounts.GetByName (nm);
  auto& banked = a->GetBanked ();

  /* While "moving" the inventory over to the banked one, we cannot right away
     modify (i.e. clear) the character inventory as that would invalidate the
     map we are iterating over.  Thus we keep track of all item types that were
     there, so that we can clear them in a later step.  */
  std::vector<std::string> types;
  for (const auto& entry : c.GetInventory ().GetFungible ())
    {
      VLOG (1) << "Banking " << entry.second << " of " << entry.first;
      banked.AddFungibleCount (entry.first, entry.second);
      types.push_back (entry.first);
    }
  for (const auto& t : types)
    c.GetInventory ().SetFungibleCount (t, 0);
  CHECK (c.GetInventory ().IsEmpty ());

  /* Try to find more completed "resource sets".  */
  const auto& setData = ctx.Params ().BankingSet ();
  int setsPossible = -1;
  for (const auto& entry : setData)
    {
      const int possibleForThis
          = banked.GetFungibleCount (entry.first) / entry.second;
      if (setsPossible == -1 || setsPossible >= possibleForThis)
        setsPossible = possibleForThis;
      CHECK_GE (setsPossible, 0);
      if (setsPossible == 0)
        break;
    }
  CHECK_GE (setsPossible, 0);

  if (setsPossible > 0)
    {
      VLOG (1)
          << "User " << nm << " has " << setsPossible
          << " more banking-sets completed";
      a->AddBankingPoints (setsPossible);
      for (const auto& entry : setData)
        {
          const Inventory::QuantityT reduced = setsPossible * entry.second;
          const auto old = banked.GetFungibleCount (entry.first);
          CHECK_GE (old, reduced);
          banked.SetFungibleCount (entry.first, old - reduced);
        }
    }
}

} // anonymous namespace

void
ProcessBanking (Database& db, const Context& ctx)
{
  AccountsTable accounts(db);
  CharacterTable characters(db);

  characters.ProcessAllPositions ([&] (const Database::IdT id,
                                       const HexCoord& pos, const Faction f)
    {
      if (!ctx.Params ().IsBankingArea (pos))
        return;

      auto c = characters.GetById (id);
      ProcessBanking (accounts, ctx, *c);
    });
}

} // namespace pxd
