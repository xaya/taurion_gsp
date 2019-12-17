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

#include "banking_tests.hpp"

#include "testutils.hpp"

#include "database/account.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"

#include <gtest/gtest.h>

namespace pxd
{

const HexCoord BANKING_POS(-175, 810);
const HexCoord NO_BANKING_POS(-176, 810);

namespace
{

class BankingTests : public DBTestWithSchema
{

protected:

  ContextForTesting ctx;

  AccountsTable accounts;
  CharacterTable characters;

  BankingTests ()
    : accounts(db), characters(db)
  {
    accounts.CreateNew ("domob", Faction::RED);
  }

};

TEST_F (BankingTests, TestPositions)
{
  EXPECT_TRUE (ctx.Params ().IsBankingArea (BANKING_POS));
  EXPECT_FALSE (ctx.Params ().IsBankingArea (NO_BANKING_POS));
  EXPECT_EQ (HexCoord::DistanceL1 (BANKING_POS, NO_BANKING_POS), 1);
}

TEST_F (BankingTests, ItemsBanked)
{
  accounts.GetByName ("domob")->GetBanked ().AddFungibleCount ("foo", 10);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (BANKING_POS);
  c->MutableProto ().set_cargo_space (100);
  c->GetInventory ().SetFungibleCount ("foo", 1);
  c->GetInventory ().SetFungibleCount ("bar", 2);
  c.reset ();

  ProcessBanking (db, ctx);

  EXPECT_TRUE (characters.GetById (id)->GetInventory ().IsEmpty ());
  auto a = accounts.GetByName ("domob");
  EXPECT_EQ (a->GetBankingPoints (), 0);
  EXPECT_EQ (a->GetBanked ().GetFungibleCount ("foo"), 11);
  EXPECT_EQ (a->GetBanked ().GetFungibleCount ("bar"), 2);
}

TEST_F (BankingTests, OutsideBankingArea)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c->SetPosition (NO_BANKING_POS);
  c->MutableProto ().set_cargo_space (100);
  c->GetInventory ().SetFungibleCount ("foo", 3);
  c.reset ();

  ProcessBanking (db, ctx);

  EXPECT_TRUE (accounts.GetByName ("domob")->GetBanked ().IsEmpty ());
  EXPECT_EQ (characters.GetById (id)->GetInventory ().GetFungibleCount ("foo"),
             3);
}

TEST_F (BankingTests, BankingPoints)
{
  auto a = accounts.GetByName ("domob");
  a->AddBankingPoints (10);
  a->GetBanked ().AddFungibleCount ("raw b", 1'000);
  a->GetBanked ().AddFungibleCount ("raw c", 1'000);
  a->GetBanked ().AddFungibleCount ("raw d", 1'000);
  a->GetBanked ().AddFungibleCount ("raw e", 1'000);
  a->GetBanked ().AddFungibleCount ("raw f", 1'000);
  a->GetBanked ().AddFungibleCount ("raw g", 1'000);
  a->GetBanked ().AddFungibleCount ("raw h", 1'000);
  a.reset ();

  auto c = characters.CreateNew ("domob", Faction::RED);
  c->SetPosition (BANKING_POS);
  c->MutableProto ().set_cargo_space (5'000);
  c->GetInventory ().SetFungibleCount ("raw a", 45);
  c->GetInventory ().SetFungibleCount ("raw i", 98);
  c.reset ();

  ProcessBanking (db, ctx);

  a = accounts.GetByName ("domob");
  EXPECT_EQ (a->GetBankingPoints (), 12);
  EXPECT_EQ (a->GetBanked ().GetFungibleCount ("raw a"), 5);
  EXPECT_EQ (a->GetBanked ().GetFungibleCount ("raw b"), 960);
  EXPECT_EQ (a->GetBanked ().GetFungibleCount ("raw i"), 58);
}

} // anonymous namespace
} // namespace pxd
