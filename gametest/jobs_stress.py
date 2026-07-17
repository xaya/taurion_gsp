#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020-2021  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""
Consensus fan-out stress recipe for the jobs board: the in-repo,
reproducible evidence behind the deliberately uncapped settlement sweeps
(see the ExpireJobs notes in src/jobs.hpp).  All four unbounded-cohort
paths run at material size through the REAL chain path, and each settling
block is mined INSIDE a bounded, GSP-synced timing:

  1. aligned expiry: N transports sharing one deadline settle in ONE sweep,
     then the whole sweep is replayed across a reorg (undo + re-settle);
  2. standing bounty stack: M wanted pools on one name all pay on ONE kill;
  3. linked-entity stack: M accepted bodyguards on one character all settle
     on that same kill (both hooks fire in the same timed block);
  4. retention prune: the whole settled-history cohort deletes in ONE block
     once the retention window passes (the standing pools survive it),
     also replayed across a reorg.

Every cohort row is paid (escrowed reward + burned posting fee), which is
the economic bound on an attacker lining these up.  Cohort sizes scale
with the JOBS_STRESS_N environment variable (the default keeps CI fast);
every bulk phase -- posts, assignments and acceptances -- rides in
sub-ceiling move chunks, so scaled runs construct the same shapes.

Recorded runs (make check TESTS=jobs_stress.py, regtest superblock_seconds
5, AMD Threadripper 7970X, 2026-07-17; timings include the mine + GSP sync
round-trip, which dominates -- per-row settlement cost is far below it):

  JOBS_STRESS_N=200 (default):  sweep 200 in 0.102s; one kill settling
    25 pools + 25 bodyguards in 0.103s; prune 225 rows in 0.103s.
  JOBS_STRESS_N=1000 (5x):      sweep 1000 in 0.103s; kill 125+125 in
    0.204s; prune 1125 rows in 0.102s.
  JOBS_STRESS_N=2200 (11x):     sweep 2200 in 0.102s; kill 275+275 in
    0.103s; prune 2475 rows in 0.103s.  The 2200-row live board also
    walks the paged reader past its 2000-row page cap.

All three PASS with flat wall times: the block budget is bounded by the
RPC round-trip, not the cohort size, at every measured scale.
"""

import os
import time

from pxtest import PXTest

N_ALIGNED = int (os.getenv ("JOBS_STRESS_N", "200"))
M_STACK = max (10, N_ALIGNED // 8)

# The test chain's Xaya X layer silently drops move values above ~1670
# bytes, so every bulk phase rides in sub-ceiling chunks of one move per
# block.  Deadlines then differ by a few seconds per block, which does not
# matter: the sweep settles EVERYTHING due at once, so a single warp past
# the last deadline still lands the whole cohort in one ExpireJobs call.
OPS_CHUNK = 15


class JobsStressTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def timed (self, label, fn, bound=60):
    """Runs fn, logs its wall time and asserts a deliberately generous
    upper bound: expected times are milliseconds, so the bound only trips
    on a complexity-class regression, not on machine noise."""
    start = time.time ()
    fn ()
    duration = time.time () - start
    self.mainLogger.info ("%s took %.3fs" % (label, duration))
    assert duration < bound, \
        "%s took %.3fs, over the %ds bound" % (label, duration, bound)

  def mineAndSync (self, superblocks=True):
    """Mines one block and waits for the GSP to process it: the sync
    barrier that makes the timed callables cover GSP processing."""
    self.generate (1, superblocks=superblocks)
    self.syncGame ()

  def sendJobOps (self, name, ops):
    """Sends the given j-ops in sub-ceiling chunks, one move per block."""
    for i in range (0, len (ops), OPS_CHUNK):
      self.sendMove (name, {"j": ops[i : i + OPS_CHUNK]})
      self.generate (1)

  def postMany (self, name, op, count):
    """Posts `count` copies of the given job op, in sub-ceiling chunks."""
    self.sendJobOps (name, [op] * count)

  def undoTo (self, snap, preTime):
    """Restores the chain snapshot and outbuilds the abandoned settling
    branch with ordinary blocks (timestamps that trigger no sweep), forcing
    the GSP to undo everything that branch settled."""
    snap.restore ()
    for i in range (3):
      self.env.setMockTime (preTime + 1 + i)
      self.generate (1, superblocks=False)
    self.syncGame ()

  def run (self):
    self.mainLogger.info ("Setting up accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("hunter", "r")
    self.initAccount ("victim", "b")
    self.initAccount ("guard", "b")
    self.generate (1)
    self.giftCoins ({"poster": 1000000, "victim": 1000000,
                     "guard": 1000000})
    self.build ("checkmark", "poster", {"x": 0, "y": 0}, rot=0)
    bId = max (self.getBuildings ().keys ())

    self.mainLogger.info ("Cohort 1: %d aligned transports..." % N_ALIGNED)
    self.postMany ("poster", {
      "t": "transport", "d": 86400, "wd": 86400, "r": 100, "co": 0,
      "to": bId, "items": {"foo": 1},
    }, N_ALIGNED)
    board = self.getJobs ()
    self.assertEqual (len (board), N_ALIGNED)
    # The paged reader clamps an over-cap pageSize to the server's hard
    # page cap, so this walks identical pages even past 2000 rows.
    self.assertEqual (self.getJobs (pageSize=10**9), board)

    posterBefore = self.available ("poster")
    preSweepTime = self.w3.eth.get_block ("latest")["timestamp"]
    snap = self.env.snapshot ()
    deadline = max (j["deadline"] for j in board)
    self.env.setMockTime (deadline + 1)
    self.timed ("one sweep settling %d aligned jobs" % N_ALIGNED,
                lambda: self.mineAndSync (superblocks=False))
    self.assertEqual (self.getJobs (), [])
    # Every escrow refunded in that one superblock, fees already burned.
    self.assertEqual (self.available ("poster"),
                      posterBefore + 100 * N_ALIGNED)
    rows = self.historyRows ()
    self.assertEqual (len (rows), N_ALIGNED)
    self.assertEqual (set (e["outcome"] for e in rows), {"void"})

    self.mainLogger.info ("Reorg across the sweep: undo and re-settle...")
    self.undoTo (snap, preSweepTime)
    self.assertEqual (len (self.getJobs ()), N_ALIGNED)
    self.assertEqual (self.available ("poster"), posterBefore)
    self.assertEqual (self.historyRows (), [])
    self.env.setMockTime (deadline + 2)
    self.mineAndSync (superblocks=False)
    self.assertEqual (self.getJobs (), [])
    self.assertEqual (self.available ("poster"),
                      posterBefore + 100 * N_ALIGNED)

    self.mainLogger.info ("Cohorts 2+3: %d pools + %d bodyguards on one "
                          "victim..." % (M_STACK, M_STACK))
    self.createCharacters ("victim")
    self.createCharacters ("hunter")
    self.generate (1)
    victimChar = self.getCharacters ()["victim"].getId ()

    # M standing pools on the victim's name (quota 2, so one kill pays a
    # tranche of 50 from EVERY pool and none drains)...
    self.postMany ("poster", {
      "t": "wanted", "r": 100, "co": 0, "name": "victim", "n": 2,
    }, M_STACK)
    # ...plus M bodyguard jobs linked to the victim's character.
    self.postMany ("victim", {
      "t": "bodyguard", "d": 86400, "wd": 86400, "r": 100, "co": 500,
      "ch": victimChar,
    }, M_STACK)

    guardIds = [j["id"] for j in self.getJobs ()
                if j["type"] == "bodyguard"]
    self.assertEqual (len (guardIds), M_STACK)
    self.sendJobOps ("victim", [{"s": i, "w": "guard"} for i in guardIds])
    self.sendJobOps ("guard", [{"a": i} for i in guardIds])
    accepted = [j for j in self.getJobs () if j["type"] == "bodyguard"
                and j["state"] == "accepted"]
    self.assertEqual (len (accepted), M_STACK)

    self.mainLogger.info ("One kill settles every pool and bodyguard...")
    self.changeCharacterVehicle ("hunter", "light attacker")
    hunterBefore = self.available ("hunter")
    victimBefore = self.available ("victim")
    guardBefore = self.available ("guard")
    self.moveCharactersTo ({
      "victim": {"x": 60, "y": 0},
      "hunter": {"x": 60, "y": 0},
    })
    # The hunter acquired its target when the teleport block above was
    # processed (targets are selected at the END of a superblock, damage is
    # dealt at the START of the next).  Submit the god HP drop WITHOUT
    # mining, so the kill and its whole settlement fan-out happen inside
    # the ONE timed block below: alive before, dead after.
    assert "victim" in self.getCharacters ()
    self.adminCommand ({"god": {"sethp": {"c": [
      {"id": victimChar, "a": 1, "s": 0},
    ]}}})
    self.timed ("one kill settling %d pools + %d bodyguards"
                  % (M_STACK, M_STACK),
                lambda: self.mineAndSync ())
    assert "victim" not in self.getCharacters ()

    # Every pool paid its tranche to the sole killer; none drained.
    self.assertEqual (self.available ("hunter"),
                      hunterBefore + 50 * M_STACK)
    pools = [j for j in self.getJobs () if j["type"] == "wanted"]
    self.assertEqual (len (pools), M_STACK)
    self.assertEqual (set (p["remaining"] for p in pools), {1})
    # Every bodyguard forfeited: reward refund + bond, both to the poster.
    self.assertEqual (self.available ("victim"),
                      victimBefore + (100 + 500) * M_STACK)
    self.assertEqual (self.available ("guard"), guardBefore)
    self.assertEqual ([j for j in self.getJobs ()
                       if j["type"] == "bodyguard"], [])
    # The counter fan-out matches: one bump per settled row.
    self.assertEqual (self.getAccounts ()["hunter"].data["jobstats"],
                      {"completed": M_STACK, "failed": 0,
                       "posterfailed": 0, "value": 50 * M_STACK})
    self.assertEqual (
        self.getAccounts ()["guard"].data["jobstats"]["failed"], M_STACK)
    self.assertEqual (
        self.getAccounts ()["victim"].data["jobstats"]["posterfailed"],
        M_STACK)

    self.mainLogger.info ("Cohort 4: pruning the settled history cohort...")
    rows = self.historyRows ()
    total = N_ALIGNED + M_STACK
    self.assertEqual (len (rows), total)
    retention = self.roConfig ().params.jobs_history_retention
    lastSettled = max (e["settledtime"] for e in rows)
    prePruneTime = self.w3.eth.get_block ("latest")["timestamp"]
    snap = self.env.snapshot ()
    self.env.setMockTime (lastSettled + retention + 1)
    self.timed ("one prune deleting %d history rows" % total,
                lambda: self.mineAndSync (superblocks=False))
    self.assertEqual (self.historyRows (), [])
    # The standing pools have no deadline: they survive the 180-day warp.
    self.assertEqual (len (self.getJobs ()), M_STACK)

    self.mainLogger.info ("Reorg across the prune: undo and re-prune...")
    self.undoTo (snap, prePruneTime)
    self.assertEqual (len (self.historyRows ()), total)
    self.env.setMockTime (lastSettled + retention + 2)
    self.mineAndSync (superblocks=False)
    self.assertEqual (self.historyRows (), [])
    self.assertEqual (len (self.getJobs ()), M_STACK)

    self.mainLogger.info ("Jobs stress test succeeded.")


if __name__ == "__main__":
  JobsStressTest ().main ()
