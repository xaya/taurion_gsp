#!/usr/bin/env python

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

# This utility script talks to Xaya Core by RPC and finds the end block
# of the Taurion competition (i.e. the block just before the first block
# was beyond the end time, which is what we use for the fame/kill prizes).
#
# The result is block 1'257'663 with hash
# 68d6e70c3821f6c554609841db461098e517536d31714f355cdc08374742f99f.

import sys
import jsonrpclib


# Timestamp of the competition end, as per params.cpp.
END_TIME = 1571148000

if len (sys.argv) != 2:
  sys.exit ("Usage: end-block.py RPC-URL")

rpc = jsonrpclib.Server (sys.argv[1])

def getBlockTime (height):
  blkHash = rpc.getblockhash (height)
  return rpc.getblockheader (blkHash)["time"]

# A block surely before the competition end.
START_BLOCK = 1254000
assert getBlockTime (START_BLOCK) < END_TIME - 24 * 3600

# Find the first block beyond the timestamp.
height = START_BLOCK
while getBlockTime (height) <= END_TIME:
  if height % 100 == 0:
    print ("Trying height %d..." % height)
  height += 1

height -= 1
print ("End block: %d (%s)" % (height, rpc.getblockhash (height)))
