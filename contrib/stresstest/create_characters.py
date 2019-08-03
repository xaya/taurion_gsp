#!/usr/bin/env python2

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

"""
This script can be used to create many characters in the Taurion game world
on a regtest chain.  By using it, the game can be stress tested.
"""

import utils

import jsonrpclib

import argparse
import json
import sys

DEV_ADDRESS = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"
COST = 5

desc = "Creates Taurion characters"
parser = argparse.ArgumentParser (description=desc)
parser.add_argument ("--xaya_rpc_url", required=True,
                     help="JSON-RPC interface of regtest Xaya Core")
parser.add_argument ("--faction", required=True,
                     help="faction for created characters ('r', 'g' or 'b')")
parser.add_argument ("--name", required=True,
                     help="Xaya name that is used to create characters")
parser.add_argument ("--count", type=int, required=True,
                     help="number of characters to create")
parser.add_argument ("--per_block", type=int, default=100,
                     help="how many characters to create per block")
args = parser.parse_args ()

logger = utils.setupLogging ()

if args.faction not in ["r", "g", "b"]:
  logger.fatal ("Invalid faction: %s" % args.faction)
  sys.exit (1)

rpc = utils.connectRegtestRpc (args.xaya_rpc_url, logger)

fullname = "p/" + args.name
try:
  data = rpc.name_show (fullname)
  if not data["ismine"]:
    logger.fatal ("Name is not owned by wallet: %s" % fullname)
    sys.exit (1)
except jsonrpclib.jsonrpc.ProtocolError:
  logger.fatal ("Name does not exist: %s" % fullname)
  sys.exit (1)

logger.info ("Creating %d characters for %s..." % (args.count, args.name))
done = 0
while done < args.count:
  now = min (args.per_block, args.count - done)

  mv = {
    "g":
      {
        "tn":
          {
            "nc": [{"faction": args.faction}] * now,
          },
      },
  }
  mvStr = json.dumps (mv, separators=(",", ":"))

  options = {
    "sendCoins":
      {
        DEV_ADDRESS: COST * now,
      },
  }
  rpc.name_update (fullname, mvStr, options)
  rpc.generatetoaddress (1, rpc.getnewaddress ())

  done += now
  logger.info ("Processed %d / %d characters" % (done, args.count))
