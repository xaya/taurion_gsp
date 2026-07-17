#!/usr/bin/env python3

#   GSP for the Taurion blockchain game
#   Copyright (C) 2020-2021  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""
Integration test for the standing wanted board.  Exercises the real chain
path: posting a standing bounty, tranche payouts driven by actual combat
kills (the pre-removal attribution sharing fame's owner set), the tranche
split across multiple hunters, notice-cancel, and pool completion.
"""

from pxtest import PXTest


class JobsBountyTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def reserved (self, name):
    return self.getAccounts ()[name].getBalance ("reserved")

  def theBounty (self):
    jobs = [j for j in self.getJobs () if j["type"] == "wanted"]
    self.assertEqual (len (jobs), 1)
    return jobs[0]

  def stageKill (self, victimChar, hunterChars):
    """
    Teleports the hunters onto the victim, drops the victim to 1 HP and lets
    combat kill it.

    All hunters MUST be the same faction (so they are allies of each other)
    and enemies of the victim.  Target selection picks uniformly at random
    among equidistant enemies (combat.cpp SelectTarget), so if the hunters
    were hostile to each other a hunter stacked on the victim's tile could
    roll the other hunter as its target instead of the victim -- the victim
    would then not die, or only one hunter would be credited for the kill.
    Keeping the hunters allied leaves the victim as their only in-range enemy,
    which makes the kill (and its damage-list attribution) deterministic.
    """
    targets = {victimChar: {"x": 0, "y": 0}}
    for h in hunterChars:
      targets[h] = {"x": 0, "y": 0}
    self.moveCharactersTo (targets)
    self.setCharactersHP ({victimChar: {"a": 1, "s": 0}})
    self.generate (1)
    assert victimChar not in self.getCharacters ()

  def run (self):
    self.mainLogger.info ("Setting up accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("victim", "b")
    # Both hunters share a faction: they are distinct accounts (so the tranche
    # still splits two ways) but allies in combat, so neither targets the other
    # when stacked on the victim -- see stageKill.  Two same-faction hunters
    # also prove the split dedups by ACCOUNT, not by faction.
    self.initAccount ("hunter", "g")
    self.initAccount ("hunter2", "g")
    self.generate (1)
    self.giftCoins ({"poster": 1000000})

    self.mainLogger.info ("Posting a standing bounty on 'victim'...")
    self.sendMove ("poster", {"j": [{
      "t": "wanted", "r": 6000, "co": 0, "name": "victim", "n": 2,
    }]})
    self.generate (1)
    job = self.theBounty ()
    jobId = job["id"]
    self.assertEqual (job["target"], "victim")
    self.assertEqual (job["quota"], 2)
    self.assertEqual (job["remaining"], 2)
    self.assertEqual (job["tranche"], 3000)
    assert "deadline" not in job
    assert "faction" not in job
    self.assertEqual (self.reserved ("poster"), 6000)

    self.mainLogger.info ("A combat kill pays one tranche...")
    self.createCharacters ("victim")
    self.createCharacters ("hunter")
    self.generate (1)
    self.changeCharacterVehicle ("hunter", "light attacker")
    before = self.available ("hunter")
    self.stageKill ("victim", ["hunter"])
    self.assertEqual (self.available ("hunter"), before + 3000)
    job = self.theBounty ()
    self.assertEqual (job["remaining"], 1)
    self.assertEqual (job["reward"], 3000)
    self.assertEqual (self.reserved ("poster"), 3000)

    self.mainLogger.info ("Notice-cancel converts it to a closing job...")
    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    job = self.theBounty ()
    assert "deadline" in job
    self.assertEqual (job.get ("closing"), True)

    # A second cancel is rejected (no window pushing): the deadline stays.
    deadline = job["deadline"]
    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    self.assertEqual (self.theBounty ()["deadline"], deadline)

    self.mainLogger.info ("A kill during the notice window still pays, "
                          "split across the distinct hunters...")
    self.createCharacters ("victim")
    self.createCharacters ("hunter2")
    self.generate (1)
    self.changeCharacterVehicle ("hunter", "light attacker")
    self.changeCharacterVehicle ("hunter2", "light attacker")
    b1 = self.available ("hunter")
    b2 = self.available ("hunter2")
    self.stageKill ("victim", ["hunter", "hunter2"])

    # The final tranche (3000) split across two distinct owners: 1500 each;
    # the pool is drained and the board row deleted.
    self.assertEqual (self.available ("hunter"), b1 + 1500)
    self.assertEqual (self.available ("hunter2"), b2 + 1500)
    self.assertEqual (
        [j for j in self.getJobs () if j["type"] == "wanted"], [])
    self.assertEqual (self.reserved ("poster"), 0)

    # The payouts are recorded in the on-chain completion counters.
    stats = self.getAccounts ()["hunter"].data["jobstats"]
    self.assertEqual (stats["completed"], 2)
    self.assertEqual (stats["value"], 4500)

    self.mainLogger.info ("Jobs bounty integration test succeeded.")


if __name__ == "__main__":
  JobsBountyTest ().main ()
