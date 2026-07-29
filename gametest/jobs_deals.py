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
block processing, the getjobs board render and
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

  def arbiterStats (self, name):
    return self.getAccounts ()[name].data["arbiterstats"]

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
    self.testPosterArbiterRejectedAndAtomicConfirm ()
    self.testTimeoutGhostSplits ()
    self.testTimeoutSingleConfirm ()
    self.testTimeoutNeitherRefunds ()
    self.testNoArbiterDispute ()
    self.testCancelBeforeAccept ()
    self.testRejectsUnknownOp ()
    self.testReactionWindowExtension ()
    self.testNoArbiterDisputeDoesNotExtend ()

    self.mainLogger.info ("Escrow-deal integration test succeeded.")

  def testHappyPathBothConfirm (self):
    self.mainLogger.info ("Posting a deal and settling it by mutual confirm...")
    pBefore = self.available ("poster")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")
    statBefore = self.dealStats ("worker")
    pStatBefore = self.dealStats ("poster")
    aStatBefore = self.arbiterStats ("arbiter")

    jobId = self.postDeal ()
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    # The board renders every posted term of the deal.
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
    # The worker's reputation counter bumps by the earned reward (5000).
    stat = self.dealStats ("worker")
    self.assertEqual (stat["completed"], statBefore["completed"] + 1)
    self.assertEqual (stat["value"], statBefore["value"] + 5000)
    # The poster's mirror of the same settlement, through the same JSON. A clean
    # deal troubles nobody: no dispute on either side, no arbiter record.
    pStat = self.dealStats ("poster")
    self.assertEqual (pStat["posted"], pStatBefore["posted"] + 1)
    self.assertEqual (pStat["postedvalue"], pStatBefore["postedvalue"] + 5000)
    self.assertEqual (pStat["disputed"], pStatBefore["disputed"])
    self.assertEqual (stat["disputed"], statBefore["disputed"])
    self.assertEqual (self.arbiterStats ("arbiter"), aStatBefore)

  def testDisputeArbiterRules (self):
    self.mainLogger.info ("Dispute resolved by the arbiter's %-dial...")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")
    statBefore = self.dealStats ("worker")
    pStatBefore = self.dealStats ("poster")
    aStatBefore = self.arbiterStats ("arbiter")

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
    stat = self.dealStats ("worker")
    self.assertEqual (stat["completed"], statBefore["completed"] + 1)
    self.assertEqual (stat["value"], statBefore["value"] + 1500)
    # Both parties carry the dispute; the arbiter carries the ruling it
    # delivered; the poster's payout mirror scales with p exactly like the
    # worker's (5000 * 30/100).
    pStat = self.dealStats ("poster")
    self.assertEqual (pStat["posted"], pStatBefore["posted"] + 1)
    self.assertEqual (pStat["postedvalue"], pStatBefore["postedvalue"] + 1500)
    self.assertEqual (pStat["disputed"], pStatBefore["disputed"] + 1)
    self.assertEqual (stat["disputed"], statBefore["disputed"] + 1)
    # The arbiter's value counter takes the whole pot it directed (5000 reward
    # + 5000 collateral), not the worker's share -- a low ruling decided just as
    # much money as a high one.
    aStat = self.arbiterStats ("arbiter")
    self.assertEqual (aStat["rulings"], aStatBefore["rulings"] + 1)
    self.assertEqual (aStat["valueruled"], aStatBefore["valueruled"] + 10000)

  def testPosterArbiterRejectedAndAtomicConfirm (self):
    self.mainLogger.info ("Poster == arbiter is rejected; an atomic confirm"
                          " bars the same party's dispute...")
    # §1: a poster-as-arbiter deal is an armed trap under the reaction window
    # (late dispute -> rule p=0 seizure), banned at the post door.
    before = self.getJobs ()
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86400, "r": 5000, "co": 5000,
      "arbiter": "poster", "fee": 1000, "terms": "trap"}]})
    self.generate (1)
    self.assertEqual (self.getJobs (), before)   # nothing admitted

    # The atomic-array H1 pin survives on a normal (third-party arbiter) deal:
    # in ONE j array the worker's confirm lands and bars its own later dispute
    # (the move processor validates each op against the evolving state), so the
    # deal stays accepted, confirmed and NOT disputed.
    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True},
                                    {"dl": jobId, "dispute": True}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["state"], "accepted")
    self.assertEqual (job["workerConfirmed"], True)
    self.assertEqual (job["disputed"], False)
    # Clean up so the board is empty for later reasoning.
    self.sendMove ("poster", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    assert self.jobGone (jobId)

  def testTimeoutGhostSplits (self):
    self.mainLogger.info ("A ghosted arbiter falls back to the 50/50 sweep...")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")
    aStatBefore = self.arbiterStats ("arbiter")

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
    # A ghost leaves NO mark on the arbiter's account record, by design: the
    # poster binds an arbiter without its consent, so a permanent counter here
    # would be inflictable on a bystander.  Consensus punishes the ghost by
    # forfeiting the fee (asserted above), and nothing else.  No "ghosted" key
    # exists in arbiterstats at all.
    aStat = self.arbiterStats ("arbiter")
    self.assertEqual (aStat, aStatBefore)
    assert "ghosted" not in aStat, aStat

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

  def testNoArbiterDispute (self):
    self.mainLogger.info ("A no-arbiter dispute forces the free 50/50 sweep...")
    pBefore = self.available ("poster")
    wBefore = self.available ("worker")
    fee = self.postFee (5000)
    aStatBefore = self.arbiterStats ("arbiter")

    # A genuinely no-arbiter deal: the post omits arbiter and fee entirely, so
    # no ruling can ever settle a dispute here.
    jobId = self.postDeal (arbiter=None)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    assert "arbiter" not in job
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)

    # The worker confirms; the still-unconfirmed poster keeps its dispute right
    # (design §6.2) and disputes.  With no arbiter that dispute is unrulable, so
    # the sweep settles the approved free terminal p=50 ghost split -- NOT the
    # p=100 single-confirm the worker's confirm alone would have earned.
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    self.sendMove ("poster", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["workerConfirmed"], True)
    self.assertEqual (job["disputed"], True)
    deadline = job["deadline"]

    self.expire (deadline)
    assert self.jobGone (jobId)
    # p=50, no fee: worker 2500 - 75(tax) + 2500(collateral) = 4925; the poster
    # reclaims the remaining 4850; treasury 225 burned; no arbiter is paid.
    self.assertEqual (self.available ("worker"), wBefore - 5000 + 4925)
    self.assertEqual (self.reserved ("worker"), 0)
    self.assertEqual (self.available ("poster"), pBefore - 5000 - fee + 4850)
    self.assertEqual (self.reserved ("poster"), 0)
    # ghost-split is ALSO the no-arbiter fallback, so no arbiter record may move
    # here: "arbiter" is a bystander to this deal and must stay untouched.
    self.assertEqual (self.arbiterStats ("arbiter"), aStatBefore)

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

  def confirmAt (self, name, jobId, when, op):
    """Sends one deal op at an exact block time (a single non-superblock)."""
    self.env.setMockTime (when)
    self.sendMove (name, {"j": [{"dl": jobId, **op}]})
    self.generate (1, superblocks=False)

  def testReactionWindowExtension (self):
    self.mainLogger.info ("A late confirm extends the deadline; the counterparty"
                          " and arbiter keep their window (regtest W = 30)...")
    wBefore = self.available ("worker")
    aBefore = self.available ("arbiter")
    jobId = self.postDeal (terms="window")
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    d0 = job["deadline"]
    self.assertEqual (job["reactionwindow"], 30)   # min(30, d) snapshot at post

    # Worker confirms 10s before the deadline: it lands inside W and pushes the
    # deadline to confirm_ts + 30.
    self.confirmAt ("worker", jobId, d0 - 10, {"confirm": True})
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["deadline"], d0 - 10 + 30)

    # The still-unconfirmed poster disputes inside the extension: arbiter-bound,
    # so it extends again and stamps dispute_time.
    self.confirmAt ("poster", jobId, d0 + 15, {"dispute": True})
    job = next (j for j in self.getJobs () if j["id"] == jobId)
    self.assertEqual (job["disputed"], True)
    self.assertEqual (job["disputetime"], d0 + 15)
    self.assertEqual (job["deadline"], d0 + 15 + 30)

    # The arbiter rules inside that window -- NOT denied (the v1.1 flip).
    self.confirmAt ("arbiter", jobId, d0 + 40, {"rule": 50})
    assert self.jobGone (jobId)
    # p=50: worker 4675, arbiter 750.
    self.assertEqual (self.available ("worker"), wBefore - 5000 + 4675)
    self.assertEqual (self.available ("arbiter"), aBefore + 750)

  def testNoArbiterDisputeDoesNotExtend (self):
    self.mainLogger.info ("A no-arbiter dispute near the deadline does NOT"
                          " extend (its only successor is the 50/50 sweep)...")
    jobId = self.postDeal (arbiter=None, terms="noext")
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    d0 = next (j for j in self.getJobs () if j["id"] == jobId)["deadline"]
    self.confirmAt ("worker", jobId, d0 - 5, {"dispute": True})
    self.assertEqual (
        next (j for j in self.getJobs () if j["id"] == jobId)["deadline"], d0)
    self.expire (d0)
    assert self.jobGone (jobId)


if __name__ == "__main__":
  JobsDealsTest ().main ()
