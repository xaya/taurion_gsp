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

import os
import os.path


GAMEID = "tn"
DEVADDR = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"
CHARACTER_COST = 5


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
      assert key in self.data
      self.test.assertEqual (self.data[key], val)


class Account (object):
  """
  Basic handle for an account (Xaya name) in the game state.
  """

  def __init__ (self, data):
    self.data = data

  def getName (self):
    return self.data["name"]


class Region (object):
  """
  Basic handle for a region in the game state.
  """

  def __init__ (self, data):
    self.data = data

  def getId (self):
    return self.data["id"]


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

  def moveWithPayment (self, name, move, devAmount):
    """
    Sends a move (name_update for the given name) and also includes the
    given payment to the developer address.
    """

    return self.sendMove (name, move, {"sendCoins": {DEVADDR: devAmount}})

  def createCharacter (self, owner, faction):
    """
    Utility method to send a move creating a character.
    """

    data = {
      "faction": faction,
    }

    return self.moveWithPayment (owner, {"nc": [data]}, CHARACTER_COST)

  def createCharacters (self, owner, factions):
    """
    Utility method to create multiple characters for a given owner.
    """

    data = [{
      "faction": faction,
    } for faction in factions]

    return self.moveWithPayment (owner, {"nc": data},
                                 len (data) * CHARACTER_COST)

  def getCharacters (self):
    """
    Retrieves the existing characters from the current game state.  The result
    is a dictionary indexed by owner.  If multiple names have the same owner,
    then the second will have the key "owner 2", the third "owner 3" and so on.
    """

    state = self.getGameState ()
    assert "characters" in state

    res = {}
    for c in state["characters"]:
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

  def getAccounts (self):
    """
    Returns all accounts with non-trivial data in the current game state.
    """

    state = self.getGameState ()
    assert "accounts" in state

    res = {}
    for a in state["accounts"]:
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

    state = self.getGameState ()
    assert "regions" in state

    for r in state["regions"]:
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
