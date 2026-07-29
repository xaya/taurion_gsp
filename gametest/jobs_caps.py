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
Integration test for the jobs-board admission caps and their runtime tuning
through the "param" admin command: a cap enforced at its exact boundary, a
raise taking effect from the next block, the 0-value posting freeze, a
null-value removal resetting to the roconfig default, and the minimum-reward
floors at their real defaults.  (The per-dimension boundary matrix is
unit-tested; this proves the same wiring end-to-end through real admin
commands and moves.)
"""

from pxtest import PXTest


class JobsCapsTest (PXTest):

  def postDeal (self, expectCount, reward=1000):
    """Posts one deal and asserts the resulting number of live jobs (the
    post is silently rejected at the cap)."""
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86400, "r": reward, "co": 0, "terms": "x",
    }]})
    self.generate (1)
    self.assertEqual (len (self.getJobs ()), expectCount)

  def run (self):
    self.mainLogger.info ("Setting up accounts...")
    self.initAccount ("poster", "r")
    self.generate (1)
    self.giftCoins ({"poster": 100000})

    self.mainLogger.info ("Capping jobs per poster at 2...")
    self.adminCommand ({"param": [
      {"n": "max-jobs-per-poster", "v": 2},
    ]})
    self.generate (1)

    self.postDeal (1)
    self.postDeal (2)
    # At the cap: the third post is rejected...
    self.postDeal (2)

    self.mainLogger.info ("Raising the cap admits the next post...")
    self.adminCommand ({"param": [
      {"n": "max-jobs-per-poster", "v": 3},
    ]})
    self.generate (1)
    self.postDeal (3)

    self.mainLogger.info ("A 0 cap freezes posting entirely...")
    self.adminCommand ({"param": [{"n": "max-live-jobs", "v": 0}]})
    self.generate (1)
    self.postDeal (3)

    self.mainLogger.info ("Removing the overrides resets to the defaults...")
    self.adminCommand ({"param": [
      {"n": "max-live-jobs", "v": None},
      {"n": "max-jobs-per-poster", "v": None},
    ]})
    self.generate (1)
    self.postDeal (4)

    self.mainLogger.info ("Minimum-reward floors at their defaults...")
    # No floor overrides exist in this suite: 999 is under the default deal
    # floor (1000) and must reject; 1000 admits.
    self.postDeal (4, reward=999)
    self.postDeal (5)

    self.mainLogger.info ("Raising the generic floor gates even that...")
    self.adminCommand ({"param": [{"n": "min-job-reward", "v": 2000}]})
    self.generate (1)
    self.postDeal (5)

    self.mainLogger.info ("Jobs caps test succeeded.")


if __name__ == "__main__":
  JobsCapsTest ().main ()
