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

#include <glog/logging.h>

#include <iomanip>
#include <sstream>

namespace pxd
{

PhaseTimes* PhaseTimes::active = nullptr;

PhaseTimes::PhaseTimes (const bool enabled, const unsigned h)
  : height(h)
{
  if (enabled)
    active = this;
}

PhaseTimes::~PhaseTimes ()
{
  if (active != this)
    return;
  active = nullptr;

  const auto ms = [] (const Clock::time_point from, const Clock::time_point to)
    {
      return std::chrono::duration<double, std::milli> (to - from).count ();
    };

  const auto end = Clock::now ();
  std::ostringstream out;
  out << std::fixed << std::setprecision (1);
  for (size_t i = 0; i < phases.size (); ++i)
    out << ' ' << phases[i].first << ' '
        << ms (phases[i].second,
               i + 1 < phases.size () ? phases[i + 1].second : end);
  if (!phases.empty ())
    out << " total " << ms (phases.front ().second, end);

  LOG (INFO) << "Super block " << height << " phases (ms):" << out.str ();
}

void
PhaseTimes::Next (const char* name)
{
  if (active != nullptr)
    active->phases.emplace_back (name, Clock::now ());
}

} // namespace pxd
