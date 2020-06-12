/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#ifndef PXD_MODIFIER_HPP
#define PXD_MODIFIER_HPP

#include "proto/modifier.pb.h"

namespace pxd
{

/**
 * Simple wrapper around a stat modifier.  It allows adding up different
 * modifiers to stack them, and computing their effect on a given number.
 */
class StatModifier
{

private:

  /** Increase (or decrease if negative) of the base number as percent.  */
  int64_t percent = 0;

public:

  StatModifier () = default;
  StatModifier (const StatModifier&) = default;
  StatModifier& operator= (const StatModifier&) = default;

  /**
   * Converts from the roconfig proto form to the instance.
   */
  StatModifier (const proto::StatModifier& pb)
    : percent(pb.percent ())
  {}

  /**
   * Adds another modifier "on top of" the current one.
   */
  StatModifier&
  operator+= (const StatModifier& m)
  {
    percent += m.percent;
    return *this;
  }

  /**
   * Applies this modifier to a given base value.
   */
  int64_t
  operator() (int64_t base) const
  {
    /* The formula is designed so that it "sticks" to the current value
       (both when increased and reduced).  In other words, only when the change
       in each direction is large enough (at least one point) will it be
       applied.  So doing -10% on a value of 5 does not reduce to 4 (as a
       naive multiplication and flooring would).  */
    return base + (base * percent) / 100;
  }

  /**
   * Converts the state back to a proto.
   */
  proto::StatModifier ToProto () const;

};

/** Adds together two modifiers directly as protos.  */
proto::StatModifier& operator+= (proto::StatModifier& pb,
                                 const proto::StatModifier& other);

} // namespace pxd

#endif // PXD_MODIFIER_HPP
