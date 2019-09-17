/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#ifndef DATABASE_LAZYPROTO_HPP
#define DATABASE_LAZYPROTO_HPP

#include <string>

namespace pxd
{

/**
 * A class that wraps a protocol buffer and implements "lazy deserialisation".
 * Initially, it just keeps the raw data in a string, and only deserialises the
 * protocol buffer when actually needed.  This can help speed up database
 * accesses for cases where we don't actually need some proto data for certain
 * operations.
 *
 * The class also keeps track of when the protocol buffer was modified, so
 * we know if we need to update it in the database.
 */
template <typename Proto>
  class LazyProto
{

private:

  /**
   * Possible "states" of a lazy proto.
   */
  enum class State : uint8_t
  {

    /** There is no data yet (this instance is uninitialised).  */
    UNINITIALISED,

    /** We have not yet accessed/parsed the byte data.  */
    UNPARSED,

    /**
     * We have parsed the byte data but not modified the proto object.
     * In other words, the serialised data is still in sync with the
     * proto message.
     */
    UNMODIFIED,

    /** The proto message has been modified.  */
    MODIFIED,

  };

  /** The raw bytes of the protocol buffer.  */
  mutable std::string data;

  /** The parsed protocol buffer.  */
  mutable Proto msg;

  /** Current state of this lazy proto.  */
  mutable State state = State::UNINITIALISED;

  /**
   * Ensures that the protocol buffer is parsed.
   */
  void EnsureParsed () const;

  friend class LazyProtoTests;

public:

  LazyProto () = default;

  /**
   * Constructs a lazy proto instance based on the given byte data.
   */
  explicit LazyProto (std::string&& d);

  /* A LazyProto can be moved but not copied.  */
  LazyProto (LazyProto&&) = default;
  LazyProto& operator= (LazyProto&&) = default;

  LazyProto (const LazyProto&) = delete;
  void operator= (const LazyProto&) = delete;

  /**
   * Initialises the protocol buffer value as "empty" (i.e. default-constructed
   * protocol buffer message, empty data string).
   */
  void SetToDefault ();

  /**
   * Accesses the message read-only.
   */
  const Proto& Get () const;

  /**
   * Accesses and modifies the proto message.
   */
  Proto& Mutable ();

  /**
   * Returns true if the protocol buffer was modified from the original
   * data (e.g. so we know that it needs updating in the database).
   */
  bool IsDirty () const;

  /**
   * Returns a serialised form of the potentially modified protocol buffer.
   */
  const std::string& GetSerialised () const;

};

} // namespace pxd

#include "lazyproto.tpp"

#endif // DATABASE_LAZYPROTO_HPP
