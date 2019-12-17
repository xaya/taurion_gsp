#!/usr/bin/env python3

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

# This utility processes the CSV list of prizes for the second Taurion
# competition and produces the C++ code we need to define the prizes
# in params.cpp.

import csv
import sys

# Odds (1/X) we want for the prizes based on the available number.
# These are computed by findOdds.m.
odds = {
  1: 65145,
  2: 38564,
  3: 28184,
  5: 18765,
  10: 10559,
  30: 4033,
  40: 3106,
  50: 2532,
  2000: 73,
}

reader = csv.reader (sys.stdin, delimiter=',')
for row in reader:
  amount = int (row[0])
  name = row[1]
  sys.stdout.write ('    {"%s", %d, %d},\n' % (name, amount, odds[amount]))
