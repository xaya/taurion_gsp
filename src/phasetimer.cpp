/*
    GSP for the Taurion blockchain game
    Copyright (C) 2026  Autonomous Worlds Ltd

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

#include "phasetimer.hpp"

#include <iomanip>
#include <sstream>

namespace pxd
{

PhaseTimes* PhaseTimes::active = nullptr;

PhaseTimes::PhaseTimes (const bool enabled)
{
  if (enabled)
    active = this;
}

PhaseTimes::~PhaseTimes ()
{
  if (active == this)
    active = nullptr;
}

std::string
PhaseTimes::Summary () const
{
  std::ostringstream out;
  out << std::fixed << std::setprecision (1);
  for (const auto& p : phases)
    out << ' ' << p.first << ' ' << p.second;
  return out.str ();
}

ScopedPhase::ScopedPhase (const char* n)
  : times(PhaseTimes::active), name(n)
{
  if (times != nullptr)
    start = std::chrono::steady_clock::now ();
}

ScopedPhase::~ScopedPhase ()
{
  if (times == nullptr)
    return;

  const std::chrono::duration<double, std::milli> d
      = std::chrono::steady_clock::now () - start;
  times->phases.emplace_back (name, d.count ());
}

} // namespace pxd
