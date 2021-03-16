/*
    GSP for the Taurion blockchain game
    Copyright (C) 2021  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DATABASE_SKILLS_HPP
#define DATABASE_SKILLS_HPP

#include "database.hpp"

#include "proto/account.pb.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace pxd
{

/**
 * A utility class to access and modify the individual skills and corresponding
 * XP levels of an account in the database.  The instance keeps an in-memory
 * cache of the original (database) values as well as the modifications done
 * to it (by means of gained XPs).  On destruction, the database will be updated
 * to reflect the modified state.
 *
 * Instances of this class are created and owned by the Account database handle.
 */
class SkillManager
{

public:

  class XpLevel;

private:

  /** The Database reference for doing queries and updating.  */
  Database& db;

  /** The account name this is for.  */
  const std::string name;

  /**
   * The individual XP levels per skill for this account.  This only
   * contains entries that have already been queried from the database;
   * if an entry is missing, it will be fetched from the database
   * on first use.
   */
  mutable std::unordered_map<proto::SkillType, std::unique_ptr<XpLevel>> levels;

  /**
   * Constructs an instance for the given account.  Initially, the instance
   * will just represent the values from the database, but from then on,
   * XP may be added to particular skills.
   */
  explicit SkillManager (Database& d, const std::string& n)
    : db(d), name(n)
  {}

  friend class Account;
  friend class TestSkillManager;

public:

  /**
   * In the destructor, all changes made (i.e. newly gained XP points)
   * are written back to the database.
   */
  ~SkillManager ();

  SkillManager () = delete;
  SkillManager (const SkillManager&) = delete;
  void operator= (const SkillManager&) = delete;

  /**
   * Fetches and returns the instance of the given skill.
   */
  XpLevel& operator[] (proto::SkillType t);

  /**
   * Constant version of the accessor.
   */
  const XpLevel&
  operator[] (const proto::SkillType t) const
  {
    return const_cast<SkillManager*> (this)->operator[] (t);
  }

};

/**
 * The state of one particular skill.  This holds the XP value from the
 * database, the potentially added new XP (which will be written back
 * to the database) and also provides the functionality needed to work
 * with the number, e.g. convert it to a skill level.
 */
class SkillManager::XpLevel
{

private:

  /** The number of XPs for this level, potentially including additions.  */
  int64_t num;

  /** Set to true if the value has been changed with respect to the DB.  */
  bool dirty = false;

  explicit XpLevel (const int64_t v)
    : num(v)
  {}

  friend class SkillManager;

public:

  XpLevel () = delete;
  XpLevel (const XpLevel&) = delete;
  void operator= (const XpLevel&) = delete;

  /**
   * Returns the total XP count for this level.
   */
  int64_t
  GetXp () const
  {
    return num;
  }

  /**
   * Adds the given number of XP.
   */
  void AddXp (int64_t v);

};

} // namespace pxd

#endif // DATABASE_SKILLS_HPP
