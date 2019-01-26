#ifndef DATABASE_TARGET_HPP
#define DATABASE_TARGET_HPP

#include "database.hpp"
#include "faction.hpp"

#include "hexagonal/coord.hpp"
#include "proto/combat.pb.h"

#include <functional>

namespace pxd
{

/**
 * Abstraction to give access to "targets" in the database.  They are either
 * characters or buildings, from their respective tables.  This class allows
 * querying both, and handles finding potential in-range and enemy entities.
 */
class TargetFinder
{

private:

  /** The Database reference for doing queries.  */
  Database& db;

public:

  /** Type for a callback function that processes targets.  */
  using ProcessingFcn
      = std::function<void (const HexCoord&, const proto::TargetId&)>;

  explicit TargetFinder (Database& d)
    : db(d)
  {}

  TargetFinder () = delete;
  TargetFinder (const TargetFinder&) = delete;
  void operator= (const TargetFinder&) = delete;

  /**
   * Finds all enemy targets in the given L1 range and executes the
   * callback on each of the resulting Target instances.
   */
  void ProcessL1Targets (const HexCoord& centre, HexCoord::IntT l1range,
                         Faction action, const ProcessingFcn& cb);

};

} // namespace pxd

#endif // DATABASE_TARGET_HPP
