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
Tests minting of vCHI through the burnsale.
"""

from pxtest import PXTest


class BurnsaleTest (PXTest):

  def run (self):
    self.collectPremine ()

    self.sendMove ("domob", {
      "vc": {"m": {}, "b": 10},
    }, burn=1.23456789)
    self.generate (1)

    a = self.getAccounts ()["domob"]
    self.assertEqual (a.getBalance (), 12345 - 10)
    self.assertEqual (a.data["minted"], 12345)

    self.sendMove ("domob", {"vc": {"m": {}}}, burn=2000000-1.2345)
    self.generate (1)

    a = self.getAccounts ()["domob"]
    self.assertEqual (a.getBalance (), 15000000000 - 10)
    self.assertEqual (a.data["minted"], 15000000000)

    self.assertEqual (self.getRpc ("getmoneysupply"), {
      "total": 15000000000,
      "entries":
        {
          "gifted": 0,
          "burnsale": 15000000000,
        },
      "burnsale":
        [
          {
            "stage": 1,
            "price": 0.0001,
            "total": 10000000000,
            "sold": 10000000000,
            "available": 0,
          },
          {
            "stage": 2,
            "price": 0.0002,
            "total": 10000000000,
            "sold": 5000000000,
            "available": 5000000000,
          },
          {
            "stage": 3,
            "price": 0.0005,
            "total": 10000000000,
            "sold": 0,
            "available": 10000000000,
          },
          {
            "stage": 4,
            "price": 0.0010,
            "total": 20000000000,
            "sold": 0,
            "available": 20000000000,
          },
        ],
    })


if __name__ == "__main__":
  BurnsaleTest ().main ()
