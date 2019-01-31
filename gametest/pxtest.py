from xayagametest.testcase import XayaGameTest

import os
import os.path
import re
from jsonrpclib import ProtocolError


GAMEID = "tn"
DEVADDR = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"
CHARACTER_COST = 5


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

  def getPosition (self):
    return self.data["position"]

  def isMoving (self):
    return "movement" in self.data

  def sendMove (self, mv):
    """
    Sends a move to update the given character with the given data.
    """

    idStr = "%d" % self.data["id"]
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


class PXTest (XayaGameTest):
  """
  Integration test for the Project X game daemon.
  """

  def __init__ (self):
    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."
    pxd = os.path.join (top_builddir, "src", "pxd")
    super (PXTest, self).__init__ (GAMEID, pxd)

  def moveWithPayment (self, name, move, devAmount):
    """
    Sends a move (name_update for the given name) and also includes the
    given payment to the developer address.
    """

    return self.sendMove (name, move, {"sendCoins": {DEVADDR: devAmount}})

  def createCharacter (self, owner, name, faction):
    """
    Utility method to send a move creating a character.
    """

    data = {
      "name": name,
      "faction": faction,
    }

    return self.moveWithPayment (owner, {"nc": data}, CHARACTER_COST)

  def getCharacters (self):
    """
    Retrieves the existing characters from the current game state.
    """

    state = self.getGameState ()
    assert "characters" in state

    res = {}
    for c in state["characters"]:
      assert "name" in c
      res[c["name"]] = Character (self, c)

    return res

  def assertEqual (self, a, b):
    """
    Utility method that tests for equality between a and b, yielding a
    nicer error message if it is not true.
    """

    if a == b:
      return

    self.log.error ("Equality assertion failed:\n%s\n  vs\n%s" % (a, b))
    msg = "%s is not equal to %s" % (a, b)
    raise AssertionError (msg)

  def expectError (self, code, msgRegExp, method, *args, **kwargs):
    """
    Calls the method object with the given arguments, and expects that
    an RPC error is raised matching the code and message.
    """

    try:
      method (*args, **kwargs)
      self.log.error ("Expected RPC error with code=%d and message %s"
                        % (code, msgRegExp))
      raise AssertionError ("expected RPC error was not raised")
    except ProtocolError as exc:
      self.log.info ("Caught expected RPC error: %s" % exc)
      (c, m) = exc.args[0]
      self.assertEqual (c, code)
      msgPattern = re.compile (msgRegExp)
      assert msgPattern.match (m)
