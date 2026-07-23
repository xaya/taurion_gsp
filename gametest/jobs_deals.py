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
Integration test for the generic escrow-deal job type (Job::Type::DEAL).
Exercises the full real move path -- post -> accept -> confirm / dispute /
rule and the end-date expiry sweep -- through moveprocessor "j" dispatch,
block processing, the getjobspage board render, the settled-jobs history and
the coin/reserve effects.  The settlement arithmetic itself is pinned
exhaustively by the C++ DealTests; this proves the on-chain path, the paged
reads, the reputation surfacing (dealstats) and the superblock expiry hook.

Deals are faction-agnostic (no audience, no linked entity), so the poster,
worker and arbiter are deliberately three DIFFERENT factions here -- a
property no other job type has and one the real chain must honour.
"""

from pxtest import PXTest


class JobsDealsTest (PXTest):

  def dealStats (self, name):
    return self.getAccounts ()[name].data["dealstats"]

  def historyEntry (self, jobId):
    """Returns the full settled-jobs history row for a job (or None).  The
    settlement metadata (mode / settledp / feepaid) rides on this snapshot."""
    for e in self.historyRows ():
      if e["id"] == jobId:
        return e
    return None

  def postFee (self, reward):
    """Mirrors PostOperation::Fee (jobs.cpp): the burned posting fee."""
    p = self.roConfig ().params
    return max (p.job_post_fee_min, reward * p.job_post_fee_bps // 10000)

  def postDeal (self, poster="poster", reward=5000, collateral=5000,
                arbiter="arbiter", fee=1000, tag=1, terms="haul it"):
    """Posts one deal and returns its id (mirrors the DealTests helper:
    reward 5000, collateral 5000, arbiter fee 10%)."""
    post = {"t": "deal", "d": 86400, "r": reward, "co": collateral,
            "tag": tag, "terms": terms}
    if arbiter:
      post["arbiter"] = arbiter
      post["fee"] = fee
    self.sendMove (poster, {"j": [post]})
    self.generate (1)
    return self.newestJob ()["id"]

  def expire (self, deadline):
    """Advances mock time past the deal deadline and mines the superblock
    that runs the expiry sweep (the sweep only fires on a superblock; the big
    time jump makes this block one)."""
    self.env.setMockTime (deadline + 1)
    self.generate (1, superblocks=False)

  def run (self):
    self.mainLogger.info ("Setting up three cross-faction accounts...")
    # Poster, worker and arbiter each a different faction: deals carry no
    # audience gate, so this must work end-to-end.
    self.initAccount ("poster", "r")
    self.initAccount ("worker", "g")
    self.initAccount ("arbiter", "b")
    self.generate (1)
    self.giftCoins ({"poster": 1000000, "worker": 1000000, "arbiter": 1000000})

    self.testHappyPathBothConfirm ()
    self.testDisputeArbiterRules ()
    self.testAtomicConfirmDisputeRule ()
    self.testTimeoutGhostSplits ()
    self.testTimeoutSingleConfirm ()
    self.testTimeoutNeitherRefunds ()
    self.testCancelBeforeAccept ()
    self.testRejectsUnknownOp ()

    self.mainLogger.info ("Escrow-deal integration test succeeded.")

  def testHappyPathBothConfirm (self):
    self.mainLogger.info ("Posting a deal and settling it by mutual confirm...")
    pBefore = self.available ("poster")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")
    statBefore = self.dealStats ("worker")

    jobId = self.postDeal ()
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    # The board renders every posted term of the deal.
    self.assertEqual (job["type"], "deal")
    self.assertEqual (job["state"], "open")
    self.assertEqual (job["poster"], "poster")
    self.assertEqual (job["reward"], 5000)
    self.assertEqual (job["arbiter"], "arbiter")
    self.assertEqual (job["fee"], 1000)          # bps, snapshot at post
    self.assertEqual (job["tax"], 300)           # bps, snapshot at post
    self.assertEqual (job["tag"], 1)
    self.assertEqual (job["terms"], "haul it")
    self.assertEqual (job["posterConfirmed"], False)
    self.assertEqual (job["workerConfirmed"], False)
    self.assertEqual (job["disputed"], False)
    # Reward escrowed + posting fee burned; collateral is the worker's, not yet in.
    fee = self.postFee (5000)
    self.assertEqual (self.available ("poster"), pBefore - 5000 - fee)
    self.assertEqual (self.reserved ("poster"), 5000)

    self.mainLogger.info ("A different-faction worker accepts...")
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["state"], "accepted")
    self.assertEqual (job["worker"], "worker")
    self.assertEqual (self.available ("worker"), wBefore - 5000)
    self.assertEqual (self.reserved ("worker"), 5000)

    self.mainLogger.info ("One confirm leaves it open; the second settles it...")
    self.sendMove ("poster", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["posterConfirmed"], True)
    self.assertEqual (job["workerConfirmed"], False)

    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    assert self.jobGone (jobId)
    # p=100: worker <- 5000 - 150(tax) - 500(fee) + 5000(collateral) = 9350;
    # arbiter <- 500; treasury 150 burned; poster reclaims nothing.
    self.assertEqual (self.available ("worker"), wBefore - 5000 + 9350)
    self.assertEqual (self.reserved ("worker"), 0)
    self.assertEqual (self.available ("arbiter"), aBefore + 500)
    self.assertEqual (self.available ("poster"), pBefore - 5000 - fee)
    self.assertEqual (self.reserved ("poster"), 0)
    self.assertEqual (self.historyOutcome (jobId), "completed")
    # The worker's reputation counter bumps by the earned reward (5000).
    stat = self.dealStats ("worker")
    self.assertEqual (stat["completed"], statBefore["completed"] + 1)
    self.assertEqual (stat["value"], statBefore["value"] + 5000)

  def testDisputeArbiterRules (self):
    self.mainLogger.info ("Dispute resolved by the arbiter's %-dial...")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")
    statBefore = self.dealStats ("worker")

    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)

    # H1 probe: a confirmation waives only the confirmer's OWN dispute right.
    # The worker confirms, so its own later dispute is rejected (disputed stays
    # false), but the counterparty (poster) may still dispute a shoddy job.
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    self.sendMove ("worker", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["workerConfirmed"], True)
    self.assertEqual (job["disputed"], False)

    self.sendMove ("poster", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["disputed"], True)

    # Only the bound arbiter may rule, and only in {0,10,..,100}.
    self.sendMove ("arbiter", {"j": [{"dl": jobId, "rule": 30}]})
    self.generate (1)
    assert self.jobGone (jobId)
    # p=30: worker 2805, arbiter 850, treasury 255, poster 6090.
    self.assertEqual (self.available ("worker"), wBefore - 5000 + 2805)
    self.assertEqual (self.available ("arbiter"), aBefore + 850)
    self.assertEqual (self.historyOutcome (jobId), "completed")
    # The history snapshot records the actual ruling and the paid fee.
    entry = self.historyEntry (jobId)
    self.assertEqual (entry["mode"], "ruling")
    self.assertEqual (entry["settledp"], 30)
    self.assertEqual (entry["feepaid"], True)
    stat = self.dealStats ("worker")
    self.assertEqual (stat["completed"], statBefore["completed"] + 1)
    self.assertEqual (stat["value"], statBefore["value"] + 1500)

  def testAtomicConfirmDisputeRule (self):
    self.mainLogger.info ("Atomic [confirm, dispute, rule:0] cannot pass"
                          " the confirmation...")
    pBefore = self.available ("poster")
    wBefore = self.available ("worker")

    # Poster-as-arbiter: the disclosed self-arbiter arrangement.  The H1
    # merge-blocker was that this account could confirm (the binding
    # all-clear), then dispute its own confirmed deal and rule p=0 -- all in
    # ONE j array.  The move processor validates each op against the evolving
    # state, so the confirm lands, the self-dispute is barred by it and the
    # rule finds no dispute: the worker's p=100 protection survives the move.
    jobId = self.postDeal (arbiter="poster")
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    self.sendMove ("poster", {"j": [{"dl": jobId, "confirm": True},
                                    {"dl": jobId, "dispute": True},
                                    {"dl": jobId, "rule": 0}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["state"], "accepted")
    self.assertEqual (job["posterConfirmed"], True)
    self.assertEqual (job["disputed"], False)

    # The worker's confirm completes the both-confirm release at p=100; the
    # poster == arbiter account collects only its 500 fee.
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    assert self.jobGone (jobId)
    fee = self.postFee (5000)
    self.assertEqual (self.available ("worker"), wBefore - 5000 + 9350)
    self.assertEqual (self.available ("poster"), pBefore - 5000 - fee + 500)
    entry = self.historyEntry (jobId)
    self.assertEqual (entry["mode"], "both-confirm")
    self.assertEqual (entry["settledp"], 100)
    self.assertEqual (entry["feepaid"], True)

  def testTimeoutGhostSplits (self):
    self.mainLogger.info ("A ghosted arbiter falls back to the 50/50 sweep...")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")

    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    self.sendMove ("worker", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    deadline = next (j for j in self.getJobs () if j["id"] == jobId)["deadline"]

    self.expire (deadline)
    assert self.jobGone (jobId)
    # p=50 with the fee FORFEITED (the arbiter ghosted the dispute it was
    # hired to rule): worker 2500 - 75(tax) + 2500(collateral) = 4925 and
    # the arbiter gets nothing -- ruling always pays better than ghosting.
    self.assertEqual (self.available ("worker"), wBefore - 5000 + 4925)
    self.assertEqual (self.available ("arbiter"), aBefore)
    self.assertEqual (self.historyOutcome (jobId), "completed")

  def testTimeoutSingleConfirm (self):
    self.mainLogger.info ("One unopposed confirm settles in full at timeout...")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")

    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    deadline = next (j for j in self.getJobs () if j["id"] == jobId)["deadline"]

    self.expire (deadline)
    assert self.jobGone (jobId)
    # one confirm, no dispute => p=100, same as a mutual confirm.
    self.assertEqual (self.available ("worker"), wBefore - 5000 + 9350)
    self.assertEqual (self.available ("arbiter"), aBefore + 500)
    self.assertEqual (self.historyOutcome (jobId), "completed")

  def testTimeoutNeitherRefunds (self):
    self.mainLogger.info ("No one acts: the sweep refunds both stakes...")
    pBefore = self.available ("poster")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")

    fee = self.postFee (5000)
    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    deadline = next (j for j in self.getJobs () if j["id"] == jobId)["deadline"]

    self.expire (deadline)
    assert self.jobGone (jobId)
    # Reward back to poster (less the sunk posting fee), collateral back to
    # worker, arbiter untouched, nothing burned beyond the fee.
    self.assertEqual (self.available ("poster"), pBefore - fee)
    self.assertEqual (self.reserved ("poster"), 0)
    self.assertEqual (self.available ("worker"), wBefore)
    self.assertEqual (self.reserved ("worker"), 0)
    self.assertEqual (self.available ("arbiter"), aBefore)
    self.assertEqual (self.historyOutcome (jobId), "void")
    # The refund is stamped on the snapshot: no p transacted, and the bound
    # arbiter was never paid.
    entry = self.historyEntry (jobId)
    self.assertEqual (entry["mode"], "refund")
    assert "settledp" not in entry
    self.assertEqual (entry["feepaid"], False)

  def testCancelBeforeAccept (self):
    self.mainLogger.info ("An open deal cancels and refunds the reward...")
    pBefore = self.available ("poster")
    fee = self.postFee (5000)
    jobId = self.postDeal ()
    self.assertEqual (self.reserved ("poster"), 5000)

    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (self.available ("poster"), pBefore - fee)
    self.assertEqual (self.reserved ("poster"), 0)
    self.assertEqual (self.historyOutcome (jobId), "cancelled")

  def testRejectsUnknownOp (self):
    self.mainLogger.info ("An unknown job op does not touch an accepted deal...")
    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    # A deal settles only via confirm/dispute/rule; the removed delivery-style
    # fulfil op is not in the grammar at all, so the move is inert.
    self.sendMove ("worker", {"j": [{"f": jobId}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["state"], "accepted")
    # Clean up so the board is empty for any later reasoning.
    self.sendMove ("poster", {"j": [{"dl": jobId, "confirm": True}]})
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    assert self.jobGone (jobId)


if __name__ == "__main__":
  JobsDealsTest ().main ()
