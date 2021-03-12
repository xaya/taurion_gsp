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

#ifndef DATABASE_UNIQUEHANDLES_HPP
#define DATABASE_UNIQUEHANDLES_HPP

#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace pxd
{

/**
 * A helper class to ensure that database handles for a particular instance
 * (like the character with a given ID, or the account with a given name)
 * exist at most once at any given moment, so as to make sure there are no
 * bugs with conflicting changes or reads.
 *
 * It keeps track of pairs of "types" and "IDs", and allows users to either
 * add a pair (when a handle is created) or remove it when it is destroyed.
 * The IDs are tracked as strings, but the add/remove functions are templates
 * so they can be called with integers as well (which will be converted
 * to strings using an ostringstream).
 */
class UniqueHandles
{

private:

  /** Mutex for protecting the active set.  */
  std::mutex mut;

  /** All currently active handles, as pairs of type and ID.  */
  std::set<std::pair<std::string, std::string>> active;

public:

  class Tracker;

  UniqueHandles () = default;

  UniqueHandles (const UniqueHandles&) = delete;
  void operator= (const UniqueHandles&) = delete;

  /**
   * The destructor verifies that no handles remain active.
   */
  ~UniqueHandles ();

  /**
   * Registers a new handle that has been activated with the given type
   * and ID.  CHECK-fails if this is already active.
   */
  void Add (const std::string& type, const std::string& id);

  /**
   * Unregisters a handle that has been deactivated.  CHECK-fails if the
   * handle is not active.
   */
  void Remove (const std::string& type, const std::string& id);

};

/**
 * RAII helper class to add and remove a handle.
 */
class UniqueHandles::Tracker
{

private:

  /** The UniqueHandles instance on which this operates.  */
  UniqueHandles& handles;

  /** The type of this handle.  */
  std::string type;

  /** Type ID converted to a string.  */
  std::string id;

public:

  /**
   * Constructs the tracker, which adds it to the UniqueHandles instance.
   */
  template <typename T>
    explicit Tracker (UniqueHandles& h, const std::string& t, const T& i);

  /**
   * Removes the handle from our UniqueHandles instance.
   */
  ~Tracker ();

  Tracker () = delete;
  Tracker (const Tracker&) = delete;
  void operator= (const Tracker&) = delete;

};

} // namespace pxd

#include "uniquehandles.tpp"

#endif // DATABASE_UNIQUEHANDLES_HPP
