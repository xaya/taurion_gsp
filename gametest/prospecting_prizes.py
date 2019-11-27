#!/usr/bin/env python

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019  Autonomous Worlds Ltd
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

# Timestamps when the competition is still active and when it is
# already over.  Note that for some reason we cannot be exact to the
# second here, since the mined block timestamps not always match the
# mocktime exactly.  We verify the correct behaviour with respect to the
# timestamp in unit tests, though.
COMPETITION_RUNNING = 1500000000
COMPETITION_OVER = 1600000000

# Positions where prizes can be won and cannot be won.
POS_WITH_PRIZES = {"x": 3000, "y": 0}
POS_NO_PRIZES = {"x": 1000, "y": 500}


class ProspectingPrizesTest (PXTest):

  def getPrizes (self, nm):
    """
    Returns the prizes (if any) the account with the given name as well as
    its "first character" has won so far (based on what they have banked
    and what the character has in its inventory).
    """

    char = self.getCharacters ()[nm].getFungibleInventory ()
    acc = self.getAccounts ()[nm].getFungibleBanked ()
    items = char + acc

    res = {}
    for item, amount in items.iteritems ():
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
    # Mine a couple of blocks to get a meaningful median time
    # that is before the times we'll use in testing.
    self.rpc.xaya.setmocktime (COMPETITION_RUNNING)
    self.generate (10)

    self.collectPremine ()

    # First test:  Try and retry (with a reorg) prospecting in the
    # same region to get both a silver tier and not silver.
    self.mainLogger.info ("Testing randomisation of prizes...")

    self.initAccount ("prize trier", "r")
    self.initAccount ("prize trier centre", "r")
    self.createCharacters ("prize trier")
    self.createCharacters ("prize trier centre")
    self.generate (1)
    self.moveCharactersTo ({"prize trier": POS_WITH_PRIZES})
    self.moveCharactersTo ({"prize trier centre": POS_NO_PRIZES})
    stillNeedNoSilver = True
    stillNeedSilver = True
    blk = None
    self.getCharacters ()["prize trier"].sendMove ({"prospect": {}})
    self.getCharacters ()["prize trier centre"].sendMove ({"prospect": {}})
    self.generate (9)
    while stillNeedNoSilver or stillNeedSilver:
      self.generate (1)
      blk = self.rpc.xaya.getbestblockhash ()
      self.generate (1)

      prosp = self.getRegionAt (POS_WITH_PRIZES).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier")

      if "silver" in self.getPrizes ("prize trier"):
        stillNeedSilver = False
      else:
        stillNeedNoSilver = False

      self.rpc.xaya.invalidateblock (blk)

    assert blk is not None
    blkOldTime = blk

    # Test the impact of the block time onto received prizes.  After
    # the competition is over, no prizes should be found anymore.  It
    # is possible to have a "beyond" block and then an earlier block with
    # prizes, though.  Thus we mine the block before prospecting ends
    # always after the competition.
    self.mainLogger.info ("Testing time and prizes...")

    self.rpc.xaya.setmocktime (COMPETITION_OVER)
    self.generate (1)

    # There's a 12% chance that we will simply not find a silver prize
    # (with 10% chance) in 20 trials even if we could, but we are fine
    # with that.
    for _ in range (20):
      self.generate (1)
      blk = self.rpc.xaya.getbestblockhash ()

      prosp = self.getRegionAt (POS_WITH_PRIZES).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier")
      self.assertEqual (self.getPrizes ("prize trier"), {})

      self.rpc.xaya.invalidateblock (blk)

    self.rpc.xaya.setmocktime (COMPETITION_RUNNING)
    stillNeedPrize = True
    while stillNeedPrize:
      self.generate (1)
      blk = self.rpc.xaya.getbestblockhash ()

      prosp = self.getRegionAt (POS_WITH_PRIZES).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier")
      if self.getPrizes ("prize trier"):
        stillNeedPrize = False

      self.rpc.xaya.invalidateblock (blk)

    self.mainLogger.info ("No prizes in centre...")
    for _ in range (20):
      self.generate (1)
      blk = self.rpc.xaya.getbestblockhash ()

      prosp = self.getRegionAt (POS_NO_PRIZES).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier centre")
      self.assertEqual (self.getPrizes ("prize trier centre"), {})

      self.rpc.xaya.invalidateblock (blk)

    # Restore the last randomised attempt.  Else we might end up with
    # a long invalid chain, which can confuse the reorg test.
    self.rpc.xaya.reconsiderblock (blkOldTime)

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
      for prize, num in thisPrizes.iteritems ():
        prizesInRegions[prize] += num

    for nm, val in self.getRpc ("getprizestats").iteritems ():
      self.assertEqual (prizesInRegions[nm], val["found"])
    self.log.info ("Found prizes:\n%s" % prizesInRegions)
    self.assertEqual (prizesInRegions["bronze"], 1)
    assert prizesInRegions["silver"] > 0


if __name__ == "__main__":
  ProspectingPrizesTest ().main ()
