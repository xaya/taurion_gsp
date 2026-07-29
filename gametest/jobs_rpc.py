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
RPC-level test for the jobs read surface, over a real JSON-RPC connection
through the generated stub: the whole-board getjobs method, and getjobsparams
projecting the POST-CLAMP effective runtime params that consensus itself uses.
"""

from pxtest import PXTest


class JobsRpcTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up an account and a small board...")
    self.initAccount ("poster", "r")
    self.generate (1)
    self.lowerRewardFloors ("min-job-reward", "min-deal-reward")
    self.giftCoins ({"poster": 1000000})

    n = 7
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86400, "r": 1000, "co": 0, "terms": "x",
    }] * n})
    self.generate (1)

    self.mainLogger.info ("getjobs returns the whole board, ordered by id...")
    allJobs = self.getRpc ("getjobs")
    self.assertEqual (len (allJobs), n)
    ids = [j["id"] for j in allJobs]
    self.assertEqual (ids, sorted (ids))

    # A settled job leaves the board outright (the row is deleted).
    self.sendMove ("poster", {"j": [{"c": ids[0]}]})
    self.generate (1)
    self.assertEqual ([j["id"] for j in self.getRpc ("getjobs")], ids[1:])

    self.mainLogger.info ("getjobsparams projects post-clamp values...")
    base = self.getRpc ("getjobsparams")
    self.assertEqual (base["max-live-jobs"], 10000)
    self.assertEqual (base["max-jobs-per-poster"], 200)
    self.assertEqual (base["deal-tax-bps"], 300)
    self.assertEqual (base["deal-reaction-window"], 30)

    # Over-ceiling caps saturate, a negative window floors to 0 (a freeze),
    # and the self-bounding economics params -- bounded by the settlement math
    # at the post door, not by a ceiling -- pass through unclamped.
    self.adminCommand ({"param": [
      {"n": "max-live-jobs", "v": 10**9},
      {"n": "max-jobs-per-poster", "v": 10**9},
      {"n": "deal-reaction-window", "v": -1},
      {"n": "deal-tax-bps", "v": 750},
    ]})
    self.generate (1)
    capped = self.getRpc ("getjobsparams")
    self.assertEqual (capped["max-live-jobs"], 100000)
    self.assertEqual (capped["max-jobs-per-poster"], 2000)
    self.assertEqual (capped["deal-reaction-window"], 0)
    self.assertEqual (capped["deal-tax-bps"], 750)

    self.mainLogger.info ("...and RPC == consensus, not a parallel formula.")
    # The reported window is exactly the one a POST snapshots onto the row
    # (min with the posted duration), so a client previewing off this RPC can
    # never preview a value the chain would clamp away.
    self.adminCommand ({"param": [
      {"n": "deal-reaction-window", "v": 2592000 + 1000},   # over the 30d cap
    ]})
    self.generate (1)
    eff = self.getRpc ("getjobsparams")["deal-reaction-window"]
    self.assertEqual (eff, 2592000)
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 2592000, "r": 1000, "co": 0, "terms": "w",
    }]})
    self.generate (1)
    posted = self.newestJob ()
    self.assertEqual (posted["reactionwindow"], eff)

    self.mainLogger.info ("Jobs RPC surface test succeeded.")


if __name__ == "__main__":
  JobsRpcTest ().main ()
