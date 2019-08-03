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
This script collects the premine (for which the regtest keys are publicly
known) into the current wallet, so that sufficient balance is available
for all testing.
"""

import utils

import argparse
import sys

desc = "Collects the regtest Xaya premine"
parser = argparse.ArgumentParser (description=desc)
parser.add_argument ("--xaya_rpc_url", required=True,
                     help="JSON-RPC interface of regtest Xaya Core")
parser.add_argument ("--address", default="",
                     help="can be set to override the target address")
args = parser.parse_args ()

logger = utils.setupLogging ()

rpc = utils.connectRegtestRpc (args.xaya_rpc_url, logger)

logger.info ("Adding premine keys...")
for key in ["b69iyynFSWcU54LqXisbbqZ8uTJ7Dawk3V3yhht6ykxgttqMQFjb",
            "b3fgAKVQpMj24gbuh6DiXVwCCjCbo1cWiZC2fXgWEU9nXy6sdxD5"]:
  rpc.importprivkey (key, "premine", False)
multisig = rpc.addmultisigaddress (1,
                                   ["cRH94YMZVk4MnRwPqRVebkLWerCPJDrXGN",
                                    "ceREF8QnXPsJ2iVQ1M4emggoXiXEynm59D"],
                                   "", "legacy")
assert multisig["address"] == "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"

logger.info ("Locating premine...")
genesisHash = rpc.getblockhash (0)
genesisBlk = rpc.getblock (genesisHash, 2)
assert len (genesisBlk["tx"]) == 1
genesisTx = genesisBlk["tx"][0]
assert len (genesisTx["vout"]) == 1
txid = genesisTx["txid"]
vout = 0
amount = genesisTx["vout"][vout]["value"]
logger.info ("Genesis transaction has value of %d CHI" % amount)

txout = rpc.gettxout (txid, vout)
if txout is None:
  logger.fatal ("Premine is already spent!")
  sys.exit (1)

addr = args.address
if addr == "":
  addr = rpc.getnewaddress ()
logger.info ("Collecting premine coins to %s..." % addr)

fee = 0.01
inputs = [{"txid": txid, "vout": vout}]
outputs = [{addr: amount - fee}]
rawTx = rpc.createrawtransaction (inputs, outputs)
signed = rpc.signrawtransactionwithwallet (rawTx)
assert signed["complete"]
rpc.sendrawtransaction (signed["hex"], 0)
rpc.generatetoaddress (1, rpc.getnewaddress ())
