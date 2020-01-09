#!/usr/bin/env python

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020  Autonomous Worlds Ltd
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
Tests the Charon integration of tauriond.
"""

from pxtest import PXTest

import jsonrpclib

import logging
import os
import os.path
import shutil
import socket
import subprocess
import threading
import time


# Accounts on chat.xaya.io (XID mainnet) for use with testing.
# See src/testutils.cpp for more details.
TEST_ACCOUNTS = [
  ("xmpptest1", "CkEfa5+WT2Rc5/TiMDhMynAbSJ+DY9FmE5lcWgWMRQWUBV5UQsgjiBWL302N4k"
                "dLZYygJVBVx3vYsDNUx8xBbw27WA=="),
  ("xmpptest2", "CkEgOEFNwRdLQ6uD543MJLSzip7mTahM1we9GDl3S5NlR49nrJ0JxcFfQmDbbF"
                "4C4OpqSlTpx8OG6xtFjCUMLh/AGA=="),
]
XMPP_SERVER = "chat.xaya.io"
PUBSUB = "pubsub.chat.xaya.io"


def testAccountJid (acc):
  return "%s@%s" % (acc[0], XMPP_SERVER)


class CharonClient ():
  """
  Wrapper around a running tauriond process that is started as Charon client
  (without Xaya Core RPC URL).  It is a context manager that handles starting
  and stopping of the client.
  """

  def __init__ (self, binary, basedir, rpcport):
    self.log = logging.getLogger ("charonclient")

    self.binary = binary
    self.rpcport = rpcport

    self.datadir = os.path.join (basedir, "charonclient")
    self.log.info ("Creating data directory for charon client in %s"
                    % self.datadir)
    shutil.rmtree (self.datadir, ignore_errors=True)
    os.mkdir (self.datadir)

    self.rpc = jsonrpclib.Server ("http://localhost:%d" % self.rpcport)
    self.proc = None

  def __enter__ (self):
    self.log.info ("Starting charon client process...")
    assert self.proc is None

    args = [self.binary]
    args.extend (["--datadir", self.datadir])
    args.append ("--game_rpc_port=%d" % self.rpcport)
    args.extend (["--charon", "client"])
    args.extend (["--charon_server_jid", testAccountJid (TEST_ACCOUNTS[0])])
    args.extend (["--charon_client_jid", testAccountJid (TEST_ACCOUNTS[1])])
    args.extend (["--charon_password", TEST_ACCOUNTS[1][1]])

    envVars = dict (os.environ)
    envVars["GLOG_log_dir"] = self.datadir
    self.proc = subprocess.Popen (args, env=envVars)

    # Wait until the client's RPC interface is up.
    while True:
      try:
        self.rpc.getregionat (coord={"x": 1, "y": 1})
        return self
      except socket.error as exc:
        self.log.info ("RPC connection error, waiting: %s" % exc)
        time.sleep (0.1)

  def __exit__ (self, exc, value, traceback):
    self.log.info ("Stopping charon client process...")
    assert self.proc is not None
    self.proc.terminate ()
    self.proc.wait ()
    self.proc = None


class Waiter:
  """
  Async call to a waitforchange method, which allows waiting for its result
  and checking that the result is as expected.
  """

  def __init__ (self, fcn, *args):
    def task ():
      self.result = fcn (*args)

    self.thread = threading.Thread (target=task)
    self.thread.start ()

  def assertRunning (self):
    time.sleep (0.1)
    assert self.thread.isAlive ()

  def await (self):
    self.thread.join ()
    return self.result


class CharonTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.initAccount ("domob", "r")
    self.generate (1)

    self.mainLogger.info ("Starting tauriond with Charon server...")
    self.stopGameDaemon ()
    args = []
    args.extend (["--charon", "server"])
    args.extend (["--charon_pubsub_service", PUBSUB])
    args.extend (["--charon_server_jid", testAccountJid (TEST_ACCOUNTS[0])])
    args.extend (["--charon_password", TEST_ACCOUNTS[0][1]])
    self.startGameDaemon (extraArgs=args)

    self.mainLogger.info ("Starting tauriond as Charon client...")
    with CharonClient (self.args.game_daemon,
                       self.basedir, self.basePort + 10) as client:

      self.mainLogger.info ("Testing local RPC...")
      pos = {"x": 10, "y": 50}
      self.assertEqual (client.rpc.getregionat (coord=pos),
                        self.rpc.game.getregionat (coord=pos))

      self.mainLogger.info ("Testing RPC through Charon...")
      self.assertEqual (client.rpc.getaccounts (), self.rpc.game.getaccounts ())

      self.mainLogger.info ("Testing waitforchange...")
      w = Waiter (client.rpc.waitforchange, "")
      w.assertRunning ()
      self.generate (1)
      self.assertEqual (w.await (), self.rpc.xaya.getbestblockhash ())

      self.mainLogger.info ("Testing waitforpendingchange...")
      w = Waiter (client.rpc.waitforpendingchange, 0)
      w.assertRunning ()
      self.createCharacters ("domob", 1)
      self.assertEqual (w.await ()["pending"], {
        "characters": [],
        "newcharacters":
          [
            {"name": "domob", "creations": [{"faction": "r"}]},
          ],
      })


if __name__ == "__main__":
  CharonTest ().main ()
