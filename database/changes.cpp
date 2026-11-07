/*
    GSP for the Taurion blockchain game
    Copyright (C) 2026  Autonomous Worlds Ltd

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

#include "changes.hpp"

#include "dex.hpp"

#include <glog/logging.h>

#include <sqlite3.h>

#include <map>
#include <vector>

namespace pxd
{

namespace
{

/**
 * Maximum number of blocks whose changes can be read.  This is the undo data
 * kept with the pruning tauriond is run with, and for more blocks, a full
 * read of the state would not be much worse anyway.
 */
constexpr int64_t MAX_BLOCKS = 1'000;

/**
 * Converts an SQLite value from a changeset to the given type.
 */
template <typename T>
  T
  FromValue (sqlite3_value* val)
{
  return static_cast<T> (sqlite3_value_int64 (val));
}

template <>
  std::string
  FromValue<std::string> (sqlite3_value* val)
{
  return reinterpret_cast<const char*> (sqlite3_value_text (val));
}

/**
 * Helper class that goes through the changesets of blocks and collects
 * the changed keys from their rows.
 */
class ChangesReader
{

private:

  /** The database to read from.  */
  Database& db;

  /** The keys being collected.  */
  ChangedKeys& keys;

  /** IDs of changed DEX orders.  */
  std::set<Database::IdT> orders;

  /** Indices of columns by table and name, filled in as needed.  */
  std::map<std::string, std::map<std::string, int>> columns;

  /** Changeset iterator at the current row.  */
  sqlite3_changeset_iter* it;

  /** The current row's table.  */
  std::string table;

  /** The current row's operation (insert, update or delete).  */
  int op;

  /**
   * Returns the values of the given column in the current row, i.e. the old
   * one for an update or deletion and the new one for an insertion or update.
   * Values not changed by an update are not part of the row (except for the
   * primary key), and NULL values are skipped as well.
   */
  std::vector<sqlite3_value*> Values (const std::string& col);

  /**
   * Adds all values of a column in the current row to the set.
   */
  template <typename T>
    void
    Add (const std::string& col, std::set<T>& out)
  {
    for (auto* val : Values (col))
      out.insert (FromValue<T> (val));
  }

  /**
   * Returns the value of a primary-key column in the current row.
   */
  template <typename T>
    T
    Key (const std::string& col)
  {
    const auto vals = Values (col);
    CHECK_EQ (vals.size (), 1) << "Column " << col << " is not a key";
    return FromValue<T> (vals.front ());
  }

  /**
   * Adds the keys from the current row.
   */
  void AddRow ();

public:

  explicit ChangesReader (Database& d, ChangedKeys& k)
    : db(d), keys(k)
  {}

  ChangesReader () = delete;
  ChangesReader (const ChangesReader&) = delete;
  void operator= (const ChangesReader&) = delete;

  /**
   * Adds the keys from a block's changeset.
   */
  void AddChangeset (const std::string& data);

  /**
   * Adds the keys from DEX orders that were changed and still exist, which
   * are not known from the changesets if they did not change.
   */
  void Finish ();

};

std::vector<sqlite3_value*>
ChangesReader::Values (const std::string& col)
{
  auto& indices = columns[table];
  if (indices.empty ())
    {
      auto stmt = (*db).PrepareRo (R"(
        SELECT `cid`, `name` FROM pragma_table_info (?1)
      )");
      stmt.Bind (1, table);
      while (stmt.Step ())
        indices.emplace (stmt.Get<std::string> (1), stmt.Get<int> (0));
    }
  const auto mit = indices.find (col);
  CHECK (mit != indices.end ()) << "Table " << table << " has no " << col;

  std::vector<sqlite3_value*> res;
  const auto add = [&res] (sqlite3_value* val)
    {
      if (val != nullptr && sqlite3_value_type (val) != SQLITE_NULL)
        res.push_back (val);
    };

  sqlite3_value* val;
  if (op != SQLITE_INSERT)
    {
      CHECK_EQ (sqlite3changeset_old (it, mit->second, &val), SQLITE_OK);
      add (val);
    }
  if (op != SQLITE_DELETE)
    {
      CHECK_EQ (sqlite3changeset_new (it, mit->second, &val), SQLITE_OK);
      add (val);
    }

  return res;
}

void
ChangesReader::AddRow ()
{
  if (table == "accounts")
    Add ("name", keys.accounts);
  else if (table == "buildings")
    Add ("id", keys.buildings);
  else if (table == "building_inventories")
    Add ("building", keys.buildings);
  else if (table == "characters")
    Add ("id", keys.characters);
  else if (table == "damage_lists")
    Add ("victim", keys.characters);
  else if (table == "dex_orders")
    {
      /* Orders are shown with their building, and bids in the reserved coins
         of their account.  Neither ever changes for an order, so the rows of
         an update do not contain them; they are looked up in Finish then.  */
      Add ("id", orders);
      Add ("building", keys.buildings);
      Add ("account", keys.accounts);
    }
  else if (table == "ground_loot")
    keys.groundLoot.emplace (Key<HexCoord::IntT> ("x"),
                             Key<HexCoord::IntT> ("y"));
  else if (table == "ongoing_operations")
    Add ("id", keys.ongoings);
  else if (table == "regions")
    Add ("id", keys.regions);
}

void
ChangesReader::AddChangeset (const std::string& data)
{
  CHECK_EQ (sqlite3changeset_start (&it, data.size (),
                                    const_cast<char*> (data.data ())),
            SQLITE_OK);

  int rc;
  while ((rc = sqlite3changeset_next (it)) == SQLITE_ROW)
    {
      const char* tbl;
      int numCols;
      CHECK_EQ (sqlite3changeset_op (it, &tbl, &numCols, &op, nullptr),
                SQLITE_OK);
      table = tbl;
      AddRow ();
    }
  CHECK_EQ (rc, SQLITE_DONE);

  CHECK_EQ (sqlite3changeset_finalize (it), SQLITE_OK);
}

void
ChangesReader::Finish ()
{
  DexOrderTable dex(db);
  for (const auto id : orders)
    {
      const auto o = dex.GetById (id);
      if (o != nullptr)
        {
          keys.buildings.insert (o->GetBuilding ());
          keys.accounts.insert (o->GetAccount ());
        }
    }
}

} // anonymous namespace

bool
ChangedKeys::ReadSince (Database& db, const xaya::uint256& block,
                        const unsigned height)
{
  /* libxayagame removes the undo data of detached blocks, so the remaining
     rows are those of the current chain in the order they were attached,
     which is the order of their rowid.  Thus the rows after the one of the
     given block are those of all following blocks; we verify their heights
     to be sure.  */

  auto stmt = (*db).PrepareRo (R"(
    SELECT `rowid`, `height`
      FROM `xayagame_undo`
      WHERE `hash` = ?1
  )");
  stmt.Bind (1, block);
  if (!stmt.Step ())
    return false;

  int64_t next = stmt.Get<int64_t> (1) + 1;
  if (next > height + 1 || height + 1 - next > MAX_BLOCKS)
    return false;

  auto rows = (*db).PrepareRo (R"(
    SELECT `height`, `data`
      FROM `xayagame_undo`
      WHERE `rowid` > ?1
      ORDER BY `rowid`
  )");
  rows.Bind (1, stmt.Get<int64_t> (0));

  ChangesReader reader(db, *this);
  for (; rows.Step (); ++next)
    {
      if (rows.Get<int64_t> (0) != next)
        return false;
      reader.AddChangeset (rows.GetBlob (1));
    }
  if (next != height + 1)
    return false;

  reader.Finish ();
  return true;
}

} // namespace pxd
