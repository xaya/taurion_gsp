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
Reorg / undo coverage for the jobs subsystem.  Every consensus state surface
the feature adds -- job rows with their escrowed balances, the settled-jobs
history table and the runtime-parameters table -- must unwind bit-identically
when the blocks touching it are detached, and the restored state must be
fully live for an alternative history.  The per-mechanic behaviour itself is
covered by the other jobs gametests; this pins the changeset (undo) machinery
they all rely on.
"""

from pxtest import PXTest


class JobsReorgTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up accounts and an accepted deal...")
    self.initAccount ("poster", "r")
    self.initAccount ("worker", "g")
    self.initAccount ("arbiter", "b")
    self.generate (1)
    self.giftCoins ({"poster": 100000, "worker": 100000, "arbiter": 100000})

    # An accepted deal (both stakes escrowed) is the base state shared by
    # both histories.
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86400, "r": 5000, "co": 5000,
      "arbiter": "arbiter", "fee": 1000, "terms": "reorg me",
    }]})
    self.generate (1)
    jobId = self.newestJob ()["id"]
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)

    snapshot = self.env.snapshot ()
    preState = self.getGameState ()
    preHistory = self.historyRows ()

    self.mainLogger.info ("Settling the deal and retuning a parameter...")
    # The branch that will be detached: an admin retune (parameters table),
    # a dispute plus ruling (job deletion, settlement balance moves and the
    # worker's dealstats bump) and the settlement's history row.
    self.adminCommand ({"param": [{"n": "min-deal-reward", "v": 20000}]})
    self.sendMove ("poster", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    self.sendMove ("arbiter", {"j": [{"dl": jobId, "rule": 30}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (self.historyOutcome (jobId), "completed")
    # The retuned floor rejects a small deal on this branch.
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86400, "r": 5000, "co": 0, "terms": "small"}]})
    self.generate (1)
    self.assertEqual (self.getJobs (), [])

    self.mainLogger.info ("Detaching the settlement blocks...")
    snapshot.restore ()
    # The undo restored every jobs surface: the live row is back with its
    # escrows, all balances and dealstats match bit-exactly, and both the
    # settlement's history row and the parameter override are gone.
    self.expectGameState (preState)
    self.assertEqual (self.historyRows (), preHistory)

    self.mainLogger.info ("Replaying an alternative history...")
    # The parameter override was undone with its block: the small deal is
    # admissible again under the roconfig default floor.
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86400, "r": 5000, "co": 0, "terms": "small"}]})
    self.generate (1)
    assert not self.jobGone (self.newestJob ()["id"])

    # And the restored deal is fully live: it settles by mutual confirm now.
    wBefore = self.available ("worker")
    self.sendMove ("poster", {"j": [{"dl": jobId, "confirm": True}]})
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (self.historyOutcome (jobId), "completed")
    # p=100: worker <- 5000 - 150(tax) - 500(fee) + 5000(collateral) = 9350.
    self.assertEqual (self.available ("worker"), wBefore + 9350)

    self.mainLogger.info ("Jobs reorg test succeeded.")


if __name__ == "__main__":
  JobsReorgTest ().main ()
