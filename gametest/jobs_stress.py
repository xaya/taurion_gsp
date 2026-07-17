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
block is mined + GSP-synced under a REAL wall-clock deadline of one
superblock interval (a daemon-thread join: it fires even if the GSP sync
hangs in its polling loop, and a self-check at the start of the run proves
both a slow and a never-returning callable fail AT the bound):

  1. aligned expiry: N transports sharing one deadline settle in ONE sweep,
     then the whole sweep is replayed across a reorg (undo + re-settle);
  2. standing bounty stack: M wanted pools on one name all pay on ONE kill;
  3. linked-entity stack: M accepted bodyguards on one character all settle
     on that same kill (both hooks fire in the same deadline-bounded block,
     and the kill block -- the broadest state mix -- is replayed across a
     reorg as well);
  4. retention prune: the whole settled-history cohort deletes in ONE block
     once the retention window passes (the standing pools survive it),
     also replayed across a reorg.

Every cohort row is paid (escrowed reward + burned posting fee), which is
the economic bound on an attacker lining these up.  Cohort sizes scale
with the JOBS_STRESS_N environment variable (the default keeps CI fast);
every bulk phase -- posts, assignments and acceptances -- rides in
sub-ceiling move chunks, so scaled runs construct the same shapes.  The
scaled shape is the release gate: run the default suite plus
`JOBS_STRESS_N=2200 make check TESTS=jobs_stress.py` on the exact release
SHA, under a process-level timeout(1) wrapper, and keep the log.  (The
wrapper matters on the FAILURE path: a deadline breach fails the test at
the bound, but Python cannot cancel the still-running daemon worker, so
only a hard process bound cleans up a wedged run.)

Recorded runs (make check TESTS=jobs_stress.py, regtest superblock_seconds
5, AMD Threadripper 7970X, 2026-07-17).  Single samples, quantised by the
0.1s GSP sync poll -- a coarse upper bound per settling block, not a
per-row cost measurement:

  JOBS_STRESS_N=200 (default):  sweep 200 in 0.104s; one kill settling
    25 pools + 25 bodyguards in 0.104s; prune 225 rows in 0.103s.
  JOBS_STRESS_N=1000 (5x):      sweep 1000 in 0.104s; kill 125+125 in
    0.105s; prune 1125 rows in 0.113s.
  JOBS_STRESS_N=2200 (11x):     sweep 2200 in 0.103s; kill 275+275 in
    0.105s; prune 2475 rows in 0.104s.  The 2200-row live board also
    walks the paged reader past its 2000-row page cap.

Every measured cohort completed within one or two poll intervals on this
machine -- comfortably inside the superblock budget the deadline enforces.
"""

import os
import threading
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

  def timed (self, label, fn, bound=None):
    """Runs fn under a REAL wall-clock deadline: the callable runs on a
    daemon worker thread and the join times out, so the assertion fires AT
    the deadline even if fn never returns (a hung GSP sync polls forever
    inside getCustomState).  The default bound is one superblock interval
    -- the operational budget: a settling superblock that cannot process
    within the cadence step means the GSP falls behind indefinitely.
    Recorded runs are ~0.1-0.2s, leaving ~25-50x margin over CI noise."""
    if bound is None:
      bound = self.roConfig ().params.superblock_seconds
    outcome = {}
    def work ():
      try:
        fn ()
      except Exception as e:
        outcome["error"] = e
    worker = threading.Thread (target=work, daemon=True)
    start = time.time ()
    worker.start ()
    worker.join (bound)
    duration = time.time () - start
    assert not worker.is_alive (), \
        "%s still running at the %ss deadline" % (label, bound)
    if "error" in outcome:
      raise outcome["error"]
    self.mainLogger.info ("%s took %.3fs" % (label, duration))

  def checkTimedDeadline (self):
    """Proves the timed() deadline is a real wall-clock bound: a slow and
    a never-returning callable both fail AT the deadline, not after the
    callable eventually returns."""
    for label, fn in [("slow", lambda: time.sleep (5)),
                      ("hung", lambda: threading.Event ().wait ())]:
      start = time.time ()
      try:
        self.timed ("deadline self-check (%s)" % label, fn, bound=1)
      except AssertionError:
        assert time.time () - start < 2, "deadline fired late for " + label
        continue
      raise RuntimeError ("deadline did not fire for " + label)

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

  def checkKillSettled (self, before):
    """Asserts the full kill fan-out: every pool paid one tranche to the
    sole killer (none drained), every bodyguard forfeited to the poster,
    and one counter bump per settled row."""
    assert "victim" not in self.getCharacters ()
    self.assertEqual (self.available ("hunter"),
                      before["hunter"] + 50 * M_STACK)
    pools = [j for j in self.getJobs () if j["type"] == "wanted"]
    self.assertEqual (len (pools), M_STACK)
    self.assertEqual (set (p["remaining"] for p in pools), {1})
    self.assertEqual (self.available ("victim"),
                      before["victim"] + (100 + 500) * M_STACK)
    self.assertEqual (self.available ("guard"), before["guard"])
    self.assertEqual ([j for j in self.getJobs ()
                       if j["type"] == "bodyguard"], [])
    self.assertEqual (self.getAccounts ()["hunter"].data["jobstats"],
                      {"completed": M_STACK, "failed": 0,
                       "posterfailed": 0, "value": 50 * M_STACK})
    self.assertEqual (
        self.getAccounts ()["guard"].data["jobstats"]["failed"], M_STACK)
    self.assertEqual (
        self.getAccounts ()["victim"].data["jobstats"]["posterfailed"],
        M_STACK)

  def checkKillUndone (self, before):
    """Asserts the kill block fully undid: victim alive, pools at full
    quota, bodyguards accepted again, balances and counters restored."""
    assert "victim" in self.getCharacters ()
    pools = [j for j in self.getJobs () if j["type"] == "wanted"]
    self.assertEqual (len (pools), M_STACK)
    self.assertEqual (set (p["remaining"] for p in pools), {2})
    self.assertEqual (len ([j for j in self.getJobs ()
                            if j["type"] == "bodyguard"
                            and j["state"] == "accepted"]), M_STACK)
    for nm in before:
      self.assertEqual (self.available (nm), before[nm])
    self.assertEqual (
        self.getAccounts ()["hunter"].data["jobstats"]["completed"], 0)

  def run (self):
    self.mainLogger.info ("Deadline self-check: slow and hung callables "
                          "must fail AT the bound...")
    self.checkTimedDeadline ()

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
    before = {nm: self.available (nm)
              for nm in ("hunter", "victim", "guard")}
    self.moveCharactersTo ({
      "victim": {"x": 60, "y": 0},
      "hunter": {"x": 60, "y": 0},
    })
    # The hunter acquired its target when the teleport block above was
    # processed (targets are selected at the END of a superblock, damage is
    # dealt at the START of the next).  Snapshot here so the whole kill
    # block can be reorg-replayed, then submit the god HP drop WITHOUT
    # mining, so the kill and its whole settlement fan-out happen inside
    # the ONE deadline-bounded block: alive before, dead after.
    preKillTime = self.w3.eth.get_block ("latest")["timestamp"]
    snap = self.env.snapshot ()
    dropHP = lambda: self.adminCommand ({"god": {"sethp": {"c": [
      {"id": victimChar, "a": 1, "s": 0},
    ]}}})
    assert "victim" in self.getCharacters ()
    dropHP ()
    self.timed ("one kill settling %d pools + %d bodyguards"
                  % (M_STACK, M_STACK),
                lambda: self.mineAndSync ())
    self.checkKillSettled (before)

    # The kill block mutates the broadest state mix (death, damage lists,
    # tranches, forfeits, history, three accounts' counters): replay it
    # across a reorg too.
    self.mainLogger.info ("Reorg across the kill: undo and re-kill...")
    self.undoTo (snap, preKillTime)
    self.checkKillUndone (before)
    dropHP ()
    self.mineAndSync ()
    self.checkKillSettled (before)

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
