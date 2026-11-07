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
Tests the getchanges and getcompactchanges RPCs:  The changes since some
block, applied to the full state at that block, must give exactly the full
state now.
"""


# The keys of a character's JSON that its entry in "moved" of compact changes
# replaces, and the keys of its combat data that one in "combat" replaces.
MOVED_KEYS = {"position", "movement"}
COMBAT_KEYS = {"hp", "target", "effects", "attackers"}


def coordKey (c):
  return (c["x"], c["y"])


def replaceKeys (obj, keys, entry):
  """
  Returns obj with the given keys replaced by those in entry (or removed
  if entry does not have them, as then the JSON has none).
  """

  assert set (entry) <= keys, entry
  res = {k: v for k, v in obj.items () if k not in keys}
  res.update (entry)
  return res


def applyChanges (state, changes, key):
  """
  Applies the changes of one kind of entity to its full state (keyed by
  entity) at their block, and returns the result.
  """

  res = dict (state)
  for e in changes["changed"]:
    res[key (e)] = e
  for e in changes.get ("moved", []):
    res[e["id"]] = replaceKeys (res[e["id"]], MOVED_KEYS | {"id"}, e)
  for e in changes.get ("combat", []):
    assert set (e) == {"id", "combat"} and "hp" in e["combat"], e
    c = res[e["id"]]
    res[e["id"]] = dict (c, combat=replaceKeys (c["combat"], COMBAT_KEYS,
                                                e["combat"]))
  # Entities may also be created and deleted in between.
  for k in changes["deleted"]:
    res.pop (coordKey (k) if isinstance (k, dict) else k, None)
  return res


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
    Verifies the changes since every recorded state against the current one,
    in full and in compact form.
    """

    tip, now = self.fullState ()
    for block, state in self.recorded + [(tip, now)]:
      full = self.getRpc ("getchanges", fromblock=block)
      compact = self.getRpc ("getcompactchanges", fromblock=block)
      for kind, (_, _, key) in KINDS.items ():
        self.assertEqual (set (full[kind]), {"changed", "deleted"})
        for changes in [full, compact]:
          self.assertEqual (applyChanges (state[kind], changes[kind], key),
                            now[kind])
        if block == tip:
          self.assertEqual (full[kind], {"changed": [], "deleted": []})

      # The compact form differs only in characters that changed in parts,
      # which are not changed but moved and/or in combat with just those.
      fullChars = full.pop ("characters")
      compactChars = compact.pop ("characters")
      self.assertEqual (compact, full)
      partial = {e["id"] for e in compactChars.pop ("moved")}
      partial |= {e["id"] for e in compactChars.pop ("combat")}
      assert partial <= {e["id"] for e in fullChars["changed"]}
      fullChars["changed"] = [e for e in fullChars["changed"]
                              if e["id"] not in partial]
      self.assertEqual (compactChars, fullChars)

  def compactCharacters (self):
    """
    Returns the IDs of characters in each list of the compact changes
    of characters since the last recorded state.
    """

    chars = self.getRpc ("getcompactchanges",
                         fromblock=self.recorded[-1][0])["characters"]
    return {k: [e if k == "deleted" else e["id"] for e in v]
            for k, v in chars.items ()}

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

    self.mainLogger.info ("Moving...")
    pos = {"x": -1838, "y": 1040}
    self.moveCharactersTo ({"andy": pos})
    andy = self.getCharacters ()["andy"]
    andy.sendMove ({"wp": self.rpc.game.encodewaypoints (wp=[
      {"x": pos["x"] + 20, "y": pos["y"]},
    ])})
    self.generate (1)
    self.record ()
    self.generate (2)
    self.assertEqual (self.compactCharacters ()["moved"], [andy.getId ()])
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
    offset = {"x": -1838, "y": 1040}
    self.moveCharactersTo ({
      "domob": offset,
      "target": offsetCoord ({"x": 0, "y": 3}, offset, False),
    })
    self.generate (1)
    self.record ()
    chars = self.getCharacters ()
    domob = chars["domob"].getId ()
    target = chars["target"].getId ()
    self.generate (1)
    chars = self.compactCharacters ()
    assert {domob, target} <= set (chars["combat"]), chars
    assert {domob, target}.isdisjoint (chars["changed"])
    hp = self.getCharacters ()["target"].data["combat"]["hp"]
    assert hp["current"] != hp["max"], hp
    self.record ()

    # Setting waypoints is a full change of domob.  But then its steps are
    # just movement while it keeps fighting, as the waypoint is in range.
    self.getCharacters ()["domob"].moveTowards (
        offsetCoord ({"x": 9, "y": 0}, offset, False))
    self.generate (1)
    self.record ()
    self.generate (1)
    chars = self.compactCharacters ()
    assert domob in chars["moved"] and domob in chars["combat"], chars
    assert domob not in chars["changed"]
    pos = self.recorded[-1][1]["characters"][domob]["position"]
    assert self.getCharacters ()["domob"].getPosition () != pos
    self.record ()

    # The attackers are gone once one of them is dead, so the changes are
    # also verified while they fight.
    chars = self.getCharacters ()
    for nm, attacker in [("domob", target), ("target", domob)]:
      self.assertEqual (chars[nm].data["combat"]["attackers"], [attacker])
    self.expectChanges ()
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
    for method in ["getchanges", "getcompactchanges"]:
      self.assertEqual (self.getRpc (method, fromblock="00" * 32), None)
      self.expectError (-1, "invalid block hash",
                        self.getRpc, method, fromblock="foo")


if __name__ == "__main__":
  GetChangesTest ().main ()
