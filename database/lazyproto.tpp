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

/* Template implementation code for lazyproto.hpp.  */

#include <glog/logging.h>

namespace pxd
{

template <typename Proto>
  LazyProto<Proto>::LazyProto (std::string&& d)
    : data(std::move (d)), state(State::UNPARSED)
{}

template <typename Proto>
  inline void
  LazyProto<Proto>::EnsureParsed () const
{
  switch (state)
    {
    case State::UNPARSED:
      CHECK (msg.ParseFromString (data));
      state = State::UNMODIFIED;
      return;

    case State::UNMODIFIED:
    case State::MODIFIED:
      return;

    default:
      LOG (FATAL) << "Unexpected state: " << static_cast<int> (state);
    }
}

template <typename Proto>
  void
  LazyProto<Proto>::SetToDefault ()
{
  data.clear ();
  msg.Clear ();
  state = State::UNMODIFIED;
}

template <typename Proto>
  inline const Proto&
  LazyProto<Proto>::Get () const
{
  EnsureParsed ();
  return msg;
}

template <typename Proto>
  inline Proto&
  LazyProto<Proto>::Mutable ()
{
  EnsureParsed ();
  state = State::MODIFIED;
  return msg;
}

template <typename Proto>
  inline bool
  LazyProto<Proto>::IsDirty () const
{
  CHECK (state != State::UNINITIALISED);
  return state == State::MODIFIED;
}

template <typename Proto>
  const std::string&
  LazyProto<Proto>::GetSerialised () const
{
  switch (state)
    {
    case State::UNPARSED:
    case State::UNMODIFIED:
      return data;

    case State::MODIFIED:
      CHECK (msg.SerializeToString (&data));
      return data;

    default:
      LOG (FATAL) << "Unexpected state: " << static_cast<int> (state);
    }
}

} // namespace pxd
