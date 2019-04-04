#ifndef PXD_FAME_HPP
#define PXD_FAME_HPP

#include "database/damagelists.hpp"
#include "database/database.hpp"
#include "proto/combat.pb.h"

namespace pxd
{

/**
 * The main class handling computation and updates of fame.  This is notified
 * about kills by the combat logic, and then updates the fame accordingly.
 *
 * The actual fame update takes place in a virtual subroutine, so that this
 * can be mocked out for testing (or tested separately).
 */
class FameUpdater
{

private:

  /** A DamageLists instance we use for the updates during computation.  */
  DamageLists dl;

protected:

  /**
   * Updates fame accordingly for the given kill.  This is the main internal
   * routine handling fame computation, which holds the actual logic.
   */
  virtual void UpdateForKill (Database::IdT victim,
                              const DamageLists::Attackers& attackers);

public:

  explicit FameUpdater (Database& db, const unsigned height)
    : dl(db, height)
  {}

  virtual ~FameUpdater () = default;

  FameUpdater () = delete;
  FameUpdater (const FameUpdater&) = delete;
  void operator= (const FameUpdater&) = delete;

  /**
   * Returns a DamageLists instance for the current block.
   */
  DamageLists&
  GetDamageLists ()
  {
    return dl;
  }

  /**
   * Updates fame when the given fighter target has been killed.
   */
  void UpdateForKill (const proto::TargetId& target);

};

} // namespace pxd

#endif // PXD_FAME_HPP
