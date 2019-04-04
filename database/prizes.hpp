#ifndef DATABASE_PRIZES_HPP
#define DATABASE_PRIZES_HPP

#include "database.hpp"

#include <string>

namespace pxd
{

/**
 * Wrapper class around the table of prospecting prizes in the database.
 */
class Prizes
{

private:

  /** The underlying database handle.  */
  Database& db;

public:

  explicit Prizes (Database& d)
    : db(d)
  {}

  Prizes () = delete;
  Prizes (const Prizes&) = delete;
  void operator= (const Prizes&) = delete;

  /**
   * Query how many of a given prize have been found already.
   */
  unsigned GetFound (const std::string& name);

  /**
   * Increment the found counter of the given prize.
   */
  void IncrementFound (const std::string& name);

};

} // namespace pxd

#endif // DATABASE_PRIZES_HPP
