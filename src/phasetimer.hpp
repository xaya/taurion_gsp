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

#ifndef PXD_PHASETIMER_HPP
#define PXD_PHASETIMER_HPP

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace pxd
{

/**
 * Wall-clock times of the phases of processing one block.  This is purely
 * instrumentation (it never influences the state):  each call to Next
 * starts a phase, which lasts until the next one starts or the instance
 * is destroyed.  Then the times of all phases are logged, if enabled.
 */
class PhaseTimes
{

private:

  using Clock = std::chrono::steady_clock;

  /** Whether or not the times are logged in the end.  */
  const bool enabled;

  /** The superblock height that is processed.  */
  const unsigned height;

  /** The phases in order, with their names and when they started.  */
  std::vector<std::pair<std::string, Clock::time_point>> phases;

public:

  /**
   * Constructs an instance for processing the given superblock height,
   * which logs the times in the end if e is true.
   */
  explicit PhaseTimes (const bool e, const unsigned h)
    : enabled(e), height(h)
  {}

  ~PhaseTimes ();

  PhaseTimes () = delete;
  PhaseTimes (const PhaseTimes&) = delete;
  void operator= (const PhaseTimes&) = delete;

  /**
   * Starts the phase with the given name.
   */
  void Next (const std::string& name);

};

} // namespace pxd

#endif // PXD_PHASETIMER_HPP
