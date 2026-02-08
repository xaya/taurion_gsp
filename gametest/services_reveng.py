#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020-2025  Autonomous Worlds Ltd
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
Tests basic reverse-engineering service operations.
"""

from pxtest import PXTest


class ServicesRevEngTest (PXTest):

  def run (self):
    self.mainLogger.info ("Setting up initial situation...")
    self.build ("ancient1", None, {"x": 0, "y": 0}, 0)
    building = 1001
    self.assertEqual (self.getBuildings ()[building].getType (), "ancient1")

    self.initAccount ("domob", "g")
    self.generate (1)
    self.giftCoins ({"domob": 100})
    self.dropIntoBuilding (building, "domob", {"test artefact": 10})

    self.mainLogger.info ("Re-rolls of first reveng...")
    snapshot = self.env.snapshot ()
    trials = 10
    # On regtest, the first reveng operation is guaranteed to succeed.
    # If we keep undoing and redoing (fresh) blocks, we will always be in
    # that situation; this ensures (among other things) that undoing the
    # item counter works.
    found = {}
    for _ in range (trials):
      snapshot.restore ()
      self.sendMove ("domob", {"s": [{
        "t": "rve",
        "b": building,
        "i": "test artefact",
        "n": 1,
      }]})
      self.generate (1)
      inv = self.getBuildings ()[building].getFungibleInventory ("domob")
      for t, n in inv.items ():
        if t == "test artefact":
          continue
        self.assertEqual (n, 1)
        if t not in found:
          found[t] = 0
        found[t] += 1
    self.assertEqual (set (found.keys ()), set (["bow bpo", "sword bpo"]))
    self.assertEqual (found["bow bpo"] + found["sword bpo"], trials)
    assert found["bow bpo"] > 0
    assert found["sword bpo"] > 0

    self.mainLogger.info ("Batched reverse engineering...")
    # Note that we still have one operation already done from before.
    self.sendMove ("domob", {"s": [
      {"t": "rve", "b": building, "i": "test artefact", "n": 4},
      {"t": "rve", "b": building, "i": "test artefact", "n": 5},
    ]})
    self.generate (1)
    self.assertEqual (self.getAccounts ()["domob"].getBalance (), 0)
    inv = self.getBuildings ()[building].getFungibleInventory ("domob")
    self.assertEqual (set (inv.keys ()), set (["bow bpo", "sword bpo"]))
    assert inv["bow bpo"] > 0
    assert inv["sword bpo"] > 0
    assert inv["bow bpo"] + inv["sword bpo"] < trials


if __name__ == "__main__":
  ServicesRevEngTest ().main ()
