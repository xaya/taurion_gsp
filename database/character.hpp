#ifndef DATABASE_CHARACTER_HPP
#define DATABASE_CHARACTER_HPP

#include "database.hpp"
#include "faction.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"
#include "proto/character.pb.h"
#include "proto/combat.pb.h"

#include <memory>
#include <string>

namespace pxd
{

/**
 * Wrapper class for the state of one character.  This connects the actual game
 * logic (reading the state and doing modifications to it) from the database.
 * All interpretation of database results and upates to the database are done
 * through this class.
 *
 * This class should not be instantiated directly by users.  Instead, the
 * methods from CharacterTable should be used.  Furthermore, variables should
 * be of type CharacterTable::Handle (or using auto) to get move semantics.
 */
class Character
{

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The underlying integer ID in the database.  */
  Database::IdT id;

  /** The owner string.  */
  std::string owner;

  /** The character's faction.  This is immutable.  */
  Faction faction;

  /** The current position.  */
  HexCoord pos;

  /**
   * The current accumulated movement towards the next step.  If there is none
   * yet or there is no movement, it will be zero.
   */
  PathFinder::DistanceT partialStep;

  /** Current HP proto.  */
  proto::HP hp;

  /** The number of blocks (or zero) the character is still busy.  */
  int busy;

  /** All other data in the protocol buffer.  */
  proto::Character data;

  /**
   * Set to true if any modification to the non-proto columns was made that
   * needs to be synced back to the database in the destructor.
   */
  bool dirtyFields;

  /**
   * Set to true if a modification to the proto-data was made that needs to
   * be written back to the database.
   */
  bool dirtyProto;

  /**
   * Constructs a new character with an auto-generated ID meant to be inserted
   * into the database.
   */
  explicit Character (Database& d, const std::string& o, Faction f);

  /**
   * Constructs a character instance based on the given query result.  This
   * represents the data from the result row but can then be modified.  The
   * result should come from a query made through CharacterTable.
   */
  explicit Character (Database& d, const Database::Result& res);

  /**
   * Binds parameters in a statement to the mutable non-proto fields.  This is
   * to share code between the proto and non-proto updates.  The ID is always
   * bound to parameter ?1.
   *
   * The immutable non-proto field faction is also not bound
   * here, since it is only present in the INSERT OR REPLACE statement
   * (with proto update) and not the UPDATE one.
   */
  void BindFieldValues (Database::Statement& stmt) const;

  /**
   * Validates the character state for consistency.  CHECK-fails if there
   * is any mismatch in the fields.
   */
  void Validate () const;

  friend class CharacterTable;

public:

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~Character ();

  Character () = delete;
  Character (const Character&) = delete;
  void operator= (const Character&) = delete;

  /* Accessor methods.  */

  Database::IdT
  GetId () const
  {
    return id;
  }

  const std::string&
  GetOwner () const
  {
    return owner;
  }

  void
  SetOwner (const std::string& o)
  {
    dirtyFields = true;
    owner = o;
  }

  Faction
  GetFaction () const
  {
    return faction;
  }

  const HexCoord&
  GetPosition () const
  {
    return pos;
  }

  void
  SetPosition (const HexCoord& c)
  {
    dirtyFields = true;
    pos = c;
  }

  PathFinder::DistanceT
  GetPartialStep () const
  {
    return partialStep;
  }

  void
  SetPartialStep (const PathFinder::DistanceT val)
  {
    dirtyFields = true;
    partialStep = val;
  }

  const proto::HP&
  GetHP () const
  {
    return hp;
  }

  proto::HP&
  MutableHP ()
  {
    dirtyFields = true;
    return hp;
  }

  unsigned
  GetBusy () const
  {
    return busy;
  }

  void
  SetBusy (const unsigned b)
  {
    busy = b;
    dirtyFields = true;
  }

  const proto::Character&
  GetProto () const
  {
    return data;
  }

  proto::Character&
  MutableProto ()
  {
    dirtyProto = true;
    return data;
  }

};

/**
 * Utility class that handles querying the characters table in the database and
 * should be used to obtain Character instances (or rather, the underlying
 * Database::Result's for them).
 */
class CharacterTable
{

private:

  /** The Database reference for creating queries.  */
  Database& db;

public:

  /** Movable handle to a character instance.  */
  using Handle = std::unique_ptr<Character>;

  explicit CharacterTable (Database& d)
    : db(d)
  {}

  CharacterTable () = delete;
  CharacterTable (const CharacterTable&) = delete;
  void operator= (const CharacterTable&) = delete;

  /**
   * Returns a Character handle for a fresh instance corresponding to a new
   * character that will be created.
   */
  Handle CreateNew (const std::string& owner, Faction faction);

  /**
   * Returns a handle for the instance based on a Database::Result.
   */
  Handle GetFromResult (const Database::Result& res);

  /**
   * Returns the character with the given ID or a null handle if there is
   * none with that ID.
   */
  Handle GetById (Database::IdT id);

  /**
   * Queries for all characters in the database table.  The characters are
   * ordered by ID to make the result deterministic.
   */
  Database::Result QueryAll ();

  /**
   * Queries for all characters with a given owner, ordered by ID.
   */
  Database::Result QueryForOwner (const std::string& owner);

  /**
   * Queries for all characters that are currently moving (and thus may need
   * to be updated for move stepping).
   */
  Database::Result QueryMoving ();

  /**
   * Queries for all characters that have a combat target and thus need
   * to be processed for damage.
   */
  Database::Result QueryWithTarget ();

  /**
   * Queries all characters that have busy=1, i.e. need their operation
   * processed and finished next.
   */
  Database::Result QueryBusyDone ();

  /**
   * Deletes the character with the given ID.
   */
  void DeleteById (Database::IdT id);

  /**
   * Decrements the busy counter for all characters (and keeps it at zero
   * where it already is zero).  This function must only be called if there
   * are no characters with a count exactly one (it CHECK-fails instead).
   * The reason is that those characters should be processed manually, including
   * an update to their busy field in the proto.  Otherwise their state would
   * become inconsistent.
   */
  void DecrementBusy ();

};

} // namespace pxd

#endif // DATABASE_CHARACTER_HPP
