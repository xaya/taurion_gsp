from xayagametest.testcase import XayaGameTest

import os
import os.path
import re
from jsonrpclib import ProtocolError


DEVADDR = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"
CHARACTER_COST = 5


class PXTest (XayaGameTest):
  """
  Integration test for the Project X game daemon.
  """

  def __init__ (self):
    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."
    pxd = os.path.join (top_builddir, "src", "pxd")
    super (PXTest, self).__init__ ("px", pxd)

  def moveWithPayment (self, name, move, devAmount):
    """
    Sends a move (name_update for the given name) and also includes the
    given payment to the developer address.
    """

    return self.sendMove (name, move, {"sendCoins": {DEVADDR: devAmount}})

  def characterId (self, charName):
    """
    Finds the ID of the character with the given name.  The ID is returned
    as string, so that it can be used in JSON dictionaries for sending updates
    for that character.
    """

    state = self.getGameState ()
    for c in state['characters']:
      if c['name'] == charName:
        return "%d" % c['id']

    raise AssertionError ("No character with name '%s' found" % charName)

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
