#!/bin/sh -e

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

# This is a simple script that tries to extract the current git commit
# using "git describe" and write it to a generated source file (so it
# can be accessed and logged from the GSP).
#
# If this cannot be done, e.g. because we have a non-git source tree from
# a released distribution archive, then the file is left alone (and should
# already be there with the release version in it).
#
# This command expects one or more arguments.  If the output matches any
# of the listed files already, nothing is done.  Else the new version data
# is written to the first argument.

version=$(git describe --dirty --always --long 2>/dev/null || true)
if [ -z "${version}" ]
then
  exit
fi

# First write to a temporary file.
out=$(mktemp)
printf "const char* GIT_VERSION = \"${version}\";\\n" >${out}

# If the output matches any of the arguments already, nothing needs to be done.
for f in $@
do
  if cmp -s ${out} $f
  then
    rm -f ${out}
    exit
  fi
done

# Write the output file.
mv -f ${out} $1
