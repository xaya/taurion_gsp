#!/usr/bin/env python3

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
Tests the getversion RPC command.
"""

from pxtest import PXTest


class GetVersionTest (PXTest):

  def run (self):
    res = self.rpc.game.getversion ()
    assert "package" in res
    assert "git" in res

    self.mainLogger.info ("Package version: %s\nGit version: %s"
        % (res["package"], res["git"]))


if __name__ == "__main__":
  GetVersionTest ().main ()
