#ifndef DATABASE_RANGEQUERY_HPP
#define DATABASE_RANGEQUERY_HPP

#include "database.hpp"

#include "hexagonal/coord.hpp"

#include <string>

namespace pxd
{

/**
 * RAII object that creates a temporary database table and a corresponding
 * JOIN clause for efficient querying of tables with an INDEX on `x`, `y`
 * in some L1 range.
 *
 * In particular, the temporary table created contains rows of the form
 * (rqx, rqminy, rqmaxy), so that "x = rqx AND y BETWEEN rqminy AND rqmaxy"
 * for any row is the condition to be within the specified L1 range.  That
 * allows to use the index on `x` and `y` efficiently, i.e. for full filtering
 * and not only for filtering in `x`.
 *
 * The temporary table is created in the constructor and dropped in the
 * destructor.  So a corresponding query has to be made while the instance
 * is active on the stack.
 */
class L1RangeQuery
{

private:

  /** Global counter for generating unique table names.  */
  static unsigned cnt;

  /** Reference to the underlying database.  */
  Database& db;

  /** Name of the temporary table.  */
  std::string tableName;

public:

  /**
   * Constructs an instance and a matching temporary table in the given
   * database.
   */
  explicit L1RangeQuery (Database& d, const HexCoord& centre,
                         HexCoord::IntT l1range);

  /**
   * Destructs the instance and drops the temporary table.
   */
  ~L1RangeQuery ();

  /**
   * Returns the SQL JOIN clause to use for filtering with the given range
   * as a string.
   */
  std::string GetJoinClause () const;

};

} // namespace pxd

#endif // DATABASE_RANGEQUERY_HPP
