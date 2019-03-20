#include "spawn.hpp"

namespace pxd
{

void
SpawnCharacter (const std::string& owner, const Faction f,
                CharacterTable& tbl, const Params& params)
{
  VLOG (1)
      << "Spawning new character for " << owner
      << " in faction " << FactionToString (f) << "...";

  auto c = tbl.CreateNew (owner, f);

  HexCoord::IntT spawnRadius;
  c->SetPosition (params.SpawnArea (f, spawnRadius));

  auto& pb = c->MutableProto ();
  params.InitCharacterStats (pb);
  c->MutableHP () = pb.combat_data ().max_hp ();
}

} // namespace pxd
