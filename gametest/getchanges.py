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

from pxtest import PXTest, offsetCoord

"""
Tests the getchanges RPC:  The changes since some block, applied to the full
state at that block, must give exactly the full state now.
"""


def coordKey (c):
  return (c["x"], c["y"])


# For each kind of entity in getchanges, the RPC returning all of them (with
# its arguments) and how to get the key of one from its JSON.
KINDS = {
  "accounts": ("getaccounts", {}, lambda e: e["name"]),
  "buildings": ("getbuildings", {}, lambda e: e["id"]),
  "characters": ("getcharacters", {}, lambda e: e["id"]),
  "groundloot": ("getgroundloot", {}, lambda e: coordKey (e["position"])),
  "ongoings": ("getongoings", {}, lambda e: e["id"]),
  "regions": ("getregions", {"fromheight": 0}, lambda e: e["id"]),
}


class GetChangesTest (PXTest):

  def fullState (self):
    """
    Returns the current block hash and the full state of all kinds
    of entities, keyed by entity.
    """

    state = {}
    for kind, (method, args, key) in KINDS.items ():
      state[kind] = {key (e): e for e in self.getRpc (method, **args)}

    return self.rpc.game.getnullstate ()["blockhash"], state

  def record (self):
    """
    Records the current state, so that the changes since then are verified
    in expectChanges.
    """

    self.recorded.append (self.fullState ())

  def expectChanges (self):
    """
    Verifies the changes since every recorded state against the current one.
    """

    tip, now = self.fullState ()
    for block, state in self.recorded + [(tip, now)]:
      changes = self.getRpc ("getchanges", fromblock=block)
      for kind, (_, _, key) in KINDS.items ():
        cur = dict (state[kind])
        for e in changes[kind]["changed"]:
          cur[key (e)] = e
        # Entities may also be created and deleted in between.
        for k in changes[kind]["deleted"]:
          cur.pop (coordKey (k) if isinstance (k, dict) else k, None)
        self.assertEqual (cur, now[kind])
        if block == tip:
          self.assertEqual (changes[kind], {"changed": [], "deleted": []})

  def run (self):
    self.mainLogger.info ("Setting up test situation...")
    self.initAccount ("domob", "r")
    self.initAccount ("andy", "r")
    self.initAccount ("target", "b")
    self.generate (1)
    self.build ("checkmark", "domob", {"x": 0, "y": 0}, rot=0)
    building = max (self.getBuildings ().keys ())
    self.giftCoins ({"domob": 10_000, "andy": 10_000})
    self.dropIntoBuilding (building, "domob", {"foo": 10})
    self.createCharacters ("andy")
    self.createCharacters ("domob")
    self.createCharacters ("target")
    self.generate (1)
    self.changeCharacterVehicle ("domob", "light attacker")
    self.changeCharacterVehicle ("target", "light attacker")
    self.recorded = []
    self.record ()

    self.mainLogger.info ("Trading...")
    self.sendMove ("domob", {"x": [{"b": building, "i": "foo", "n": 5,
                                    "ap": 10}]})
    self.sendMove ("andy", {"x": [{"b": building, "i": "foo", "n": 2,
                                   "bp": 10}]})
    self.sendMove ("andy", {"x": [{"b": building, "i": "foo", "n": 2,
                                   "bp": 5}]})
    self.generate (1)
    self.record ()

    # These change only data shown with a building and an account:  items
    # in the building, and the coins reserved by andy's partially filled bid.
    self.sendMove ("domob", {"x": [{"b": building, "i": "foo", "n": 1,
                                    "t": "andy"}]})
    self.generate (1)
    self.record ()
    self.sendMove ("domob", {"x": [{"b": building, "i": "foo", "n": 1,
                                    "ap": 5}]})
    self.generate (1)
    self.record ()

    self.mainLogger.info ("Picking up loot and prospecting...")
    pos = {"x": -1032, "y": 1270}
    self.moveCharactersTo ({"andy": pos})
    self.dropLoot (pos, {"foo": 3})
    self.record ()
    self.getCharacters ()["andy"].sendMove ({"pu": {"f": {"foo": 3}}})
    self.generate (1)
    self.getCharacters ()["andy"].sendMove ({"prospect": {}})
    self.generate (1)
    self.record ()

    self.mainLogger.info ("Fighting...")
    offset = {"x": -1100, "y": 1042}
    self.moveCharactersTo ({
      "domob": offset,
      "target": offsetCoord ({"x": 5, "y": 0}, offset, False),
    })
    self.generate (1)
    self.record ()
    self.setCharactersHP ({"target": {"a": 1, "s": 0}})
    self.generate (15)
    assert "target" not in self.getCharacters ()
    self.expectChanges ()

    self.mainLogger.info ("Testing reorgs...")
    snapshot = self.env.snapshot ()
    self.giftCoins ({"andy": 1})
    self.generate (1)
    orphaned, _ = self.fullState ()
    assert self.getRpc ("getchanges", fromblock=orphaned) is not None
    snapshot.restore ()
    self.generate (3)
    self.assertEqual (self.getRpc ("getchanges", fromblock=orphaned), None)
    self.expectChanges ()

    self.mainLogger.info ("Testing unknown and invalid blocks...")
    self.assertEqual (self.getRpc ("getchanges", fromblock="00" * 32), None)
    self.expectError (-1, "invalid block hash",
                      self.getRpc, "getchanges", fromblock="foo")


if __name__ == "__main__":
  GetChangesTest ().main ()
