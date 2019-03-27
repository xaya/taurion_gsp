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

    return self.moveWithPayment (owner, {"nc": data}, CHARACTER_COST)

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

  def createCharacterBlock (self, fmt, faction, lower, upper):
    """
    Creates and positions characters with the given faction and names
    formatted as "fmt % index" as a block on each tile between the
    lower and upper coordinates.

    This is useful to create a bunch of attacking characters for testing
    the killing of some other character.
    """

    nextIndex = 0
    mv = {}
    for x in range (lower["x"], upper["x"] + 1):
      for y in range (lower["y"], upper["y"] + 1):
        nm = fmt % nextIndex
        self.createCharacter (nm, faction)
        mv[nm] = {"x": x, "y": y}
        nextIndex += 1
    self.generate (1)
    self.log.info ("Created %d characters for the block" % nextIndex)

    self.moveCharactersTo (mv)

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
