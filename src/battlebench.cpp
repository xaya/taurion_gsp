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

/* Offline benchmark of block processing on a copy of a real game-state
   database:  optionally stages a battle (fighters of several factions moved
   into one area) and movers, then processes super blocks without moves
   exactly as tauriond does (one transaction and one undo changeset per
   block) and logs the time of each processing phase.  */

#include "context.hpp"
#include "fitments.hpp"
#include "gamestatejson.hpp"
#include "logic.hpp"
#include "movement.hpp"
#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/faction.hpp"
#include "hexagonal/coord.hpp"
#include "mapdata/basemap.hpp"

#include <xayagame/sqlitestorage.hpp>
#include <xayautil/hash.hpp>
#include <xayautil/random.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <json/json.h>

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

DECLARE_bool (pxd_phase_timing);

DEFINE_string (db, "", "State database to process (it is modified)");
DEFINE_int32 (blocks, 10, "Number of super blocks to process");
DEFINE_int32 (per_faction, 0,
              "Fighters per faction to move into the battle area");
DEFINE_int32 (factions, 2, "Number of factions in the battle");
DEFINE_int32 (radius, 10, "L1 radius of the battle area");
DEFINE_int32 (cx, 0, "x of the battle centre");
DEFINE_int32 (cy, 0, "y of the battle centre");
DEFINE_int32 (movers, 0, "Other characters to set moving");
DEFINE_string (fit, "",
               "Comma-separated fitments that replace the fighters' own"
               " (e.g. 'lf laser' for an AoE battle)");
DEFINE_bool (session, true, "Record an undo changeset per block");
DEFINE_bool (json, false, "Time the full characters JSON after each block");
DEFINE_int32 (findpath_threads, 0,
              "Threads running findpath calls during processing");
DEFINE_int32 (findpath_l1range, 4000, "l1range of those findpath calls");

namespace pxd
{

/** Gives the benchmark access to PXLogic's block processing.  */
class BattleBench
{
public:

  static void
  UpdateState (Database& db, xaya::Random& rnd, const xaya::Chain chain,
               const BaseMap& map, const Json::Value& blockData)
  {
    PXLogic::UpdateState (db, rnd, chain, map, blockData);
  }

};

namespace
{

using Clock = std::chrono::steady_clock;

double
MsSince (const Clock::time_point start)
{
  const std::chrono::duration<double, std::milli> d = Clock::now () - start;
  return d.count ();
}

/**
 * Database on a state file, with IDs continuing from its stored AutoId.
 */
class BenchDatabase : public Database
{

private:

  xaya::SQLiteDatabase db;
  IdT nextId;
  IdT nextLogId = 1;

public:

  explicit BenchDatabase (const std::string& file)
    : db(file, SQLITE_OPEN_READWRITE)
  {
    SetDatabase (db);

    auto stmt = db.Prepare (R"(
      SELECT `nextid` FROM `xayagame_autoids` WHERE `key` = 'pxd'
    )");
    CHECK (stmt.Step ());
    nextId = stmt.Get<int64_t> (0);
  }

  IdT
  GetNextId () override
  {
    return nextId++;
  }

  IdT
  GetLogId () override
  {
    return nextLogId++;
  }

};

/**
 * Returns the IDs of up to n characters of a faction that are on the map
 * and idle (not busy and not mining), ordered by ID.
 */
std::vector<Database::IdT>
IdleCharacters (CharacterTable& tbl, const Faction f, const unsigned n,
                const std::set<Database::IdT>& exclude)
{
  std::vector<Database::IdT> res;
  auto q = tbl.QueryAll ();
  while (res.size () < n && q.Step ())
    {
      auto c = tbl.GetFromResult (q);
      if (c->GetFaction () != f || c->IsInBuilding () || c->IsBusy ()
            || c->GetProto ().mining ().active ()
            || exclude.count (c->GetId ()) > 0)
        continue;
      res.push_back (c->GetId ());
    }
  return res;
}

/**
 * Moves fighters of the first --factions factions into the battle area,
 * spread over its free tiles (interleaved by faction), and sets --movers
 * other characters moving.
 */
void
StageScenario (Database& db, const Context& ctx)
{
  CharacterTable tbl(db);
  std::set<Database::IdT> used;

  if (FLAGS_per_faction > 0)
    {
      const HexCoord centre(FLAGS_cx, FLAGS_cy);
      std::vector<HexCoord> tiles;
      for (int x = -FLAGS_radius; x <= FLAGS_radius; ++x)
        for (int y = -FLAGS_radius; y <= FLAGS_radius; ++y)
          {
            const HexCoord c(centre.GetX () + x, centre.GetY () + y);
            if (HexCoord::DistanceL1 (c, centre) > FLAGS_radius)
              continue;
            if (!ctx.Map ().IsPassable (c)
                  || ctx.Map ().SafeZones ().IsNoCombat (c))
              continue;
            tiles.push_back (c);
          }
      CHECK (!tiles.empty ());

      const Faction factions[] = {Faction::RED, Faction::GREEN, Faction::BLUE};
      CHECK_LE (FLAGS_factions, 3);
      std::vector<std::vector<Database::IdT>> ids;
      for (int i = 0; i < FLAGS_factions; ++i)
        ids.push_back (IdleCharacters (tbl, factions[i],
                                       FLAGS_per_faction, used));

      std::vector<std::string> fitments;
      std::istringstream fitIn(FLAGS_fit);
      for (std::string f; std::getline (fitIn, f, ',');)
        fitments.push_back (f);

      /* Stride through the tiles by a prime so the factions mix.  */
      constexpr size_t stride = 7919;
      size_t n = 0;
      for (int k = 0; k < FLAGS_per_faction; ++k)
        for (const auto& lst : ids)
          if (k < static_cast<int> (lst.size ()))
            {
              auto c = tbl.GetById (lst[k]);
              StopCharacter (*c);
              c->SetPosition (tiles[(n++ * stride) % tiles.size ()]);
              if (!fitments.empty ())
                {
                  auto& pb = c->MutableProto ();
                  pb.clear_fitments ();
                  for (const auto& f : fitments)
                    pb.add_fitments (f);
                  DeriveCharacterStats (*c, ctx);
                }
              used.insert (lst[k]);
            }
      LOG (INFO)
          << "Placed " << n << " fighters on " << tiles.size ()
          << " tiles around " << centre;
    }

  if (FLAGS_movers > 0)
    {
      /* Movers walk 1000 tiles along a principal direction (they stop at
         the first obstacle, as in real play).  */
      int n = 0;
      for (const auto f : {Faction::RED, Faction::GREEN, Faction::BLUE})
        for (const auto id : IdleCharacters (tbl, f, FLAGS_movers / 3, used))
          {
            auto c = tbl.GetById (id);
            const HexCoord target = c->GetPosition ()
                + HexCoord (0, (id % 2 == 0) ? 1000 : -1000);
            StopCharacter (*c);
            auto* mv = c->MutableProto ().mutable_movement ();
            *mv->add_waypoints () = CoordToProto (target);
            ++n;
          }
      LOG (INFO) << "Set " << n << " movers";
    }
}

/**
 * Counts what a block left behind, for the per-block log line.
 */
std::string
CountState (Database& db)
{
  auto stmt = (*db).Prepare (R"(
    SELECT
      (SELECT COUNT (*) FROM `characters`),
      (SELECT COUNT (*) FROM `characters` WHERE `target` IS NOT NULL),
      (SELECT COUNT (*) FROM `characters` WHERE `ismoving`)
  )");
  CHECK (stmt.Step ());
  std::ostringstream out;
  out << "characters " << stmt.Get<int64_t> (0)
      << " targeting " << stmt.Get<int64_t> (1)
      << " moving " << stmt.Get<int64_t> (2);
  return out.str ();
}

/**
 * Runs findpath computations like the RPC method (base map only) between
 * points of a 1000-tile square until stop is set, as the fork's stress
 * driver does with --l1range.  Returns the number of calls in calls.
 */
void
FindPathLoad (const BaseMap& map, const int seed, std::atomic<bool>& stop,
              std::atomic<unsigned>& calls)
{
  unsigned n = seed;
  while (!stop)
    {
      n = n * 1103515245 + 12345;
      const HexCoord source(1000 + (n >> 8) % 1000, 1000 + (n >> 18) % 1000);
      const HexCoord target(1000 + (n >> 4) % 1000, 1000 + (n >> 14) % 1000);
      PathFinder finder(target);
      const auto edges = [&] (const HexCoord& from, const HexCoord& to)
        {
          return MovementEdgeWeight (map, Faction::RED, from, to);
        };
      finder.Compute (edges, source, FLAGS_findpath_l1range);
      ++calls;
    }
}

void
Run ()
{
  CHECK (!FLAGS_db.empty ()) << "--db is required";
  FLAGS_pxd_phase_timing = true;

  const auto chain = xaya::Chain::POLYGON;
  const BaseMap map(chain);
  BenchDatabase db(FLAGS_db);

  unsigned sbHeight;
  int64_t sbTimestamp;
  CHECK (db.LastSuperBlock (sbHeight, sbTimestamp));
  unsigned height;
  {
    auto stmt = (*db).Prepare ("SELECT MAX (`height`) FROM `xayagame_undo`");
    CHECK (stmt.Step ());
    height = stmt.Get<int64_t> (0);
  }
  LOG (INFO)
      << "State at block " << height << ", super block " << sbHeight;

  (*db).Execute ("BEGIN");
  {
    const Context ctx(db, chain, map, sbHeight + 1, height + 1, sbTimestamp);
    StageScenario (db, ctx);
  }
  (*db).Execute ("COMMIT");

  std::atomic<bool> stop(false);
  std::atomic<unsigned> calls(0);
  std::vector<std::thread> load;
  for (int i = 0; i < FLAGS_findpath_threads; ++i)
    load.emplace_back ([&map, &stop, &calls, i] ()
      {
        FindPathLoad (map, i, stop, calls);
      });
  const auto loadStart = Clock::now ();

  for (int i = 0; i < FLAGS_blocks; ++i)
    {
      ++height;
      sbTimestamp += 5;

      Json::Value blockData(Json::objectValue);
      blockData["block"]["height"] = static_cast<Json::UInt64> (height);
      blockData["block"]["timestamp"] = static_cast<Json::Int64> (sbTimestamp);
      blockData["moves"] = Json::Value (Json::arrayValue);
      blockData["admin"] = Json::Value (Json::arrayValue);

      xaya::SHA256 seed;
      seed << "battlebench " << std::to_string (height);
      xaya::Random rnd;
      rnd.Seed (seed.Finalise ());

      const auto start = Clock::now ();
      (*db).Execute ("BEGIN");
      sqlite3_session* session = nullptr;
      if (FLAGS_session)
        (*db).AccessDatabase ([&session] (sqlite3* h)
          {
            CHECK_EQ (sqlite3session_create (h, "main", &session), SQLITE_OK);
            CHECK_EQ (sqlite3session_attach (session, nullptr), SQLITE_OK);
          });

      BattleBench::UpdateState (db, rnd, chain, map, blockData);
      const double processMs = MsSince (start);

      auto t = Clock::now ();
      int changesetBytes = 0;
      if (session != nullptr)
        {
          void* data;
          CHECK_EQ (sqlite3session_changeset (session, &changesetBytes, &data),
                    SQLITE_OK);
          sqlite3_free (data);
          sqlite3session_delete (session);
        }
      const double changesetMs = MsSince (t);

      t = Clock::now ();
      (*db).Execute ("COMMIT");
      const double commitMs = MsSince (t);

      LOG (INFO)
          << "Block " << height << ": process " << processMs
          << " ms, changeset " << changesetMs << " ms (" << changesetBytes
          << " B), commit " << commitMs << " ms, total " << MsSince (start)
          << " ms; " << CountState (db);

      if (FLAGS_json)
        {
          t = Clock::now ();
          const Context ctx(db, chain, map, sbHeight + 1, height, sbTimestamp);
          GameStateJson gsj(db, ctx);
          const auto chars = gsj.Characters ();
          const double buildMs = MsSince (t);
          Json::StreamWriterBuilder wbuilder;
          wbuilder["indentation"] = "";
          const auto str = Json::writeString (wbuilder, chars);
          LOG (INFO)
              << "Characters JSON: build " << buildMs << " ms, total "
              << MsSince (t) << " ms, " << str.size () << " B";
        }
    }

  stop = true;
  for (auto& t : load)
    t.join ();
  if (!load.empty ())
    LOG (INFO)
        << "Set " << calls << " findpath calls in "
        << MsSince (loadStart) / 1'000 << " s";
}

} // anonymous namespace
} // namespace pxd

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);
  gflags::ParseCommandLineFlags (&argc, &argv, true);
  pxd::Run ();
  return 0;
}
