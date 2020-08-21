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

#include "moneysupply.hpp"

#include <glog/logging.h>

namespace pxd
{

namespace
{

struct MoneySupplyResult : public Database::ResultType
{
  RESULT_COLUMN (std::string, key, 1);
  RESULT_COLUMN (int64_t, amount, 2);
};

} // anonymous namespace

Amount
MoneySupply::Get (const std::string& key)
{
  auto stmt = db.Prepare (R"(
    SELECT *
      FROM `money_supply`
      WHERE `key` = ?1
  )");
  stmt.Bind (1, key);

  auto res = stmt.Query<MoneySupplyResult> ();
  CHECK (res.Step ()) << "Invalid key: " << key;

  const Amount amount = res.Get<MoneySupplyResult::amount> ();
  CHECK (!res.Step ());

  return amount;
}

void
MoneySupply::Increment (const std::string& key, const Amount value)
{
  VLOG (1)
      << "Incrementing money supply for key " << key
      << " by " << value;
  CHECK_NE (GetValidKeys ().count (key), 0) << "Invalid key: " << key;
  CHECK_GT (value, 0);

  auto stmt = db.Prepare (R"(
    UPDATE `money_supply`
      SET `amount` = `amount` + ?2
      WHERE `key` = ?1
  )");
  stmt.Bind (1, key);
  stmt.Bind (2, value);
  stmt.Execute ();
}

void
MoneySupply::InitialiseDatabase ()
{
  auto stmt = db.Prepare (R"(
    INSERT INTO `money_supply`
      (`key`, `amount`) VALUES (?1, ?2)
  )");

  for (const auto& k : GetValidKeys ())
    {
      stmt.Reset ();
      stmt.Bind (1, k);
      stmt.Bind (2, 0);
      stmt.Execute ();
    }
}

const std::set<std::string>&
MoneySupply::GetValidKeys ()
{
  static const std::set<std::string> keys =
    {
      "burnsale",
      "gifted",
    };
  return keys;
}

} // namespace pxd
