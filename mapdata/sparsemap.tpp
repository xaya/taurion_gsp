/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

/* Template implementation code for sparsemap.hpp.  */

namespace pxd
{

template <typename T>
  SparseTileMap<T>::SparseTileMap (const T& val)
  : defaultValue(val), density(false)
{}

template <typename T>
  const T&
  SparseTileMap<T>::Get (const HexCoord& c) const
{
  if (!density.Get (c))
    return defaultValue;

  return values.at (c);
}

template <typename T>
  void
  SparseTileMap<T>::Set (const HexCoord& c, const T& val)
{
  if (val == defaultValue)
    {
      values.erase (c);
      density.Access (c) = false;
      return;
    }

  density.Access (c) = true;
  values[c] = val;
}

} // namespace pxd
