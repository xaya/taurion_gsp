/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#ifndef PXD_CONTEXT_HPP
#define PXD_CONTEXT_HPP

#include "params.hpp"

#include "mapdata/basemap.hpp"

#include <xayagame/gamelogic.hpp>

#include <memory>

namespace pxd
{

/**
 * Basic, read-only contextual data about the current block and the chain state
 * in general.  The data is immutable, except if using the ContextForTesting
 * subclass in unit tests.
 */
class Context
{

private:

  /** Reference to the used BaseMap instance.  */
  const BaseMap& map;

  /** The chain we are on.  */
  xaya::Chain chain;

  /**
   * Basic parameters dependant on the chain.  This is a pointer so that we
   * can recreate it with modified chain in tests.
   */
  std::unique_ptr<pxd::Params> params;

  /**
   * The current block's height.  This is set to the confirmed height plus
   * one for processing pending moves, as that corresponds to the expected
   * height at which the move will be confirmed.
   */
  unsigned height;

  /**
   * The timestamp of the current block.  Unset for pending moves and it must
   * not be accessed for them.
   */
  int64_t timestamp;

  friend class ContextForTesting;

public:

  /** Value for timestamp if this is a pending block.  */
  static constexpr int64_t NO_TIMESTAMP = -1;

  /** Value for height if there is no height set (and shouldn't be used).  */
  static constexpr unsigned NO_HEIGHT = static_cast<unsigned> (-1);

  /**
   * Constructs an instance based on the given data.
   */
  explicit Context (xaya::Chain c, const BaseMap& m, unsigned h, int64_t ts);

  xaya::Chain
  Chain () const
  {
    return chain;
  }

  const BaseMap&
  Map () const
  {
    return map;
  }

  const pxd::Params&
  Params () const
  {
    return *params;
  }

  /**
   * Returns the context's block height.  Must not be used if NO_HEIGHT was
   * passed to the constructor.
   */
  unsigned Height () const;

  /**
   * Returns the context's block timestamp.  This must not be called for
   * processing pending moves (where we do not have a timestamp).
   */
  int64_t Timestamp () const;

};

} // namespace pxd

#endif // PXD_CONTEXT_HPP
