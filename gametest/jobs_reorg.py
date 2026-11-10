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
Tests that a reorg undoes the changes to jobs:  a post, a ruling,
a deadline extension and the settlement at the deadline.
"""

from pxtest import PXTest


class JobsReorgTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up test accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("worker", "g")
    self.initAccount ("arbiter", "b")
    self.generate (1)
    self.giftCoins ({"poster": 100_000, "worker": 100_000})

    post = {"j": [{
      "t": "deal", "d": 86_400, "r": 5_000, "co": 5_000,
      "arbiter": "arbiter", "fee": 1_000,
    }]}

    self.mainLogger.info ("Undoing a post...")
    snapshot = self.env.snapshot ()
    self.sendMove ("poster", post)
    self.generate (1)
    self.assertEqual (len (self.getJobs ()), 1)
    self.assertEqual (self.getAccounts ()["poster"].getBalance (), 94_950)

    snapshot.restore ()
    self.assertEqual (self.getJobs (), {})
    # This includes the burnt posting fee.
    self.assertEqual (self.getAccounts ()["poster"].getBalance (), 100_000)

    self.mainLogger.info ("Posting and accepting a deal...")
    self.sendMove ("poster", post)
    self.generate (1)
    [jobId] = self.getJobs ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    deadline = self.getJobs ()[jobId]["deadline"]

    snapshot = self.env.snapshot ()
    preState = self.getGameState ()

    self.mainLogger.info ("Undoing a ruling...")
    self.sendMove ("poster", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    self.sendMove ("arbiter", {"j": [{"dl": jobId, "rule": 30}]})
    self.generate (1)
    self.assertEqual (self.getJobs (), {})
    snapshot.restore ()
    self.expectGameState (preState)

    self.mainLogger.info ("Undoing a deadline extension...")
    # A confirmation 10 seconds before the deadline extends it to a full
    # reaction window of 30 seconds.
    self.env.setMockTime (deadline - 10)
    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1, superblocks=False)
    self.assertEqual (self.getJobs ()[jobId]["deadline"], deadline + 20)
    snapshot.restore ()
    self.expectGameState (preState)

    self.mainLogger.info ("Undoing the refund of a dispute at the deadline...")
    self.sendMove ("worker", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    self.env.setMockTime (deadline)
    self.generate (1, superblocks=False)
    self.assertEqual (self.getJobs (), {})
    acc = self.getAccounts ()
    self.assertEqual (acc["poster"].getBalance (), 100_000 - 50)
    self.assertEqual (acc["worker"].getBalance (), 100_000)
    self.assertEqual (acc["worker"].data["dealstats"]["disputed"], 1)
    snapshot.restore ()
    self.expectGameState (preState)


if __name__ == "__main__":
  JobsReorgTest ().main ()
