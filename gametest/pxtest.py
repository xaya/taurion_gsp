from xayagametest.testcase import XayaGameTest

import os
import os.path
import re
from jsonrpclib import ProtocolError


class PXTest (XayaGameTest):
  """
  Integration test for the Project X game daemon.
  """

  def __init__ (self):
    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."
    pxd = os.path.join (top_builddir, "src", "pxd")
    super (PXTest, self).__init__ ("mv", pxd)

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
