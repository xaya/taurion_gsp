#ifndef DATABASE_FIGHTER_HPP
#define DATABASE_FIGHTER_HPP

#include "character.hpp"

#include "hexagonal/coord.hpp"

#include <functional>

namespace pxd
{

/**
 * Database abstraction for a fighter (entity that can attack another or take
 * damage from another one's attack).  This can be either a character or a
 * building.  This class makes both look the same for the processing code.
 */
class Fighter
{

private:

  /** The character handle if this is a character.  */
  CharacterTable::Handle character;

  /**
   * Construct a fighter based on a character handle.
   */
  explicit Fighter (CharacterTable::Handle&& c)
    : character(std::move (c))
  {}

  friend class FighterTable;

public:

  Fighter () = default;
  Fighter (Fighter&&) = default;
  Fighter& operator= (Fighter&&) = default;

  Fighter (const Fighter&) = delete;
  void operator= (const Fighter&) = delete;

  /**
   * Returns this fighter's faction association.  This is used to determine
   * friendlyness towards potential targets.
   */
  Faction GetFaction () const;

  /**
   * Returns the figher's position.  Must not be called if this is an
   * empty handle.
   */
  const HexCoord& GetPosition () const;

  /**
   * Returns the maximum attack range in which we have to look for targets.
   */
  HexCoord::IntT GetRange () const;

  /**
   * Sets the target of this fighter to the given proto.
   */
  void SetTarget (const proto::TargetId& target);

  /**
   * Clears target selection (i.e. no target is selected).
   */
  void ClearTarget ();

};

/**
 * Database interface for retrieving handles to all fighters.
 */
class FighterTable
{

private:

  /** The CharacterTable from which we retrieve character-based fighters.  */
  CharacterTable& characters;

public:

  /** Type for callbacks when querying for all fighters.  */
  using Callback = std::function<void (Fighter f)>;

  /**
   * Constructs a fighter table drawing characters from the given table.
   */
  explicit FighterTable (CharacterTable& c)
    : characters(c)
  {}

  FighterTable () = delete;
  FighterTable (const FighterTable&) = delete;
  void operator= (const FighterTable&) = delete;

  /**
   * Retrieves all fighters from the database and runs the callback
   * on each one.
   */
  void ProcessAll (const Callback& cb);

};

} // namespace pxd

#endif // DATABASE_FIGHTER_HPP
