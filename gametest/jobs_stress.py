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
paths run at material size through the REAL chain path, in single blocks,
with wall-clock timings logged:

  1. aligned expiry: N transports sharing one deadline settle in ONE sweep;
  2. standing bounty stack: M wanted pools on one name all pay on ONE kill;
  3. linked-entity stack: M accepted bodyguards on one character all settle
     on that same kill (both hooks fire in the same block);
  4. retention prune: the whole settled-history cohort deletes in ONE block
     once the retention window passes (the standing pools survive it).

Every cohort row is paid (escrowed reward + burned posting fee), which is
the economic bound on an attacker lining these up.  Cohort sizes scale
with the JOBS_STRESS_N environment variable (the default keeps CI fast);
the fork/e2e harness runs the same shapes against a real chain at 5-10x.
"""

import os
import time

from pxtest import PXTest

N_ALIGNED = int (os.getenv ("JOBS_STRESS_N", "200"))
M_STACK = max (10, N_ALIGNED // 8)

# The test chain's Xaya X layer silently drops move values above ~1670
# bytes, so bulk posts ride in sub-ceiling chunks of one move per block.
# Deadlines then differ by a few seconds per block, which does not matter:
# the sweep settles EVERYTHING due at once, so a single warp past the last
# deadline still lands the whole cohort in one ExpireJobs call.
POST_CHUNK = 15


class JobsStressTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def timed (self, label, fn):
    start = time.time ()
    fn ()
    self.mainLogger.info ("%s took %.3fs" % (label, time.time () - start))

  def postMany (self, name, op, count):
    """Posts `count` copies of the given job op, in sub-ceiling chunks."""
    left = count
    while left > 0:
      chunk = min (left, POST_CHUNK)
      self.sendMove (name, {"j": [op] * chunk})
      self.generate (1)
      left -= chunk

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

    posterBefore = self.available ("poster")
    self.env.setMockTime (max (j["deadline"] for j in board) + 1)
    self.timed ("one sweep settling %d aligned jobs" % N_ALIGNED,
                lambda: self.generate (1, superblocks=False))
    self.assertEqual (self.getJobs (), [])
    # Every escrow refunded in that one superblock, fees already burned.
    self.assertEqual (self.available ("poster"),
                      posterBefore + 100 * N_ALIGNED)
    rows = self.historyRows ()
    self.assertEqual (len (rows), N_ALIGNED)
    self.assertEqual (set (e["outcome"] for e in rows), {"void"})

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
    self.sendMove ("victim", {"j": [{"s": i, "w": "guard"}
                                    for i in guardIds]})
    self.generate (1)
    self.sendMove ("guard", {"j": [{"a": i} for i in guardIds]})
    self.generate (1)
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
    self.setCharactersHP ({"victim": {"a": 1, "s": 0}})
    self.timed ("one kill settling %d pools + %d bodyguards"
                  % (M_STACK, M_STACK),
                lambda: self.generate (1))
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
    self.env.setMockTime (lastSettled + retention + 1)
    self.timed ("one prune deleting %d history rows" % total,
                lambda: self.generate (1, superblocks=False))
    self.assertEqual (self.historyRows (), [])
    # The standing pools have no deadline: they survive the 180-day warp.
    self.assertEqual (len (self.getJobs ()), M_STACK)

    self.mainLogger.info ("Jobs stress test succeeded.")


if __name__ == "__main__":
  JobsStressTest ().main ()
