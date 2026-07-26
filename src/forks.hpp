/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2025  Autonomous Worlds Ltd

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

#ifndef PXD_FORKS_HPP
#define PXD_FORKS_HPP

#include <xayagame/gamelogic.hpp>

namespace pxd
{

/**
 * The Polygon block at which the game world is created: both the initial game
 * state (PXLogic::GetInitialStateBlock) and the GameStart fork below.  Those two
 * are the same block BY DEFINITION -- genesis is when gameplay begins -- so they
 * share one constant rather than two literals that can drift apart.  Drift is a
 * nasty failure: a GameStart above genesis leaves the daemon happily syncing
 * while it SILENTLY discards every gameplay move (moveprocessor returns early,
 * accounts appear with no faction and no error is logged).
 *
 * Moving these forward RESETS the game world -- state is the replay of all moves
 * since this block, so nothing before it exists any more.  The hash must be the
 * real hash of that height on Polygon.
 */
constexpr unsigned POLYGON_GENESIS_HEIGHT = 90'800'000;
constexpr const char* POLYGON_GENESIS_HASH
    = "9d904bb4a23243bc911922960b752c567ff04c9d3ddb7d3c5edf3a9d775214d6";

/**
 * Hardforks that are done on the Taurion game world.
 */
enum class Fork
{

  /**
   * Test fork that does nothing, but is used in unit tests and such
   * for the fork system itself.
   */
  Dummy,

  /**
   * Fork at which we enable the actual gameplay.  Before it, only Cubit
   * operations are enabled.
   *
   * Historically this fork sat ABOVE genesis, so that burnsale Cubits bought
   * during the third competition kept working while gameplay was still shut.
   * It no longer does: GameStart IS genesis (see POLYGON_GENESIS_HEIGHT), so
   * there is no pre-gameplay era left on this chain and nothing reaches this
   * gate but the very first block.  The pre-genesis burnsale balances and the
   * global sold-supply are NOT carried over -- InitialiseState seeds no
   * accounts and MoneySupply::InitialiseDatabase starts every key at 0 -- and
   * that reset is deliberate and approved (Andy, 2026-07-26: pre-anchor burnt
   * coins are gone, the game is unreleased and all of this is testing).  Any
   * future genesis move has the same consequence; re-read the comment on
   * POLYGON_GENESIS_HEIGHT before making one.
   */
  GameStart,

};

/**
 * Helper class that exposes the state of forks on the network with
 * respect to the current block height and/or block time.
 */
class ForkHandler
{

private:

  /** The chain we are running on.  */
  const xaya::Chain chain;

  /** The block height this is for.  */
  const unsigned height;

  /**
   * Translates a chain like MAIN to POLYGON.
   */
  static xaya::Chain TranslateChain (xaya::Chain c);

public:

  explicit ForkHandler (const xaya::Chain c, const unsigned h)
    : chain(TranslateChain (c)), height(h)
  {}

  ForkHandler () = delete;
  ForkHandler (const ForkHandler&) = delete;
  void operator= (const ForkHandler&) = delete;

  /**
   * Returns true if the given fork should be considered active.
   */
  bool IsActive (Fork f) const;

};

} // namespace pxd

#endif // PXD_FORKS_HPP
