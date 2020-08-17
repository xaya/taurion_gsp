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

"""
Some common utility code shared between the individual stress-test scripts.
"""

import jsonrpclib

import json
import logging
import sys
import time


def setupLogging ():
  logFmt = "%(asctime)s %(name)s (%(levelname)s): %(message)s"
  logHandler = logging.StreamHandler (sys.stderr)
  logHandler.setFormatter (logging.Formatter (logFmt))

  logger = logging.getLogger ()
  logger.setLevel (logging.INFO)
  logger.addHandler (logHandler)

  return logger


def connectRegtestRpc (url, logger):
  """
  Opens an RPC client connection to the given JSON-RPC url, and verifies
  that it is good and on regtest.
  """

  rpc = jsonrpclib.ServerProxy (url)
  netinfo = rpc.getnetworkinfo ()
  logger.info ("Connected to Xaya Core version %d" % netinfo["version"])

  chaininfo = rpc.getblockchaininfo ()
  if chaininfo["chain"] != "regtest":
    logger.fatal ("Connected to chain %s instead of regtest"
                    % chaininfo["chain"])
    sys.exit (1)

  return rpc


def connectGspRpc (url, logger):
  """
  Opens an RPC connection to the Taurion GSP on the given url
  and verifies that it is good and synced up.
  """

  rpc = jsonrpclib.ServerProxy (url)

  while True:
    state = rpc.getnullstate ()

    if state["state"] == "up-to-date":
      logger.info ("GSP is up-to-date at height %d" % state["height"])
      return rpc

    logger.warning ("GSP is %s, waiting to be up-to-date..." % state["state"])
    time.sleep (1)


def readGsp (rpc, logger, fcn, *args, **kwargs):
  """
  Calls an RPC method on the GSP, verifies that it matches the current
  RPC tip (if not, waits and tries again), and returns the "data" field
  on success.
  """

  blk = rpc.getbestblockhash ()

  while True:
    res = fcn (*args, **kwargs)

    if res["state"] != "up-to-date" or res["blockhash"] != blk:
      logger.warning ("GSP is not yet caught up, waiting...")
      time.sleep (1)
      continue

    return res["data"]


def updateOrRegister (rpc, logger, name, val, options={}):
  """
  Updates or registers the given name with the given value.
  """

  try:
    data = rpc.name_show (name)
    if not data["ismine"]:
      logger.fatal ("Name is not owned by wallet: %s" % name)
      sys.exit (1)
    return rpc.name_update (name, val, options)

  except jsonrpclib.jsonrpc.ProtocolError:
    logger.info ("Name does not exist yet, creating: %s" % name)
    return rpc.name_register (name, val, options)


def sendMove (rpc, logger, name, data, options={}):
  """
  Sends a Taurion move for the given name (without p/ prefix) and
  data.  Optionally adds some options to the name_update.

  If the name does not exist yet, it will be registered.
  """

  fullname = "p/" + name

  mv = {"g": {"tn": data}}
  mvStr = json.dumps (mv, separators=(",", ":"))

  updateOrRegister (rpc, logger, fullname, mvStr, options)


def adminCommand (rpc, logger, data):
  """
  Sends an admin command.
  """

  mv = {"cmd": data}
  mvStr = json.dumps (mv, separators=(",", ":"))

  updateOrRegister (rpc, logger, "g/tn", mvStr)


def mineBlocks (rpc, n=1):
  """
  Mines n blocks.
  """

  rpc.generatetoaddress (n, rpc.getnewaddress ())
