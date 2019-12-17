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

from xayagametest.testcase import XayaGameTest

import collections
import os
import os.path


GAMEID = "tn"
DEVADDR = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"
CHARACTER_COST = 50


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

  def getPosition (self):
    return self.data["position"]

  def isMoving (self):
    return "movement" in self.data

  def getSpeed (self):
    return self.data["speed"]

  def getBusy (self):
    if "busy" in self.data:
      return self.data["busy"]
    return None

  def getFungibleInventory (self):
    return collections.Counter (self.data["inventory"]["fungible"])

  def sendMove (self, mv):
    """
    Sends a move to update the given character with the given data.
    """

    idStr = self.getIdStr ()
    return self.test.sendMove (self.data["owner"], {"c": {idStr: mv}})

  def expectPartial (self, expected):
    """
    Expects that the data matches the values in the expected dictionary.
    Keys that are not present in expected are ignored (and may be present
    in the actual data).
    """

    for key, val in expected.iteritems ():
      self.test.assertEqual (self.data[key], val)


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

  def getFungibleBanked (self):
    return collections.Counter (self.data["banked"]["fungible"])


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

  def __init__ (self):
    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."
    binary = os.path.join (top_builddir, "src", "tauriond")
    super (PXTest, self).__init__ (GAMEID, binary)

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

    return self.sendMove (name, move, {"sendCoins": {DEVADDR: devAmount}})

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

    return self.moveWithPayment (owner, {"nc": [{}] * num},
                                 num * CHARACTER_COST)

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
    for nm, c in charTargets.iteritems ():
      idStr = chars[nm].getIdStr ()
      teleport[idStr] = c

    self.adminCommand ({"god": {"teleport": teleport}})
    self.generate (1)

    chars = self.getCharacters ()
    for nm, c in charTargets.iteritems ():
      self.assertEqual (chars[nm].getPosition (), c)

  def setCharactersHP (self, charHP):
    """
    Sets the HP and max HP of the characters with the given owners.
    """

    chars = self.getCharacters ()
    sethp = {}
    for nm, c in charHP.iteritems ():
      idStr = chars[nm].getIdStr ()
      sethp[idStr] = c

    self.adminCommand ({"god": {"sethp": sethp}})
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

  def getRegion (self, regionId):
    """
    Retrieves data for the given region from the current game state.  This
    handles also the case that the region only has trivial data and is not
    explicitly present.
    """

    for r in self.getRpc ("getregions"):
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
