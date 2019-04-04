#include "prizes.hpp"

#include <glog/logging.h>

namespace pxd
{

unsigned
Prizes::GetFound (const std::string& name)
{
  auto stmt = db.Prepare (R"(
    SELECT `found` FROM `prizes` WHERE `name` = ?1
  )");
  stmt.Bind (1, name);

  auto res = stmt.Query ();
  CHECK (res.Step ()) << "Prize not found in database: " << name;
  const unsigned found = res.Get<int64_t> ("found");
  CHECK (!res.Step ());

  return found;
}

void
Prizes::IncrementFound (const std::string& name)
{
  VLOG (1) << "Incrementing found counter for prize " << name << "...";

  auto stmt = db.Prepare (R"(
    UPDATE `prizes`
      SET `found` = `found` + 1
      WHERE `name` = ?1
  )");
  stmt.Bind (1, name);
  stmt.Execute ();
}

} // namespace pxd
