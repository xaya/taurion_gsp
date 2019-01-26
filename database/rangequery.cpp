#include "rangequery.hpp"

#include <glog/logging.h>

namespace pxd
{

unsigned L1RangeQuery::cnt = 0;

L1RangeQuery::L1RangeQuery (Database& d, const HexCoord& centre,
                            const HexCoord::IntT l1range)
  : db(d)
{
  std::ostringstream nm;
  nm << "l1rangequery" << ++cnt;
  tableName = nm.str ();

  VLOG (1)
      << "Creating temporary table for querying the " << l1range
      << " L1 range around " << centre << ": " << tableName;

  auto stmt = db.Prepare ("CREATE TEMPORARY TABLE `" + tableName + R"(`
    (
      `rqx` INTEGER NOT NULL,
      `rqminy` INTEGER NOT NULL,
      `rqmaxy` INTEGER NOT NULL
    )
  )");
  stmt.Execute ();

  for (HexCoord::IntT x = centre.GetX () - l1range;
       x <= centre.GetX () + l1range; ++x)
    {
      stmt = db.Prepare ("INSERT INTO `" + tableName + R"(`
        (`rqx`, `rqminy`, `rqmaxy`) VALUES (?1, ?2, ?3)
      )");
      stmt.Bind<int> (1, x);
      /* This is actually more like an L-infinity range.  But it contains
         the L1 range and is simple enough for now.  */
      stmt.Bind<int> (2, centre.GetY () - l1range);
      stmt.Bind<int> (3, centre.GetY () + l1range);
      stmt.Execute ();
    }
}

L1RangeQuery::~L1RangeQuery ()
{
  VLOG (1) << "Dropping temporary table for range query: " << tableName;
  auto stmt = db.Prepare ("DROP TABLE temp.`" + tableName + "`");
  stmt.Execute ();
}

std::string
L1RangeQuery::GetJoinClause () const
{
  std::ostringstream res;
  res << " INNER JOIN `" << tableName << "`"
      << " ON `x` = `rqx` AND (`y` BETWEEN `rqminy` AND `rqmaxy`)";

  return res.str ();
}

} // namespace pxd
