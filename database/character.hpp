#ifndef DATABASE_CHARACTER_HPP
#define DATABASE_CHARACTER_HPP

#include "database.hpp"

#include "hexagonal/coord.hpp"
#include "proto/character.pb.h"

#include <string>

namespace pxd
{

/**
 * Wrapper class for the state of one character.  This connects the actual game
 * logic (reading the state and doing modifications to it) from the database.
 * All interpretation of database results and upates to the database are done
 * through this class.
 */
class Character
{

private:

  /** Database reference this belongs to.  */
  Database& db;

  /** The underlying integer ID in the database.  */
  unsigned id;

  /** The owner string.  */
  std::string owner;

  /** The name of the character as string.  */
  std::string name;

  /** The current position.  */
  HexCoord pos;

  /** All other data in the protocol buffer.  */
  proto::Character data;

  /**
   * Set to true if any modification was made so that we will have to sync
   * the values back to the database in the destructor.
   */
  bool dirty;

public:

  /**
   * Constructs a new character with an auto-generated ID meant to be inserted
   * into the database.
   */
  explicit Character (Database& d, const std::string& o, const std::string& n);

  /**
   * Constructs a character instance based on the given query result.  This
   * represents the data from the result row but can then be modified.  The
   * result should come from a query made through CharacterTable.
   */
  explicit Character (Database::Result& res);

  /**
   * In the destructor, the underlying database is updated if there are any
   * modifications to send.
   */
  ~Character ();

  Character () = delete;
  Character (const Character&) = delete;
  void operator= (const Character&) = delete;

  /* Accessor methods.  */

  unsigned
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
    dirty = true;
    owner = o;
  }

  const std::string&
  GetName () const
  {
    return name;
  }

  const HexCoord&
  GetPosition () const
  {
    return pos;
  }

  void
  SetPosition (const HexCoord& c)
  {
    dirty = true;
    pos = c;
  }

  const proto::Character&
  GetProto () const
  {
    return data;
  }

  proto::Character&
  MutableProto ()
  {
    dirty = true;
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

  explicit CharacterTable (Database& d)
    : db(d)
  {}

  CharacterTable () = delete;
  CharacterTable (const CharacterTable&) = delete;
  void operator= (const CharacterTable&) = delete;

  /**
   * Queries for all characters in the database table.  The characters are
   * ordered by ID to make the result deterministic.
   */
  Database::Result GetAll ();

  /**
   * Queries for all characters with a given owner, ordered by ID.
   */
  Database::Result GetForOwner (const std::string& owner);

  /**
   * Verifies whether the given string is valid as name for a new character.
   * This means that it is non-empty and not yet used in the database.
   */
  bool IsValidName (const std::string& name);

};

} // namespace pxd

#endif // DATABASE_CHARACTER_HPP
