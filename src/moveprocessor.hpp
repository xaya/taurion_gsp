#ifndef PXD_MOVEPROCESSOR_HPP
#define PXD_MOVEPROCESSOR_HPP

#include "amount.hpp"
#include "params.hpp"

#include "database/character.hpp"
#include "database/database.hpp"
#include "database/region.hpp"
#include "mapdata/basemap.hpp"

#include <json/json.h>

namespace pxd
{

/**
 * Class that handles processing of all moves made in a block.
 */
class MoveProcessor
{

private:

  /** Basemap instance that can be used.  */
  const BaseMap& map;

  /**
   * The Database handle we use for making any changes (and looking up the
   * current state while validating moves).
   */
  Database& db;

  /** Parameters for the current situation.  */
  const Params& params;

  /** Access handle for the characters table in the DB.  */
  CharacterTable characters;

  /** Access to the regions table.  */
  RegionsTable regions;

  /**
   * Processes the move corresponding to one transaction.
   */
  void ProcessOne (const Json::Value& moveObj);

  /**
   * Processes commands to create new characters in the given move.
   */
  void HandleCharacterCreation (const std::string& name, const Json::Value& mv,
                                Amount paidToDev);

  /**
   * Processes commands to make changes to existing characters.
   */
  void HandleCharacterUpdate (const std::string& name, const Json::Value& mv);

public:

  explicit MoveProcessor (Database& d, const Params& p, const BaseMap& m)
    : map(m), db(d), params(p), characters(db), regions(db)
  {}

  MoveProcessor () = delete;
  MoveProcessor (const MoveProcessor&) = delete;
  void operator= (const MoveProcessor&) = delete;

  /**
   * Processes all moves from the given JSON array.
   */
  void ProcessAll (const Json::Value& moveArray);

};

} // namespace pxd

#endif // PXD_MOVEPROCESSOR_HPP
