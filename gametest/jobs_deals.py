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
Tests deals on the job board through moves:  posting, accepting and
confirming, a ruling on a dispute, the settlement of a due deal on
a superblock, cancelling, and invalid operations next to valid ones.
The rules for each step are covered by the unit tests.
"""

from pxtest import PXTest


class JobsDealsTest (PXTest):

  def postDeal (self):
    """
    Posts a deal with reward and collateral of 5'000 each and an arbiter
    fee of 10%, and returns its id.  The board must be empty before.
    """

    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86_400, "r": 5_000, "co": 5_000,
      "arbiter": "arbiter", "fee": 1_000, "tag": 1, "terms": "mine 100 ore",
    }]})
    self.generate (1)

    [jobId] = self.getJobs ()
    return jobId

  def expectBalances (self, expected):
    """
    Checks the balances of accounts, given in the dictionary as tuples
    of the available and the reserved coins.
    """

    acc = self.getAccounts ()
    for name, val in expected.items ():
      a = acc[name]
      self.assertEqual ((a.getBalance (), a.getBalance ("reserved")), val)

  def run (self):
    self.mainLogger.info ("Setting up test accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("worker", "g")
    self.initAccount ("arbiter", "b")
    self.generate (1)
    self.giftCoins ({"poster": 100_000, "worker": 100_000})

    self.mainLogger.info ("Posting a deal...")
    jobId = self.postDeal ()
    timestamp = self.w3.eth.get_block ("latest")["timestamp"]
    self.assertEqual (self.getJobs (), {
      jobId: {
        "id": jobId,
        "poster": "poster",
        "reward": 5_000,
        "collateral": 5_000,
        "deadline": timestamp + 86_400,
        "arbiter": "arbiter",
        "fee": 1_000,
        "tax": 300,
        "tag": 1,
        "terms": "mine 100 ore",
        "posterconfirmed": False,
        "workerconfirmed": False,
        "disputed": False,
        "reactionwindow": 30,
        "state": "open",
      },
    })
    # The reward is locked, and the posting fee of 1% is burnt.
    self.expectBalances ({"poster": (94_950, 5_000)})

    self.mainLogger.info ("Accepting and confirming the deal...")
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    job = self.getJobs ()[jobId]
    self.assertEqual (job["state"], "accepted")
    self.assertEqual (job["worker"], "worker")
    self.expectBalances ({"worker": (95_000, 5_000)})

    self.sendMove ("poster", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    self.assertEqual (self.getJobs ()[jobId]["posterconfirmed"], True)

    self.sendMove ("worker", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    self.assertEqual (self.getJobs (), {})
    # The worker gets the reward less 3% tax and 10% fee, and its
    # collateral back:  5'000 - 150 - 500 + 5'000 = 9'350.
    self.expectBalances ({
      "poster": (94_950, 0),
      "worker": (104_350, 0),
      "arbiter": (500, 0),
    })
    acc = self.getAccounts ()
    self.assertEqual (acc["poster"].data["dealstats"], {
      "completed": 0,
      "value": 0,
      "posted": 1,
      "postedvalue": 5_000,
      "disputed": 0,
    })
    self.assertEqual (acc["worker"].data["dealstats"], {
      "completed": 1,
      "value": 5_000,
      "posted": 0,
      "postedvalue": 0,
      "disputed": 0,
    })
    self.assertEqual (acc["arbiter"].data["arbiterstats"],
                      {"rulings": 0, "valueruled": 0})

    self.mainLogger.info ("Ruling on a dispute...")
    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [{"a": jobId}]})
    self.generate (1)
    self.sendMove ("poster", {"j": [{"dl": jobId, "dispute": True}]})
    self.generate (1)
    self.assertEqual (self.getJobs ()[jobId]["disputed"], True)
    acc = self.getAccounts ()
    self.assertEqual (acc["poster"].data["dealstats"]["disputed"], 1)
    self.assertEqual (acc["worker"].data["dealstats"]["disputed"], 1)

    self.sendMove ("arbiter", {"j": [{"dl": jobId, "rule": 30}]})
    self.generate (1)
    self.assertEqual (self.getJobs (), {})
    # Tax and fee are 150 and 500 as before.  Of the other 4'350 and the
    # collateral, the worker gets 30% (2'805) and the poster 6'545.
    self.expectBalances ({
      "poster": (96_445, 0),
      "worker": (102_155, 0),
      "arbiter": (1_000, 0),
    })
    acc = self.getAccounts ()
    self.assertEqual (acc["arbiter"].data["arbiterstats"],
                      {"rulings": 1, "valueruled": 10_000})

    self.mainLogger.info ("Settling a due deal on a superblock...")
    jobId = self.postDeal ()
    self.sendMove ("worker", {"j": [
      {"a": jobId},
      {"dl": jobId, "confirm": True},
    ]})
    self.generate (1)
    deadline = self.getJobs ()[jobId]["deadline"]

    # The block one second before the deadline is a superblock, so that
    # the block at the deadline is not.  The job is due then, but it is
    # settled only with the next superblock.
    self.env.setMockTime (deadline - 1)
    self.generate (1, superblocks=False)
    self.env.setMockTime (deadline)
    self.generate (1, superblocks=False)
    assert jobId in self.getJobs ()

    # The worker confirmed and the poster did not dispute, so that the
    # deal is settled in full.
    self.generate (1)
    self.assertEqual (self.getJobs (), {})
    self.expectBalances ({
      "poster": (91_395, 0),
      "worker": (106_505, 0),
      "arbiter": (1_500, 0),
    })

    self.mainLogger.info ("Cancelling a deal...")
    jobId = self.postDeal ()
    self.expectBalances ({"poster": (86_345, 5_000)})
    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    self.assertEqual (self.getJobs (), {})
    # The reward is refunded, but not the posting fee.
    self.expectBalances ({"poster": (91_345, 0)})

    self.mainLogger.info ("Skipping invalid operations...")
    jobId = self.postDeal ()
    # The unknown and malformed operations are skipped, and the valid ones
    # (accept and confirm) are executed in order.
    self.sendMove ("worker", {"j": [
      {"x": jobId},
      "a",
      {"a": jobId, "x": 1},
      {"a": jobId},
      {"dl": jobId, "confirm": False},
      {"dl": jobId, "confirm": True},
    ]})
    self.generate (1)
    job = self.getJobs ()[jobId]
    self.assertEqual (job["worker"], "worker")
    self.assertEqual (job["workerconfirmed"], True)


if __name__ == "__main__":
  JobsDealsTest ().main ()
