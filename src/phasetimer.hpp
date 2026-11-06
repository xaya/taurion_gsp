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
 * instrumentation (it never influences the state):  while an instance is
 * active, every ScopedPhase adds its duration to it.  Block processing is
 * single-threaded, so a plain global pointer is enough.
 */
class PhaseTimes
{

private:

  /** The active instance, if any.  */
  static PhaseTimes* active;

  /** The recorded phases in order, with their durations in milliseconds.  */
  std::vector<std::pair<std::string, double>> phases;

  friend class ScopedPhase;

public:

  /**
   * Constructs an instance and makes it active if enabled is true.
   */
  explicit PhaseTimes (bool enabled);

  ~PhaseTimes ();

  PhaseTimes (const PhaseTimes&) = delete;
  void operator= (const PhaseTimes&) = delete;

  /**
   * Returns the recorded phases as a single line, "name ms" each.
   */
  std::string Summary () const;

};

/**
 * RAII timer that records its lifetime as a phase of the active PhaseTimes.
 * Phases may nest; each records its own total time.
 */
class ScopedPhase
{

private:

  /** The PhaseTimes instance this records to, or null if none is active.  */
  PhaseTimes* times;

  /** The phase's name.  */
  const char* name;

  /** Start of the phase.  */
  std::chrono::steady_clock::time_point start;

public:

  explicit ScopedPhase (const char* n);
  ~ScopedPhase ();

  ScopedPhase (const ScopedPhase&) = delete;
  void operator= (const ScopedPhase&) = delete;

};

} // namespace pxd

#endif // PXD_PHASETIMER_HPP
