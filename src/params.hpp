#ifndef PXD_PARAMS_HPP
#define PXD_PARAMS_HPP

#include "amount.hpp"

#include "hexagonal/coord.hpp"
#include "database/faction.hpp"
#include "proto/character.pb.h"

#include <xayagame/gamelogic.hpp>

#include <string>

namespace pxd
{

/**
 * The basic parameters that determine the game rules.  Once constructed, an
 * instance of this class is immutable and can be used to retrieve the
 * parameters for a particular situation (e.g. chain).
 */
class Params
{

private:

  /** The chain for which we need parameters.  */
  const xaya::Chain chain;

public:

  /**
   * Constructs an instance based on the given situation.
   */
  explicit Params (const xaya::Chain c)
    : chain(c)
  {}

  Params () = delete;
  Params (const Params&) = delete;
  void operator= (const Params&) = delete;

  /**
   * Returns the address to which CHI for the developers should be paid.
   */
  std::string DeveloperAddress () const;

  /**
   * Returns the amount of CHI to be paid for creation of a character.
   */
  Amount CharacterCost () const;

  /**
   * Returns the maximum L1 distance between waypoints for movement.
   */
  HexCoord::IntT MaximumWaypointL1Distance () const;

  /**
   * Returns the duration of prospecting in blocks.
   */
  unsigned ProspectingBlocks () const;

  /**
   * Returns the spawn centre and radius for the given faction.
   */
  HexCoord SpawnArea (Faction f, HexCoord::IntT& radius) const;

  /**
   * Initialises the stats for a newly created character.  This fills in the
   * combat data and other fields in the protocol buffer as needed.
   */
  void InitCharacterStats (proto::Character& pb) const;

  /**
   * Returns true if god-mode commands are allowed (on regtest).
   */
  bool GodModeEnabled () const;

};

} // namespace pxd

#endif // PXD_PARAMS_HPP
