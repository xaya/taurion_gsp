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

  def postPool (self, expectCount):
    """Posts one wanted pool on 'target' and asserts the resulting number
    of live pools (the post is silently rejected at the cap)."""
    self.sendMove ("poster", {"j": [{
      "t": "wanted", "r": 1000, "co": 0, "name": "target", "n": 2,
    }]})
    self.generate (1)
    self.assertEqual (len (self.getJobs ()), expectCount)

  def run (self):
    self.mainLogger.info ("Setting up accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("target", "b")
    self.generate (1)
    self.giftCoins ({"poster": 10000})

    self.mainLogger.info ("Capping pools per target at 2...")
    self.adminCommand ({"param": [
      {"n": "max-bounty-pools-per-target", "v": 2},
    ]})
    self.generate (1)

    self.postPool (1)
    self.postPool (2)
    # At the cap: the third pool is rejected...
    self.postPool (2)

    self.mainLogger.info ("Raising the cap admits the next post...")
    self.adminCommand ({"param": [
      {"n": "max-bounty-pools-per-target", "v": 3},
    ]})
    self.generate (1)
    self.postPool (3)

    self.mainLogger.info ("A 0 cap freezes posting entirely...")
    self.adminCommand ({"param": [{"n": "max-live-jobs", "v": 0}]})
    self.generate (1)
    self.postPool (3)

    self.mainLogger.info ("Removing the overrides resets to the defaults...")
    self.adminCommand ({"param": [
      {"n": "max-live-jobs", "v": None},
      {"n": "max-bounty-pools-per-target", "v": None},
    ]})
    self.generate (1)
    self.postPool (4)

    self.mainLogger.info ("Minimum-reward floors at their defaults...")
    # No floor overrides exist in this suite: 999 is under the default
    # bounty floor (1000) and must reject; 1000 admits.
    self.sendMove ("poster", {"j": [{
      "t": "wanted", "r": 999, "co": 0, "name": "target", "n": 2,
    }]})
    self.generate (1)
    self.assertEqual (len (self.getJobs ()), 4)
    self.postPool (5)

    self.mainLogger.info ("Raising the generic floor gates even that...")
    self.adminCommand ({"param": [{"n": "min-job-reward", "v": 2000}]})
    self.generate (1)
    self.postPool (5)

    self.mainLogger.info ("Jobs caps test succeeded.")


if __name__ == "__main__":
  JobsCapsTest ().main ()
