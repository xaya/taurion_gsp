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

# This utility processes CSV data about the resource areas (which is the
# actual input format in which the data was created) and writes them out
# in the text-proto format we use for resourcedist.pb.text.

import csv
import sys

def convertType (csvName):
  """
  Converts the string name of a resource as it appears in CSV to the
  name we use in the GSP.  Returns None if the name is an empty column.
  """

  if csvName == "":
    return None

  letter = csvName.strip ()
  letter = letter[0].lower ()
  return "raw %s" % letter

reader = csv.reader (sys.stdin, delimiter=',')
for row in reader:
  type1 = convertType (row[0])
  type2 = convertType (row[1])
  x = int (row[2])
  y = int (row[3])
  sys.stdout.write ("areas:\n  {\n")
  sys.stdout.write ("    centre: { x: %d y: %d }\n" % (x, y))
  for t in [type1, type2]:
    if t is not None:
      sys.stdout.write ("    resources: \"%s\"\n" % t)
  sys.stdout.write ("  }\n")
