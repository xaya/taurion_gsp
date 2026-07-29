#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2026  Autonomous Worlds Ltd
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
Tests the "roconfig" admin command, which merges admin-sent updates into
the read-only configuration data at runtime.
"""

from proto import config_pb2

from pxtest import PXTest

import base64
import time


class RoConfigTest (PXTest):

  def sendConfigUpdate (self, upd):
    """
    Sends the given ConfigData proto as roconfig merge admin command and
    mines a block so it takes effect.
    """

    encoded = base64.b64encode (upd.SerializeToString ()).decode ("ascii")
    self.adminCommand ({"roconfig": {"merge": encoded}})
    self.generate (1)

  def run (self):
    self.mainLogger.info ("Setting up a test account...")
    self.initAccount ("domob", "r")
    self.generate (1)
    baseCost = self.roConfig ().params.character_cost
    self.giftCoins ({"domob": 100 * baseCost})

    snapshot = self.env.snapshot ()
    basePrizes = set (self.getRpc ("getprizestats").keys ())
    assert "extra" not in basePrizes

    self.mainLogger.info ("Applying a config update...")
    upd = config_pb2.ConfigData ()
    upd.params.character_cost = baseCost + 10
    prize = upd.params.prizes.add ()
    prize.name = "extra"
    prize.number = 1
    prize.probability = 10
    self.sendConfigUpdate (upd)

    # Repeated fields append: the compiled-in prizes are still there and the
    # new one shows up after them in the read path.
    prizes = self.getRpc ("getprizestats")
    self.assertEqual (set (prizes.keys ()), basePrizes | {"extra"})
    self.assertEqual (prizes["extra"]["available"], 1)

    # Scalars overwrite: a character creation paying the old cost is now
    # rejected, while the new cost works.
    self.moveWithPayment ("domob", {"nc": [{}]}, baseCost)
    self.generate (1)
    self.assertEqual (self.getCharacters (), {})
    self.moveWithPayment ("domob", {"nc": [{}]}, baseCost + 10)
    self.generate (1)
    assert "domob" in self.getCharacters ()

    self.mainLogger.info ("Applying a second update...")
    # The second update merges into the previously stored config:  the prize
    # added before is unaffected, while the cost changes again.
    upd = config_pb2.ConfigData ()
    upd.params.character_cost = baseCost + 20
    self.sendConfigUpdate (upd)
    self.assertEqual (set (self.getRpc ("getprizestats").keys ()),
                      basePrizes | {"extra"})
    self.moveWithPayment ("domob", {"nc": [{}]}, baseCost + 10)
    self.generate (1)
    assert "domob 2" not in self.getCharacters ()
    self.moveWithPayment ("domob", {"nc": [{}]}, baseCost + 20)
    self.generate (1)
    assert "domob 2" in self.getCharacters ()

    self.mainLogger.info ("Restarting the daemon...")
    # A restarted process must serve state reads from the stored config
    # right away, before any new block is processed.  The config-dependent
    # read is the first call made to the fresh daemon.
    self.stopGameDaemon ()
    self.startGameDaemon (wait=False)
    prizes = None
    for _ in range (100):
      try:
        prizes = self.getRpc ("getprizestats")
        break
      except Exception:
        time.sleep (0.1)
    self.assertEqual (set (prizes.keys ()), basePrizes | {"extra"})

    self.mainLogger.info ("Testing reorg...")
    # The stored config is consensus state: unwinding the blocks that carried
    # the admin commands must restore the compiled-in configuration.
    snapshot.restore ()
    self.assertEqual (set (self.getRpc ("getprizestats").keys ()), basePrizes)
    self.moveWithPayment ("domob", {"nc": [{}]}, baseCost)
    self.generate (1)
    assert "domob" in self.getCharacters ()

    self.mainLogger.info ("RoConfig admin test succeeded.")


if __name__ == "__main__":
  RoConfigTest ().main ()
