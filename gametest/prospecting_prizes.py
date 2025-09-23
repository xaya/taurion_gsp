#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2025  Autonomous Worlds Ltd
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
Tests distribution of prizes for prospecting.
"""

from pxtest import PXTest

# Positions where prizes can be won (with normal chance).
POS = {"x": 3000, "y": 500}


class ProspectingPrizesTest (PXTest):

  def getPrizes (self, nm):
    """
    Returns the prizes (if any) the account with the given name as well as
    its "first character" has won so far (based on what the character has in
    its inventory).
    """

    items = self.getCharacters ()[nm].getFungibleInventory ()

    res = {}
    for item, amount in items.items ():
      suffix = " prize"
      if not item.endswith (suffix):
        continue

      prize = item[:-len (suffix)]
      if prize in res:
        res[prize] += amount
      else:
        res[prize] = amount

    return res

  def run (self):
    # First test:  Try and retry (with a reorg) prospecting in the
    # same region to get both a silver tier and not silver.
    self.mainLogger.info ("Testing randomisation of prizes...")

    self.initAccount ("prize trier", "r")
    self.createCharacters ("prize trier")
    self.generate (1)
    self.moveCharactersTo ({"prize trier": POS})
    stillNeedNoSilver = True
    stillNeedSilver = True
    blk = None
    self.getCharacters ()["prize trier"].sendMove ({"prospect": {}})
    self.generate (10)
    snapshot = self.env.snapshot ()
    while stillNeedNoSilver or stillNeedSilver:
      snapshot.restore ()
      self.generate (1)

      prosp = self.getRegionAt (POS).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier")

      if "silver" in self.getPrizes ("prize trier"):
        stillNeedSilver = False
      else:
        stillNeedNoSilver = False

      snapshot.restore ()

    # Prospect in some regions and verify some basic expectations
    # on the number of prizes found.
    self.mainLogger.info ("Testing prize numbers...")

    sendTo = {}
    regionIds = set ()
    nextInd = 1
    for i in range (2):
      for j in range (10):
        pos = {"x": (-1)**i * 2500, "y": 20 * j}
        region = self.getRegionAt (pos)
        assert "prospection" not in region.data
        assert region.getId () not in regionIds
        regionIds.add (region.getId ())

        nm = "prize numbers %d" % nextInd
        self.initAccount (nm, "r")
        self.createCharacters (nm)
        self.generate (1)
        sendTo[nm] = pos
        nextInd += 1
    self.moveCharactersTo (sendTo)

    chars = self.getCharacters ()
    for nm in sendTo:
      chars[nm].sendMove ({"prospect": {}})
    self.generate (11)

    prizesInRegions = {
      "gold": 0,
      "silver": 0,
      "bronze": 0,
    }
    for nm in self.getAccounts ():
      thisPrizes = self.getPrizes (nm)
      for prize, num in thisPrizes.items ():
        prizesInRegions[prize] += num

    for nm, val in self.getRpc ("getprizestats").items ():
      self.assertEqual (prizesInRegions[nm], val["found"])
    self.log.info ("Found prizes:\n%s" % prizesInRegions)
    self.assertEqual (prizesInRegions["bronze"], 1)
    assert prizesInRegions["silver"] > 0


if __name__ == "__main__":
  ProspectingPrizesTest ().main ()
