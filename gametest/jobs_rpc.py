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
RPC-level test for the hardened jobs read surface, over a real JSON-RPC
connection through the generated stub: the paged getjobspage walk (named
parameter binding, keyset cursor, limit clamping), strict cursor errors
propagating as RPC errors instead of silently reading as zero, the shared
parser on getjobshistory, and the deliberate ABSENCE of any whole-board
getjobs method.
"""

from pxtest import PXTest


class JobsRpcTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up an account and a small board...")
    self.initAccount ("poster", "r")
    self.generate (1)
    # Lower the minimum-reward floors (roconfig 100/1000) the same way an
    # admin would: the suite's rewards predate the floors.  The defaults
    # themselves are exercised in jobs_caps.py.
    self.adminCommand ({"param": [
      {"n": "min-job-reward", "v": 1},
      {"n": "min-bounty-reward", "v": 1},
    ]})
    self.generate (1)
    self.giftCoins ({"poster": 1000000})
    self.build ("checkmark", "poster", {"x": 0, "y": 0}, rot=0)
    bId = max (self.getBuildings ().keys ())

    n = 7
    self.sendMove ("poster", {"j": [{
      "t": "deal", "d": 86400, "r": 1000, "co": 0, "terms": "x",
    }] * n})
    self.generate (1)
    allJobs = self.getJobs ()
    self.assertEqual (len (allJobs), n)
    ids = [j["id"] for j in allJobs]
    self.assertEqual (ids, sorted (ids))

    self.mainLogger.info ("Page walk: first / middle / final / past-end...")
    p1 = self.getRpc ("getjobspage", afterid="0", limit=3)
    self.assertEqual ([j["id"] for j in p1], ids[:3])
    p2 = self.getRpc ("getjobspage", afterid=str (p1[-1]["id"]), limit=3)
    self.assertEqual ([j["id"] for j in p2], ids[3:6])
    p3 = self.getRpc ("getjobspage", afterid=str (p2[-1]["id"]), limit=3)
    self.assertEqual ([j["id"] for j in p3], ids[6:])
    # Continuing past the end proves completion with an empty page.
    self.assertEqual (
        self.getRpc ("getjobspage", afterid=str (ids[-1]), limit=3), [])

    self.mainLogger.info ("Limit edges clamp to the hard cap...")
    for limit in [0, -5, 10**6]:
      page = self.getRpc ("getjobspage", afterid="0", limit=limit)
      self.assertEqual ([j["id"] for j in page], ids)
    self.assertEqual (
        len (self.getRpc ("getjobspage", afterid="0", limit=1)), 1)

    # The shared paged reader clamps its pageSize like the server clamps
    # the RPC limit, so out-of-range requests still walk the whole board
    # instead of truncating it or looping onto an empty page.
    for pageSize in [10**6, 0, -5]:
      self.assertEqual (self.getJobs (pageSize=pageSize), allJobs)
    # A sub-cap pageSize forces a genuine multi-page walk (two full pages
    # plus the short final one) in every default CI run.
    self.assertEqual (self.getJobs (pageSize=3), allJobs)

    self.mainLogger.info ("Malformed cursors error, not restart from 0...")
    for bad in ["junk", "12x", " 1", "-1", "+1", "0x10",
                "99999999999999999999999999"]:
      self.expectError (-1, ".*not a non-negative integer.*",
                        self.rpc.game.getjobspage, afterid=bad, limit=5)
    # INT64_MAX parses fine and naturally yields an empty page.
    self.assertEqual (
        self.getRpc ("getjobspage", afterid=str (2**63 - 1), limit=5), [])

    self.mainLogger.info ("There is no whole-board getjobs method...")
    self.expectError (-32601, ".*", self.rpc.game.getjobs)

    self.mainLogger.info ("History shares the strict cursor parser...")
    jobId = ids[0]
    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    hist = self.getRpc ("getjobshistory", fromtime="0", aftertime="0",
                        afterid="0", limit=1000)
    self.assertEqual ([e["id"] for e in hist], [jobId])
    self.assertEqual (hist[0]["outcome"], "cancelled")
    for field in ["fromtime", "aftertime", "afterid"]:
      kwargs = {"fromtime": "0", "aftertime": "0", "afterid": "0",
                "limit": 10}
      kwargs[field] = "junk"
      self.expectError (-1, ".*not a non-negative integer.*",
                        self.rpc.game.getjobshistory, **kwargs)

    self.mainLogger.info ("Jobs RPC surface test succeeded.")


if __name__ == "__main__":
  JobsRpcTest ().main ()
