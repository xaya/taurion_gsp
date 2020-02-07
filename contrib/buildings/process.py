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

# This utility processes the building shapes given in JSON format and
# writes out individual text files that have the shape tiles for each
# building type in the required text proto format.

import json

# Define the building types that we will output together with their names
# in the JSON file with shapes.
factions = {
  "r": "Jodon",
  "g": "Ephrati",
  "b": "Reubo",
}
baseTypes = {
  "cc": "Command_Centre",
  "constr": "Construction_Facility",
  "ep": "Energy_Plant",
  "rt": "Rocket_Turret",
  "et": "Energy_Turret",
  "ref": "Refinery",
  "res": "Reserach_Facility",
  "vb": "Vehicle_Bay",
}

outputs = {
  "obelisk1": "Ancient1",
  "obelisk2": "Ancient1_2",
  "obelisk3": "Ancient1_3",
  "ancient1": "Ancient2",
  "ancient2": "Ancient3",
  "ancient3": "Ancient4",
}
#for k, v in factions.items ():
#  for t, nm in baseTypes.items ():
#    outputs["%s_%s" % (k, t)] = "%s_%s" % (v, nm)

# Read the input JSON data.
with open ("shapetiles.json", "r") as f:
  data = json.load (f)

#assert len (data) == len (outputs)

# Process all requested output building types by looking up the data,
# processing it, and writing it to the output protobuf file.
for o, k in outputs.items ():
  tiles = None
  fullKey = "shape_%s_tiles" % k
  for d in data:
    if fullKey in d:
      tiles = d[fullKey]
      break
  assert tiles is not None, fullKey

  with open ("%s.pb.text" % o, "w") as f:
    f.write ("building_types:\n")
    f.write ("  {\n")
    f.write ('    key: "%s"\n' % o)
    f.write ("    value:\n")
    f.write ("      {\n")
    for t in tiles:
      f.write ("        shape_tiles: { x: %d y: %d }\n" % (t["x"], t["y"]))
    f.write ("      }\n")
    f.write ("  }\n")
