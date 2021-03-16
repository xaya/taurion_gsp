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

#include "skills.hpp"

#include <glog/logging.h>

#include <vector>

namespace pxd
{

SkillManager::~SkillManager ()
{
  std::vector<std::pair<proto::SkillType, std::unique_ptr<XpLevel>>> dirty;
  for (auto& e : levels)
    {
      if (e.second->dirty)
        dirty.emplace_back (e.first, std::move (e.second));
    }

  if (dirty.empty ())
    return;

  VLOG (1)
      << "SkillManager for " << name
      << " has " << dirty.size () << " dirty entries";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `account_xps`
      (`name`, `skill`, `xp`)
      VALUES (?1, ?2, ?3)
  )");

  for (const auto& d : dirty)
    {
      stmt.Bind (1, name);
      stmt.Bind (2, static_cast<int> (d.first));
      stmt.Bind (3, d.second->GetXp ());
      stmt.Execute ();
      stmt.Reset ();
    }
}

namespace
{

struct XpResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, xp, 1);
};

} // anonymous namespace

SkillManager::XpLevel&
SkillManager::operator[] (const proto::SkillType t)
{
  auto mit = levels.find (t);

  if (mit == levels.end ())
    {
      auto stmt = db.Prepare (R"(
        SELECT `xp`
          FROM `account_xps`
          WHERE `name` = ?1 AND `skill` = ?2
      )");
      stmt.Bind (1, name);
      stmt.Bind (2, static_cast<int> (t));

      std::unique_ptr<XpLevel> entry;

      auto res = stmt.Query<XpResult> ();
      if (!res.Step ())
        {
          VLOG (2)
              << "Account " << name
              << " does not have any XP for skill " << t
              << " yet, creating empty XpLevel instance";
          entry.reset (new XpLevel (0));
        }
      else
        {
          VLOG (2)
              << "Loaded XpLevel instance for account " << name
              << " and skill " << t << " from the database";
          entry.reset (new XpLevel (res.Get<XpResult::xp> ()));
          CHECK (!res.Step ());
        }

      CHECK (entry != nullptr);
      const auto ins = levels.emplace (t, std::move (entry));
      CHECK (ins.second);
      mit = ins.first;
    }

  return *mit->second;
}

void
SkillManager::XpLevel::AddXp (const int64_t v)
{
  CHECK_GT (v, 0) << "XP added is not positive";
  const int64_t newValue = num + v;
  CHECK_GT (newValue, num) << "XP overflow, adding " << v << " to " << num;
  num = newValue;
  dirty = true;
}

} // namespace pxd
