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
(see the ExpireJobs notes in src/jobs.hpp).  All seven unbounded-cohort
paths run at material size through the REAL chain path, and each primary
measured block (reorg replays are deliberately not timed) is mined +
GSP-synced under a REAL wall-clock deadline of one
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
  4. retention prune: once the retention window passes, each superblock
     deletes at most one roconfig batch (5,000) of the settled-history
     cohort, oldest first -- the 10k scale proves the deterministic drain
     over three sweeps (the standing pools survive it), also replayed
     across a reorg;
  5. dormant distinct-target board: M more pools on M DISTINCT initialised
     target names, then an UNRELATED kill -- the bounty attribution probes
     only the dead owner (one indexed query, negatives memoised), so the
     dormant board must add nothing to an unrelated death;
  6. pools x killers: K distinct same-faction hunter accounts all on the
     victim's damage list for ONE kill that pays and drains every stacked
     pool, the multiplicative payout shape (M pools x K owners);
  7. mega-battle deaths: P distinct owners (bountied and bounty-free mixed)
     all die in ONE superblock against the dormant board, then the whole
     death block is replayed across a reorg.  (Repeated same-owner deaths
     in one block are unit-covered by SameBlockKillsBeyondPoolDoNotCrash.)

The run first RAISES the admission caps through the real "param" admin
command (the cohorts deliberately exceed the production defaults), so the
runtime-tunable cap path is itself part of the tested surface; the caps'
enforcement lives in jobs_caps.py.

Every cohort row is paid (escrowed reward + burned posting fee), which is
the economic bound on an attacker lining these up.  Cohort sizes scale
with the JOBS_STRESS_N environment variable (the default keeps CI fast);
every bulk phase -- posts, assignments and acceptances -- rides in
sub-ceiling move chunks, so scaled runs construct the same shapes.  The
scaled shape is the release gate: run the default suite plus
`JOBS_STRESS_N=2200` AND `JOBS_STRESS_N=10000` (the global-cap
cardinality) `make check TESTS=jobs_stress.py` on the exact release SHA,
under a process-level timeout(1) wrapper, and keep the logs.  (The
wrapper matters on the FAILURE path: a deadline breach fails the test at
the bound, but Python cannot cancel the still-running daemon worker, so
only a hard process bound cleans up a wedged run.)

Recorded runs (make check TESTS=jobs_stress.py, regtest superblock_seconds
5, AMD Threadripper 7970X, 2026-07-18, v17-round aggregated-payout build).
Single samples, quantised by the 0.1s GSP sync poll -- a coarse upper
bound per settling block, not a per-row cost measurement.  Every kill
cohort mines its killing block INSIDE the timed call (the god HP drop is
submitted unmined), and cohort 6's tranche scales with its killer count,
so the per-owner payout pass is genuinely exercised at every scale:

  JOBS_STRESS_N=200 (default):  sweep 200 in 0.103s; one kill settling
    25 pools + 25 bodyguards in 0.104s; prune 225 of 225 in 0.103s;
    unrelated kill against 50 dormant pools in 0.105s; 25 pools x 4
    killers in 0.105s; 6 deaths against 25 dormant pools in 0.105s.
  JOBS_STRESS_N=1000 (5x):      sweep 1000 in 0.103s; kill 125+125 in
    0.205s; prune 1125 of 1125 in 0.304s; unrelated kill against 250
    dormant pools in 0.205s; 125 pools x 15 killers in 0.105s; 15 deaths
    against 125 dormant pools in 0.105s.
  JOBS_STRESS_N=2200 (11x):     sweep 2200 in 0.104s; kill 275+275 in
    0.104s; prune 2475 of 2475 in 0.103s; unrelated kill against 550
    dormant pools in 0.107s; 275 pools x 34 distinct killers in 0.107s;
    34 deaths against 275 dormant pools in 0.108s.  The 2200-row live
    board also walks the paged reader past its 2000-row page cap.
  JOBS_STRESS_N=10000 (default GLOBAL-cap cardinality, see cohort 1's
    label):  sweep 10000 in 0.357s; kill 1250+1250 in 0.105s; prune 5000
    of 11250 in 0.104s (the batched drain -- two further superblocks
    empty the remainder, survivor identity asserted exactly); unrelated
    kill against 2500 dormant pools in 0.112s; 1250 pools x 156 distinct
    killers -- tranche 156, share 1, so all 156 owners are genuinely
    credited through the aggregated single pass -- in 0.115s; 42 deaths
    (the site-grid cap) against 1250 dormant pools in 0.216s.

Every measured cohort completed well inside the superblock budget the
deadline enforces, flat across the scales -- the whole global-cap board
settles in under half a second, and the worst-case payout block (every
pool paying every killer owner) costs pools + owners account writes, not
pools x owners.  The dormant-board and mega-battle samples are the
regression evidence for the bounty attribution rework: thousands of
dormant pools cost a death block no more than an empty board (one indexed
probe per distinct dead owner).
"""

import os
import threading
import time

from pxtest import PXTest

N_ALIGNED = int (os.getenv ("JOBS_STRESS_N", "200"))
M_STACK = max (10, N_ALIGNED // 8)
K_KILLERS = max (4, M_STACK // 8)
# The stacked pools' per-kill tranche: it must hand every one of cohort 6's
# distinct killers at least one coin at EVERY scale, or the integer split
# rounds to zero and the whole per-owner payout pass -- the very path under
# test -- is silently skipped (a share-zero kill burns the tranche without
# touching any account).
TRANCHE = max (50, K_KILLERS)

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
                      before["hunter"] + TRANCHE * M_STACK)
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
                       "posterfailed": 0, "value": TRANCHE * M_STACK})
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

    self.mainLogger.info ("Raising the admission caps for the cohorts...")
    # The stress shapes deliberately exceed the production defaults; the
    # raise itself exercises the runtime-tunable parameter path.
    self.adminCommand ({"param": [
      {"n": n, "v": 10**6}
      for n in ("max-live-jobs", "max-jobs-per-poster",
                "max-jobs-per-linked-entity", "max-bounty-pools-per-target")
    ] + [
      {"n": n, "v": 1}
      for n in ("min-job-reward", "min-bounty-reward")
    ]})
    self.generate (1)

    self.mainLogger.info ("Setting up accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("hunter", "r")
    self.initAccount ("victim", "b")
    self.initAccount ("guard", "b")
    self.generate (1)
    # Fund linearly with the scale: cohort 1 alone escrows
    # N_ALIGNED x (reward 100 + fee 1), which outgrows any fixed gift
    # (the 10k max-legal run needs 1,010,000 for cohort 1 by itself).
    fund = 101 * N_ALIGNED + 10**6
    self.giftCoins ({"poster": fund, "victim": fund, "guard": fund})
    self.build ("checkmark", "poster", {"x": 0, "y": 0}, rot=0)
    bId = max (self.getBuildings ().keys ())

    self.mainLogger.info ("Cohort 1: %d aligned transports (global-cap "
                          "cardinality; concentration caps raised)..."
                            % N_ALIGNED)
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

    # M standing pools on the victim's name (quota 2, so one kill pays one
    # tranche from EVERY pool and none drains)...
    self.postMany ("poster", {
      "t": "wanted", "r": 2 * TRANCHE, "co": 0, "name": "victim", "n": 2,
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
    batch = self.roConfig ().params.jobs_history_prune_batch
    sbSecs = self.roConfig ().params.superblock_seconds
    lastSettled = max (e["settledtime"] for e in rows)
    prePruneTime = self.w3.eth.get_block ("latest")["timestamp"]
    snap = self.env.snapshot ()

    def pruneOrder (rows):
      """The deterministic deletion order: oldest (settled_time, id) first,
      so after one batch the survivors are exactly the newest rows."""
      return sorted (rows, key=lambda e: (e["settledtime"], e["id"]))

    def drainPrune (t):
      """Advances superblocks until the history is empty, asserting each
      sweep deletes exactly one further batch of the exact oldest rows
      (survivor IDENTITY, not just counts)."""
      left = pruneOrder (self.historyRows ())
      while left:
        left = left[batch:]
        t += sbSecs
        self.env.setMockTime (t)
        self.mineAndSync (superblocks=False)
        self.assertEqual ([e["id"] for e in pruneOrder (self.historyRows ())],
                          [e["id"] for e in left])

    self.env.setMockTime (lastSettled + retention + 1)
    self.timed ("one prune deleting %d of %d history rows"
                  % (min (batch, total), total),
                lambda: self.mineAndSync (superblocks=False))
    # One superblock deletes at most one batch, oldest first; the
    # remainder drains on the following sweeps (three in all at the 10k
    # scale, where the cohort outgrows the batch).  The survivors are the
    # exact newest (settled_time, id) rows, not merely the right count.
    expectSurvivors = [e["id"] for e in pruneOrder (rows)][batch:]
    self.assertEqual ([e["id"] for e in pruneOrder (self.historyRows ())],
                      expectSurvivors)
    drainPrune (lastSettled + retention + 1)
    # The standing pools have no deadline: they survive the 180-day warp.
    self.assertEqual (len (self.getJobs ()), M_STACK)

    self.mainLogger.info ("Reorg across the prune: undo and re-prune...")
    self.undoTo (snap, prePruneTime)
    self.assertEqual (len (self.historyRows ()), total)
    self.env.setMockTime (lastSettled + retention + 2)
    self.mineAndSync (superblocks=False)
    self.assertEqual ([e["id"] for e in pruneOrder (self.historyRows ())],
                      expectSurvivors)
    drainPrune (lastSettled + retention + 2)
    self.assertEqual (self.historyRows (), [])
    self.assertEqual (len (self.getJobs ()), M_STACK)

    self.mainLogger.info ("Cohort 5: %d dormant distinct bounty targets, "
                          "then an unrelated kill..." % M_STACK)
    # Bounty targets must be existing initialised accounts, so a dormant
    # board costs its builder one name registration per DISTINCT target on
    # top of every pool's fee + escrow.
    targetNames = ["tgt%d" % i for i in range (M_STACK)]
    for i, nm in enumerate (targetNames):
      self.initAccount (nm, "b")
      if (i + 1) % OPS_CHUNK == 0:
        self.generate (1)
    self.generate (1)
    self.sendJobOps ("poster", [
      {"t": "wanted", "r": 100, "co": 0, "name": nm, "n": 2}
      for nm in targetNames])
    boardBefore = sorted (self.getJobs (), key=lambda j: j["id"])
    self.assertEqual (len (boardBefore), 2 * M_STACK)

    self.initAccount ("bystander", "b")
    self.generate (1)
    self.createCharacters ("bystander")
    self.generate (1)
    hunterBefore = self.available ("hunter")
    self.moveCharactersTo ({
      "bystander": {"x": 60, "y": 0},
      "hunter": {"x": 60, "y": 0},
    })
    # The god HP drop is NOT mined here: the timed mineAndSync below mines
    # the killing block itself, so the kill AND its whole bounty-attribution
    # pass run inside the deadline-bounded block (alive before, dead after).
    assert "bystander" in self.getCharacters ()
    self.setCharactersHP ({"bystander": {"a": 1, "s": 0}}, mine=False)
    self.timed ("unrelated kill against %d dormant pools" % (2 * M_STACK),
                lambda: self.mineAndSync ())
    assert "bystander" not in self.getCharacters ()
    # No pool paid, none drained: the dormant board adds nothing.
    self.assertEqual (self.available ("hunter"), hunterBefore)
    self.assertEqual (sorted (self.getJobs (), key=lambda j: j["id"]),
                      boardBefore)

    self.mainLogger.info ("Cohort 6: one kill paying %d pools x %d distinct "
                          "killers..." % (M_STACK, K_KILLERS))
    # All hunters share a faction, so they are allies of each other and the
    # respawned victim is their only in-range enemy -- which makes the kill
    # and its damage-list attribution deterministic (see jobs_bounty.py).
    pack = ["hunter"] + ["pack%d" % i for i in range (K_KILLERS - 1)]
    for i, nm in enumerate (pack[1:]):
      self.initAccount (nm, "r")
      if (i + 1) % OPS_CHUNK == 0:
        self.generate (1)
    self.generate (1)
    for nm in pack[1:]:
      self.createCharacters (nm)
    self.generate (1)
    for nm in pack[1:]:
      self.changeCharacterVehicle (nm, "light attacker")
    self.createCharacters ("victim")
    self.generate (1)

    # Each pool still holds its LAST tranche: this kill pays and drains
    # every one of them, split across the distinct killer owners (the
    # division remainder burns).  The share must be POSITIVE at this scale,
    # or the block under time would skip the per-owner payout pass and
    # measure nothing (the exact defect a zero-share tranche hid before).
    share = TRANCHE // K_KILLERS
    assert share > 0, "tranche %d cannot pay %d killers" % (TRANCHE, K_KILLERS)
    before = {nm: self.available (nm) for nm in pack}
    spots = {"victim": {"x": 0, "y": 0}}
    for nm in pack:
      spots[nm] = {"x": 0, "y": 0}
    self.moveCharactersTo (spots)
    # As in cohort 5: submit the drop unmined so the timed block below is
    # the killing/settling block itself.
    assert "victim" in self.getCharacters ()
    self.setCharactersHP ({"victim": {"a": 1, "s": 0}}, mine=False)
    self.timed ("one kill paying %d pools x %d killers"
                  % (M_STACK, K_KILLERS),
                lambda: self.mineAndSync ())
    assert "victim" not in self.getCharacters ()
    for nm in pack:
      self.assertEqual (self.available (nm), before[nm] + share * M_STACK)
    # The victim's pools all drained and deleted; the dormant board stays.
    remaining = [j for j in self.getJobs () if j["type"] == "wanted"]
    self.assertEqual (len (remaining), M_STACK)
    assert all (j["target"] != "victim" for j in remaining)
    rows = self.historyRows ()
    self.assertEqual (len (rows), M_STACK)
    self.assertEqual (set (e["outcome"] for e in rows), {"drained"})

    P_PAIRS = min (42, max (6, M_STACK // 8))
    self.mainLogger.info ("Cohort 7: %d distinct owners die in ONE "
                          "superblock..." % P_PAIRS)
    # Two victims are under a live pool from the dormant board (tgt0/tgt1);
    # the rest are bounty-free.  Each victim gets its own same-tile hunter
    # at a spot far from all the others, so target selection is
    # deterministic pair-wise and every death lands in the same block.
    vOwners = ["tgt0", "tgt1", "bystander"] \
        + ["extra%d" % i for i in range (P_PAIRS - 3)]
    hOwners = ["ph%d" % i for i in range (P_PAIRS)]
    for i, nm in enumerate (vOwners[3:] + hOwners):
      self.initAccount (nm, "b" if nm.startswith ("extra") else "r")
      if (i + 1) % OPS_CHUNK == 0:
        self.generate (1)
    self.generate (1)
    for nm in vOwners + hOwners:
      self.createCharacters (nm)
    self.generate (1)
    for nm in hOwners:
      self.changeCharacterVehicle (nm, "light attacker")

    spots = {}
    for i, (v, h) in enumerate (zip (vOwners, hOwners)):
      # A grid of pair sites, 300 hexes apart on both axes.  Rows of
      # seven and the 42-site cap keep every site inside the map's hex
      # bound (|x + y| stays under ~4,000) while no two pairs come near
      # auto-engage range of each other.
      pos = {"x": 300 * (1 + i % 7), "y": 300 * (1 + i // 7)}
      spots[v] = dict (pos)
      spots[h] = dict (pos)
    self.moveCharactersTo (spots)

    hBefore = {nm: self.available (nm) for nm in hOwners}
    boardBefore = sorted (self.getJobs (), key=lambda j: j["id"])
    preMegaTime = self.w3.eth.get_block ("latest")["timestamp"]
    snap = self.env.snapshot ()

    def dropAllVictims ():
      for nm in vOwners:
        assert nm in self.getCharacters ()
      self.setCharactersHP (
          {nm: {"a": 1, "s": 0} for nm in vOwners}, mine=False)

    def checkMegaBattle ():
      for nm in vOwners:
        assert nm not in self.getCharacters ()
      # The two bountied victims paid one tranche of 50 (the dormant
      # board's tranche, untouched by TRANCHE scaling) to their pair
      # hunter; every other hunter got nothing.
      for i, nm in enumerate (hOwners):
        self.assertEqual (self.available (nm),
                          hBefore[nm] + (50 if i < 2 else 0))
      # The two touched pools lost one tranche (remaining 2 -> 1) but stay
      # on the board; the dormant remainder is untouched.
      pools = [j for j in self.getJobs () if j["type"] == "wanted"]
      self.assertEqual (len (pools), M_STACK)
      self.assertEqual (sorted (p["remaining"] for p in pools),
                        [1, 1] + [2] * (M_STACK - 2))

    dropAllVictims ()
    self.timed ("one superblock settling %d deaths against %d dormant pools"
                  % (P_PAIRS, M_STACK),
                lambda: self.mineAndSync ())
    checkMegaBattle ()

    self.mainLogger.info ("Reorg across the mega-battle: undo and re-kill...")
    self.undoTo (snap, preMegaTime)
    for nm in vOwners:
      assert nm in self.getCharacters ()
    self.assertEqual (sorted (self.getJobs (), key=lambda j: j["id"]),
                      boardBefore)
    for nm in hOwners:
      self.assertEqual (self.available (nm), hBefore[nm])
    dropAllVictims ()
    self.mineAndSync ()
    checkMegaBattle ()

    self.mainLogger.info ("Jobs stress test succeeded.")


if __name__ == "__main__":
  JobsStressTest ().main ()
