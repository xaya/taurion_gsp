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

/* Template implementation code for combat.hpp.  */

#include <type_traits>

namespace pxd
{

template <typename T>
  CombatEntity::CombatEntity (Database& d, const Database::Result<T>& res)
    : db(d), isNew(false)
{
  static_assert (std::is_base_of<ResultWithCombat, T>::value,
                 "CombatEntity needs a ResultWithCombat");
  
  hp = res.template GetProto<typename T::hp> ();
  regenData = res.template GetProto<typename T::regendata> ();

  if (res.template IsNull<typename T::target> ())
    target.SetToDefault ();
  else
    target = res.template GetProto<typename T::target> ();

  oldAttackRange = res.template Get<typename T::attackrange> ();
  oldCanRegen = res.template Get<typename T::canregen> ();
}

} // namespace pxd
