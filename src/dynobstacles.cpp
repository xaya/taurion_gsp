#include "dynobstacles.hpp"

#include "database/character.hpp"

namespace pxd
{

DynObstacles::DynObstacles (Database& db)
  : red(false), green(false), blue(false)
{
  CharacterTable tbl(db);
  auto res = tbl.QueryAll ();
  while (res.Step ())
    {
      auto c = tbl.GetFromResult (res);
      AddVehicle (c->GetPosition (), c->GetFaction ());
    }
}

DynTiles<bool>&
DynObstacles::FactionVehicles (const Faction f)
{
  switch (f)
    {
    case Faction::RED:
      return red;
    case Faction::GREEN:
      return green;
    case Faction::BLUE:
      return blue;
    default:
      LOG (FATAL) << "Unknown faction: " << static_cast<int> (f);
    }
}

bool
DynObstacles::IsPassable (const HexCoord& c, const Faction f) const
{
  return !FactionVehicles (f).Get (c);
}

void
DynObstacles::AddVehicle (const HexCoord& c, const Faction f)
{
  auto ref = FactionVehicles (f).Access (c);
  CHECK (!ref);
  ref = true;
}

void
DynObstacles::RemoveVehicle (const HexCoord& c, const Faction f)
{
  auto ref = FactionVehicles (f).Access (c);
  CHECK (ref);
  ref = false;
}

} // namespace pxd
