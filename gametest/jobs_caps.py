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
Tests the limits on posting jobs from the config, how getjobsparams reports
them, and that config updates change them from the next block.  The limits
themselves are covered by the unit tests.
"""

from pxtest import PXTest


class JobsCapsTest (PXTest):

  def post (self, reward):
    """
    Sends a post of a deal with the given reward and mines a block.
    """

    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86_400, "r": reward, "co": 0,
    }]})
    self.generate (1)

  def run (self):
    self.mainLogger.info ("Setting up test accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("worker", "g")
    self.generate (1)
    self.giftCoins ({"poster": 100_000})

    self.mainLogger.info ("Default minimum reward...")
    params = self.getRpc ("getjobsparams")
    self.assertEqual (params["min_job_reward"], 1_000)
    self.assertEqual (params["max_live_jobs"], 10_000)
    self.post (999)
    self.assertEqual (self.getJobs (), {})
    self.post (1_000)
    self.assertEqual (len (self.getJobs ()), 1)

    self.mainLogger.info ("Raising the minimum reward...")
    self.setParams (min_job_reward=2_000)
    self.assertEqual (self.getRpc ("getjobsparams")["min_job_reward"], 2_000)
    # The new minimum applies to the post in the next block.
    self.post (1_000)
    self.assertEqual (len (self.getJobs ()), 1)
    self.post (2_000)
    self.assertEqual (len (self.getJobs ()), 2)

    self.mainLogger.info ("Freezing the board with a limit of zero...")
    self.setParams (max_live_jobs=0)
    self.assertEqual (self.getRpc ("getjobsparams")["max_live_jobs"], 0)
    self.post (2_000)
    jobs = self.getJobs ()
    self.assertEqual (len (jobs), 2)

    # The jobs on the board can still be settled.  The worker gets the
    # reward of 1'000 less 3% tax.
    jobId = min (jobs)
    self.sendMove ("worker", {"j": [
      {"a": jobId},
      {"dl": jobId, "confirm": True},
    ]})
    self.generate (1)
    self.sendMove ("poster", {"j": [{"dl": jobId, "confirm": True}]})
    self.generate (1)
    assert jobId not in self.getJobs ()
    self.assertEqual (self.getAccounts ()["worker"].getBalance (), 970)


if __name__ == "__main__":
  JobsCapsTest ().main ()
