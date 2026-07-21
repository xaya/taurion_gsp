/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DATABASE_PARAMS_HPP
#define DATABASE_PARAMS_HPP

#include "database.hpp"

#include <string>

namespace pxd
{

/**
 * Access to the runtime-tunable named parameters (the parameters table),
 * set through the "param" admin command exactly as in the soccerverse GSP.
 * Unlike there, reads fall back to a caller-supplied default (taurion's
 * defaults live in roconfig), so the table only ever holds explicit
 * overrides and removing one cleanly resets to the default.  Consensus
 * state: values change only through admin commands in block data and
 * unwind with the normal changesets.
 *
 * Currently read by the jobs-board admission caps.  The mechanism also
 * carries soccerverse-style "fork-*" activation flags: post-launch
 * consensus changes can ship dormant behind Get ("fork-x", 0) > 0 and be
 * enabled chain-wide by one admin command, with no wipe or coordinated
 * restart.
 */
class ParamsTable
{

private:

  /** The underlying database handle.  */
  Database& db;

public:

  explicit ParamsTable (Database& d)
    : db(d)
  {}

  ParamsTable () = delete;
  ParamsTable (const ParamsTable&) = delete;
  void operator= (const ParamsTable&) = delete;

  /**
   * Returns the effective value for the given name: the stored override if
   * one exists, else the fallback (the roconfig default).
   */
  int64_t Get (const std::string& name, int64_t fallback) const;

  /** Sets (inserts or replaces) the override for the given name.  */
  void Set (const std::string& name, int64_t value);

  /** Removes the override (resetting the name to its default).  */
  void Remove (const std::string& name);

};

} // namespace pxd

#endif // DATABASE_PARAMS_HPP
