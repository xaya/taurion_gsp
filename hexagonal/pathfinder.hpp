#ifndef HEXAGONAL_PATHFINDER_HPP
#define HEXAGONAL_PATHFINDER_HPP

#include "coord.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>

namespace pxd
{

/**
 * A class that solves the problem of finding the shortest path between
 * two tiles on a hex grid.  Dijkstra's algorithm is used with a user-supplied
 * edge-weight function for steps between tiles and their neighbours.
 *
 * This class initially computes the distance field for a given
 * target and source, and then can be used to actually step along
 * the resulting shortest path.
 */
class PathFinder
{

public:

  using DistanceT = uint32_t;

  /**
   * Call-back function that should return the edge weight (distance)
   * between two neighbouring hex tiles.
   *
   * It will only ever be called for neighbouring tiles.
   *
   * If there is no connection at all, it should return NO_CONNECTION instead
   * of some distance value.
   */
  using EdgeWeightFcn
      = std::function<DistanceT (const HexCoord& from, const HexCoord& to)>;

  /**
   * DistanceT to be returned from the edge weight function if there is
   * no connection between two tiles at all.
   */
  static constexpr DistanceT NO_CONNECTION
      = std::numeric_limits<DistanceT>::max ();

private:

  /** The underlying "edge weight" function that is used.  */
  const EdgeWeightFcn edgeWeight;

  /** The target coordinate, which is always fixed.  */
  const HexCoord target;

  /**
   * The field of distances to the target, for coordinates for which this is
   * known definitely.  Once Compute has been called, at least the source
   * coordinate and all tiles along the shortest path between source and target
   * will be in that map.
   */
  std::unordered_map<HexCoord, DistanceT> distances;

  friend class PathFinderTests;

public:

  class Stepper;

  inline explicit PathFinder (const EdgeWeightFcn& ew, const HexCoord& t)
    : edgeWeight(ew), target(t)
  {}

  PathFinder () = delete;
  PathFinder (const PathFinder&) = delete;
  void operator= (const PathFinder&) = delete;

  /**
   * Computes the distance field from the fixed target to the given
   * source coordinate and returns the distance value (or NO_CONNECTION
   * if there is no available path).
   *
   * After this call has succeeded, StepPath can be called to step along
   * the actual path.
   *
   * For the whole computation, only tiles with a L1 distance to the target
   * of up to the given limit are considered.  This way, we get a guarantee
   * on the computational complexity and protect against DoS vectors.
   */
  DistanceT Compute (const HexCoord& source, HexCoord::IntT l1Range);

  /**
   * Returns a Stepper instance, which can be used to walk along the shortest
   * path from the given source to the fixed target.  This function must only
   * be called after Compute with source was successful.
   */
  Stepper StepPath (const HexCoord& source) const;

};

/**
 * Utility class that resembles an "iterator" for stepping along the shortest
 * path found between two coordinates.
 */
class PathFinder::Stepper final
{

private:

  /** The PathFinder instance that is used to look up the path.  */
  const PathFinder& finder;

  /** The current position along the path.  */
  HexCoord position;

  inline explicit Stepper (const PathFinder& f, const HexCoord& source)
    : finder(f), position(source)
  {}

  friend class PathFinder;

public:

  Stepper (Stepper&&) = default;

  Stepper () = delete;
  Stepper (const Stepper&) = delete;
  void operator= (const Stepper&) = delete;

  /**
   * Returns true if there are more steps (we are not yet at the target).
   */
  inline bool
  HasMore () const
  {
    return position != finder.target;
  }

  /**
   * Returns the current position along the path.
   */
  inline const HexCoord&
  GetPosition () const
  {
    return position;
  }

  /**
   * Steps onto the next tile along the path and returns the distance this
   * one step accounts for.
   *
   * Must only be called if HasMore() is true.
   */
  DistanceT Next ();

};

} // namespace pxd

#endif // HEXAGONAL_PATHFINDER_HPP
