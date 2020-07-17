#   GSP for the Taurion blockchain game
#   Copyright (C) 2019-2020  Autonomous Worlds Ltd
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

from xayagametest.testcase import XayaGameTest

from proto import config_pb2

import collections
import os
import os.path


GAMEID = "tn"


def offsetCoord (c, offs, inverse):
  """
  Returns c + offs or c - offs if inverse is True.
  """

  factor = 1
  if inverse:
    factor = -1

  return {
    "x": c["x"] + factor * offs["x"],
    "y": c["y"] + factor * offs["y"],
  }


class Character (object):
  """
  Wrapper class around a character in the game state (already existing).
  This gives utility access for test cases.
  """

  def __init__ (self, test, data):
    """
    Constructs the wrapper.  It records a reference to the PXTest case
    (so that it can send moves) and the JSON data from the game state.
    """

    self.test = test
    self.data = data

  def getId (self):
    return self.data["id"]

  def getIdStr (self):
    """
    Returns the character ID as a string, suitable for indexing
    JSON dictionaries in commands.
    """

    return "%d" % self.getId ()

  def getOwner (self):
    return self.data["owner"]

  def isInBuilding (self):
    return "inbuilding" in self.data

  def getBuildingId (self):
    return self.data["inbuilding"]

  def getPosition (self):
    return self.data["position"]

  def isMoving (self):
    return "movement" in self.data

  def getSpeed (self):
    return self.data["speed"]

  def getBusy (self):
    """
    If the character is busy, looks up the ongoing operation and returns
    it.  Returns None if the character is not busy.

    The returned operation object is modified from the original "getongoings"
    RPC result to make it easier to work with.  We remove the ongoing ID and
    character ID, and translate the "height" to "blocks" (based on the
    current Xaya block height).
    """

    if "busy" in self.data:
      opId = self.data["busy"]
      ongoings = self.test.getRpc ("getongoings")
      for o in ongoings:
        if o["id"] == opId:
          # Translate to make it easier for expecting in tests.
          del o["id"]
          assert o["characterid"] == self.getId ()
          del o["characterid"]
          del o["start_height"]
          o["blocks"] = o["end_height"] - self.test.rpc.xaya.getblockcount ()
          del o["end_height"]
          return o
      raise AssertionError ("Character busy %d not found in ongoings", opId)

    return None

  def getFungibleInventory (self):
    return collections.Counter (self.data["inventory"]["fungible"])

  def sendMove (self, mv):
    """
    Sends a move to update the given character with the given data.
    """

    idStr = self.getIdStr ()
    return self.test.sendMove (self.data["owner"], {"c": {idStr: mv}})

  def findPath (self, target):
    """
    Computes the findpath output from the current position to the given
    target, and returns it as encoded string suitable for a "wp" move.
    """

    path = self.test.rpc.game.findpath (source=self.getPosition (),
                                        target=target,
                                        faction=self.data["faction"],
                                        l1range=1000,
                                        exbuildings=[])
    return path["wp"]

  def moveTowards (self, target):
    """
    Sends a move with waypoints matching the findpath output from the
    current position to target.
    """

    return self.sendMove ({"wp": self.findPath (target)})

  def expectPartial (self, expected):
    """
    Expects that the data matches the values in the expected dictionary.
    Keys that are not present in expected are ignored (and may be present
    in the actual data).
    """

    for key, val in expected.items ():
      self.test.assertEqual (self.data[key], val)


class Building (object):
  """
  Basic handle for a building in the game state.
  """

  def __init__ (self, test, data):
    self.test = test
    self.data = data

  def getId (self):
    return self.data["id"]

  def getType (self):
    return self.data["type"]

  def getFaction (self):
    return self.data["faction"]

  def getOwner (self):
    if "owner" in self.data:
      return self.data["owner"]
    return None

  def getCentre (self):
    return self.data["centre"]

  def isFoundation (self):
    return "foundation" in self.data

  def getOngoingConstruction (self):
    """
    Returns the ongoing operation data for the building's construction
    (from foundation to full building) or None if there is no ongoing
    construction for it.

    To make asserting of the value easy, the IDs (ongoing and building ID)
    are removed, and the "height" value is translated to "blocks" based
    on the current height of Xaya Core.
    """

    if "construction" not in self.data:
      return None
    if "ongoing" not in self.data["construction"]:
      return None

    opId = self.data["construction"]["ongoing"]
    ongoings = self.test.getRpc ("getongoings")
    for o in ongoings:
      if o["id"] == opId:
        del o["id"]
        assert o["buildingid"] == self.getId ()
        del o["buildingid"]
        del o["start_height"]
        o["blocks"] = o["end_height"] - self.test.rpc.xaya.getblockcount ()
        del o["end_height"]
        return o

    raise AssertionError ("Ongoing construction %d not found in ongoings", opId)

  def getConstructionInventory (self):
    constr = self.data["construction"]
    return collections.Counter (constr["inventory"]["fungible"])

  def getFungibleInventory (self, account):
    inv = self.data["inventories"]
    if account not in inv:
      return collections.Counter ()
    return collections.Counter (inv[account]["fungible"])

  def sendMove (self, mv):
    """
    Sends a move to update the given building with the given data.
    """

    idStr = "%s" % self.getId ()
    return self.test.sendMove (self.data["owner"], {"b": {idStr: mv}})


class Account (object):
  """
  Basic handle for an account (Xaya name) in the game state.
  """

  def __init__ (self, data):
    self.data = data

  def getName (self):
    return self.data["name"]

  def getFaction (self):
    return self.data["faction"]

  def getBalance (self):
    return self.data["balance"]


class Region (object):
  """
  Basic handle for a region in the game state.
  """

  def __init__ (self, data):
    self.data = data

  def getId (self):
    return self.data["id"]

  def getResource (self):
    """
    Returns the type and remaining amount of mine-able resource at
    the current region.
    """

    if "resource" not in self.data:
      return None

    return self.data["resource"]["type"], self.data["resource"]["amount"]


class PXTest (XayaGameTest):
  """
  Integration test for the Tauron game daemon.
  """

  cfg = None

  def __init__ (self):
    binary = self.getBuildPath ("src", "tauriond")
    super (PXTest, self).__init__ (GAMEID, binary)

  def getBuildPath (self, *parts):
    """
    Returns the builddir (to get the GSP binary and roconfig file).
    It retrieves what should be the top builddir and then adds on the parts
    with os.path.join.
    """

    top = os.getenv ("top_builddir")
    if top is None:
      top = ".."

    return os.path.join (top, *parts)

  def splitPremine (self):
    """
    Splits the premine coin into smaller outputs, so that there are more
    than a single outputs in the wallet.  Some tests require this or else
    name_update's will fail for some reason.
    """

    for i in range (10):
      self.rpc.xaya.sendtoaddress (self.rpc.xaya.getnewaddress (), 100)
    self.generate (1)

  def getRpc (self, method, *args, **kwargs):
    """
    Calls the given "read-type" RPC method on the game daemon and returns
    the "data" field (holding the main data).
    """

    return self.getCustomState ("data", method, *args, **kwargs)

  def moveWithPayment (self, name, move, devAmount):
    """
    Sends a move (name_update for the given name) and also includes the
    given payment to the developer address.
    """

    addr = self.roConfig ().params.dev_addr
    return self.sendMove (name, move, {"sendCoins": {addr: devAmount}})

  def roConfig (self):
    """
    Returns the roconfig protocol buffer.
    """

    if self.cfg is None:
      self.cfg = config_pb2.ConfigData ()
      with open (self.getBuildPath ("proto", "roconfig.pb"), "rb") as f:
        self.cfg.ParseFromString (f.read ())
      self.cfg.MergeFrom (self.cfg.regtest_merge)
      self.cfg.ClearField ("regtest_merge")

    assert self.cfg is not None
    return self.cfg

  def initAccount (self, name, faction):
    """
    Utility method to initialise an account.
    """

    move = {
      "a":
        {
          "init":
            {
              "faction": faction,
            },
        },
    }

    return self.sendMove (name, move)

  def createCharacters (self, owner, num=1):
    """
    Utility method to create multiple characters for a given owner.
    """

    cost = self.roConfig ().params.character_cost
    return self.moveWithPayment (owner, {"nc": [{}] * num}, num * cost)

  def getCharacters (self):
    """
    Retrieves the existing characters from the current game state.  The result
    is a dictionary indexed by owner.  If multiple names have the same owner,
    then the second will have the key "owner 2", the third "owner 3" and so on.
    """

    res = {}
    for c in self.getRpc ("getcharacters"):
      assert "owner" in c
      nm = c["owner"]
      idx = 2
      while nm in res:
        nm = "%s %d" % (c["owner"], idx)
        idx += 1
      res[nm] = Character (self, c)

    return res

  def moveCharactersTo (self, charTargets):
    """
    Moves all characters from the dictionary to the given coordinates.
    This issues a god-mode teleport command and then generates one block
    to ensure that all characters are moved after return.
    """

    chars = self.getCharacters ()
    teleport = {}
    for nm, c in charTargets.items ():
      idStr = chars[nm].getIdStr ()
      teleport[idStr] = c

    self.adminCommand ({"god": {"teleport": teleport}})
    self.generate (1)

    chars = self.getCharacters ()
    for nm, c in charTargets.items ():
      self.assertEqual (chars[nm].getPosition (), c)

  def setCharactersHP (self, charHP):
    """
    Sets the HP and max HP of the characters with the given owners.
    """

    chars = self.getCharacters ()
    sethp = {}
    for nm, c in charHP.items ():
      idStr = chars[nm].getIdStr ()
      sethp[idStr] = c

    self.adminCommand ({"god": {"sethp": {"c": sethp}}})
    self.generate (1)

  def changeCharacterVehicle (self, char, vehicleType, fitments=[]):
    """
    Changes the vehicle of the given character to the given type.  This is
    done through god-mode, by dropping the vehicle type into an ancient
    building and changing there.
    """

    b = self.getBuildings ()[1]
    self.assertEqual (b.getFaction (), "a")
    pos = offsetCoord (b.getCentre (), {"x": 30, "y": 0}, False)
    self.moveCharactersTo ({char: pos})

    c = self.getCharacters ()[char]
    c.sendMove ({"eb": b.getId ()})
    inv = {vehicleType: 1}
    for f in fitments:
      if f in inv:
        ++inv[f]
      else:
        inv[f] = 1
    self.dropIntoBuilding (b.getId (), c.getOwner (), inv)

    # Make sure that we can change vehicle and fitments by maxing the
    # character's HP (just in case).
    maxHp = self.getCharacters ()[char].data["combat"]["hp"]["max"]
    self.setCharactersHP ({
      char: {"a": maxHp["armour"], "s": maxHp["shield"]},
    })

    self.getCharacters ()[char].sendMove ({
      "v": vehicleType,
      "fit": fitments,
      "xb": {},
    })
    self.generate (1)

  def build (self, typ, owner, centre, rot):
    """
    Issues a god-mode command to place a building with the given data.
    owner set to None places an ancients building.
    """

    self.adminCommand ({"god": {"build": [{
      "t": typ,
      "o": owner,
      "c": centre,
      "rot": rot,
    }]}})
    self.generate (1)

  def dropLoot (self, position, fungible):
    """
    Issues a god-mode command to drop loot on the ground.  fungible should be
    a dictionary mapping item-type strings to corresponding counts.
    """

    self.adminCommand ({"god": {"drop": [{
      "pos": position,
      "fungible": fungible,
    }]}})
    self.generate (1)

  def dropIntoBuilding (self, buildingId, account, fungible):
    """
    Issues a god-mode command to add loot into the inventory of a user
    account inside some building.
    """

    self.adminCommand ({"god": {"drop": [{
      "building": {"id": buildingId, "a": account},
      "fungible": fungible,
    }]}})
    self.generate (1)

  def giftCoins (self, gifts):
    """
    Issues a gift-coins god-mode command, adding coins to the balance of the
    accounts as per the dictionary.
    """

    self.adminCommand ({"god": {"giftcoins": gifts}})
    self.generate (1)

  def getAccounts (self):
    """
    Returns all accounts with non-trivial data in the current game state.
    """

    res = {}
    for a in self.getRpc ("getaccounts"):
      handle = Account (a)
      nm = handle.getName ()
      assert nm not in res
      res[nm] = handle

    return res

  def getBuildings (self):
    """
    Returns all buildings in the game state.
    """

    res = {}
    for b in self.getRpc ("getbuildings"):
      handle = Building (self, b)
      curId = handle.getId ()
      assert curId not in res
      res[curId] = handle

    return res

  def getLoot (self, pos):
    """
    Returns the ground-loot inventory at a given location.
    """

    loot = self.getRpc ("getgroundloot")
    for l in loot:
      if l["position"] == pos:
        return collections.Counter (l["inventory"]["fungible"])

    return collections.Counter ()

  def getRegion (self, regionId):
    """
    Retrieves data for the given region from the current game state.  This
    handles also the case that the region only has trivial data and is not
    explicitly present.
    """

    for r in self.getRpc ("getregions", fromheight=0):
      if r["id"] == regionId:
        return Region (r)

    data = {"id": regionId}
    return Region (data)

  def getRegionAt (self, pos):
    """
    Returns the region data from the game state for the region at the
    given coordinate.
    """

    data = self.rpc.game.getregionat (coord=pos)
    return self.getRegion (data["id"])
