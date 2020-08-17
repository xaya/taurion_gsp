#!/usr/bin/env python3

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
This script can be used to create many characters in the Taurion game world
on a regtest chain.  By using it, the game can be stress tested.
"""

import utils

import jsonrpclib

import argparse
import collections
import sys

DEV_ADDRESS = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"
COST = 1

desc = "Creates Taurion characters"
parser = argparse.ArgumentParser (description=desc)
parser.add_argument ("--xaya_rpc_url", required=True,
                     help="JSON-RPC interface of regtest Xaya Core")
parser.add_argument ("--gsp_rpc_url", required=True,
                     help="JSON-RPC interface of the Taurion GSP")
parser.add_argument ("--faction", required=True,
                     help="faction for created characters ('r', 'g' or 'b')")
parser.add_argument ("--vehicle", default=None,
                     help="vehicle type to change into (if set)")
parser.add_argument ("--fitments", default="",
                     help="semicolon-separated list of fitments to apply")
parser.add_argument ("--name_template", required=True,
                     help="printf template for the names that will be used")
parser.add_argument ("--count", type=int, required=True,
                     help="number of characters to create")
parser.add_argument ("--batch_size", type=int, default=20,
                     help="number of characters to create per name")
args = parser.parse_args ()

################################################################################

logger = utils.setupLogging ()

if args.faction not in ["r", "g", "b"]:
  logger.fatal ("Invalid faction: %s" % args.faction)
  sys.exit (1)

rpc = utils.connectRegtestRpc (args.xaya_rpc_url, logger)
gsp = utils.connectGspRpc (args.gsp_rpc_url, logger)

buildings = utils.readGsp (rpc, logger, gsp.getbuildings)
bId = None
for b in buildings:
  if b["type"] == "%s ss" % args.faction:
    bId = b["id"]
    break

assert bId is not None
logger.info ("Using building ID %d for vehicle / fitment changes" % bId)

names = []
for i in range ((args.count + args.batch_size - 1) // args.batch_size):
  names.append (args.name_template % i)

def getAccount (name):
  """
  Returns the account data for the given name, or None if there is none.
  """

  accounts = utils.readGsp (rpc, logger, gsp.getaccounts)

  for a in accounts:
    if a["name"] == name:
      return a

  return None

for nm in names:
  if getAccount (nm) is None:
    logger.info ("Initialising account %s for faction %s..."
                    % (nm, args.faction))
    utils.sendMove (rpc, logger, nm, {"a": {"init": {"faction": args.faction}}})

if rpc.getrawmempool ():
  utils.mineBlocks (rpc)

existingCharOwners = set ()
for c in utils.readGsp (rpc, logger, gsp.getcharacters):
  existingCharOwners.add (c["owner"])

for nm in names:
  if nm in existingCharOwners:
    logger.fatal ("Account %s already has characters" % nm)
    sys.exit (1)

  acc = getAccount (nm)
  assert acc is not None
  if acc["faction"] != args.faction:
    logger.fatal ("%s is faction %s" % (nm, acc["faction"]))
    sys.exit (1)
  logger.debug ("%s is initialised to faction %s" % (nm, acc["faction"]))

items = collections.Counter ()
if args.vehicle is not None:
  items[args.vehicle] += 1
if args.fitments:
  fitments = args.fitments.split (";")
  for f in fitments:
    items[f] += 1
else:
  fitments = []

################################################################################

logger.info ("Creating %d characters..." % args.count)

done = 0
while done < args.count:
  now = min (args.batch_size, args.count - done)
  done += now
  percent = (done * 100) // args.count

  assert names
  nm = names[0]
  names = names[1:]
  logger.info ("Processing %d characters with %s (%d%% done)..."
                  % (now, nm, percent))

  # Create the characters.
  mv = {"nc": [{}] * now}
  options = {
    "sendCoins":
      {
        DEV_ADDRESS: COST * now,
      },
  }
  utils.sendMove (rpc, logger, nm, mv, options)

  # God-mode drop required items.
  fungible = {}
  for itm, cnt in items.items ():
    fungible[itm] = cnt * now
  cmd = {
    "god":
      {
        "drop":
          [
            {
              "building": {"id": bId, "a": nm},
              "fungible": fungible,
            },
          ],
      },
  }
  utils.adminCommand (rpc, logger, cmd)
  utils.mineBlocks (rpc)

  # Enter the building with all characters.
  chars = utils.readGsp (rpc, logger, gsp.getcharacters)
  cIds = []
  for c in chars:
    if c["owner"] == nm:
      cIds.append (c["id"])
  assert len (cIds) == now, "%d != %d" % (len (cIds), now)
  cIdsStr = ",".join (["%d" % id for id in cIds])
  mv = {"c": {cIdsStr: {"eb": bId}}}
  utils.sendMove (rpc, logger, nm, mv)
  utils.mineBlocks (rpc)

  # Change vehicle, fitments, and exit building.
  chars = utils.readGsp (rpc, logger, gsp.getcharacters)
  for c in chars:
    if c["owner"] != nm:
      continue
    if "inbuilding" not in c or c["inbuilding"] != bId:
      logger.fatal ("Failed to enter building %d with character %d"
                        % (bId, c["id"]))
      sys.exit (1)
  upd = {"fit": fitments, "xb": {}}
  if args.vehicle is not None:
    upd["v"] = args.vehicle
  mv = {"c": {cIdsStr: upd}}
  utils.sendMove (rpc, logger, nm, mv)
  utils.mineBlocks (rpc)

  # Verify all worked.
  chars = utils.readGsp (rpc, logger, gsp.getcharacters)
  for c in chars:
    if c["owner"] != nm:
      continue
    if "inbuilding" in c:
      logger.fatal ("Failed to exit building with character %d" % c["id"])
      sys.exit (1)
    if args.vehicle is not None and c["vehicle"] != args.vehicle:
      logger.fatal ("Failed to change vehicle with character %d" % c["id"])
      sys.exit (1)
    if fitments != c["fitments"]:
      logger.fatal ("Failed to change fitments with character %d" % c["id"])
      sys.exit (1)
