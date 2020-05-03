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

import logging
import sys


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
