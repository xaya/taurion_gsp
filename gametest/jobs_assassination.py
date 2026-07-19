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
Integration test for the assassination contract on the real chain path: the
approval-required hire (assign + enemy-only accept), per-kill slices driven by
actual combat kills of the designated assassin, the full-quota completion, the
"any kill is a pass" expiry, and the zero-kill failure.
"""

from pxtest import PXTest


class JobsAssassinationTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def reserved (self, name):
    return self.getAccounts ()[name].getBalance ("reserved")

  def stageKill (self, victimChar, hunterChars):
    """
    Teleports the hunters onto the victim, drops the victim to 1 HP and lets
    combat kill it.  The hunters must be allies of each other and enemies of
    the victim (see jobs_bounty.stageKill for why allied hunters make the kill
    and its damage-list attribution deterministic).
    """
    targets = {victimChar: {"x": 0, "y": 0}}
    for h in hunterChars:
      targets[h] = {"x": 0, "y": 0}
    self.moveCharactersTo (targets)
    self.setCharactersHP ({victimChar: {"a": 1, "s": 0}})
    self.generate (1)
    assert victimChar not in self.getCharacters ()

  def theHit (self):
    jobs = [j for j in self.getJobs () if j["type"] == "assassination"]
    self.assertEqual (len (jobs), 1)
    return jobs[0]

  def hire (self, jobId, worker):
    """Assigns the hit to `worker` and has them accept it."""
    self.sendMove ("poster", {"j": [{"s": jobId, "w": worker}]})
    self.generate (1)
    self.sendMove (worker, {"j": [{"a": jobId}]})
    self.generate (1)

  def run (self):
    self.mainLogger.info ("Setting up accounts...")
    self.initAccount ("poster", "r")
    self.initAccount ("victim", "b")
    # The assassin is an enemy (GREEN) of the (BLUE) victim; a same-faction
    # (BLUE) would-be assassin proves the enemy-only accept rule.
    self.initAccount ("assassin", "g")
    self.initAccount ("bluefriend", "b")
    self.generate (1)
    # Lower the minimum-reward floors (roconfig 100/1000) the same way an admin
    # would: the suite's rewards predate the floors (exercised in jobs_caps.py).
    self.adminCommand ({"param": [
      {"n": "min-job-reward", "v": 1},
      {"n": "min-bounty-reward", "v": 1},
    ]})
    self.generate (1)
    self.giftCoins ({"poster": 1000000})

    self.testHireAndComplete ()
    self.testExpiryIsPass ()
    self.testExpiryZeroKillsFails ()

    self.mainLogger.info ("Jobs assassination integration test succeeded.")

  def testHireAndComplete (self):
    self.mainLogger.info ("Posting a hit on 'victim' (2 kills)...")
    self.sendMove ("poster", {"j": [{
      "t": "assassination", "d": 86400, "wd": 86400,
      "r": 6000, "co": 0, "name": "victim", "n": 2,
    }]})
    self.generate (1)
    job = self.theHit ()
    jobId = job["id"]
    self.assertEqual (job["target"], "victim")
    self.assertEqual (job["quota"], 2)
    self.assertEqual (job["remaining"], 2)
    self.assertEqual (job["tranche"], 3000)
    self.assertEqual (job["state"], "open")
    assert "faction" not in job
    self.assertEqual (self.reserved ("poster"), 6000)

    self.mainLogger.info ("A same-faction worker cannot be hired...")
    self.sendMove ("poster", {"j": [{"s": jobId, "w": "bluefriend"}]})
    self.generate (1)
    self.sendMove ("bluefriend", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (self.theHit ()["state"], "open")

    self.mainLogger.info ("The enemy assassin is hired...")
    self.hire (jobId, "assassin")
    self.assertEqual (self.theHit ()["state"], "accepted")
    self.assertEqual (self.theHit ()["worker"], "assassin")

    self.mainLogger.info ("A kill by the assassin pays one slice, no stats...")
    self.createCharacters ("victim")
    self.createCharacters ("assassin")
    self.generate (1)
    self.changeCharacterVehicle ("assassin", "light attacker")
    before = self.available ("assassin")
    self.stageKill ("victim", ["assassin"])
    self.assertEqual (self.available ("assassin"), before + 3000)
    job = self.theHit ()
    self.assertEqual (job["remaining"], 1)
    self.assertEqual (job["reward"], 3000)
    self.assertEqual (self.reserved ("poster"), 3000)
    # No reputation until the contract settles.
    self.assertEqual (
        self.getAccounts ()["assassin"].data["jobstats"]["completed"], 0)

    self.mainLogger.info ("The quota'th kill completes the contract...")
    self.createCharacters ("victim")
    self.generate (1)
    before = self.available ("assassin")
    self.stageKill ("victim", ["assassin"])
    self.assertEqual (self.available ("assassin"), before + 3000)
    assert self.jobGone (jobId)
    self.assertEqual (self.reserved ("poster"), 0)
    # The completion is counted once, at the full earned value.
    stats = self.getAccounts ()["assassin"].data["jobstats"]
    self.assertEqual (stats["completed"], 1)
    self.assertEqual (stats["value"], 6000)
    self.assertEqual (self.historyOutcome (jobId), "completed")

  def testExpiryIsPass (self):
    self.mainLogger.info ("A hit with one kill is a pass at expiry...")
    self.sendMove ("poster", {"j": [{
      "t": "assassination", "d": 86400, "wd": 86400,
      "r": 5000, "co": 0, "name": "victim", "n": 5,
    }]})
    self.generate (1)
    jobId = self.theHit ()["id"]
    self.hire (jobId, "assassin")

    self.createCharacters ("victim")
    self.createCharacters ("assassin")
    self.generate (1)
    self.changeCharacterVehicle ("assassin", "light attacker")
    assassinBefore = self.available ("assassin")
    posterBefore = self.available ("poster")
    statBefore = self.getAccounts ()["assassin"].data["jobstats"]["completed"]
    self.stageKill ("victim", ["assassin"])
    # One slice of 1000 earned.
    self.assertEqual (self.available ("assassin"), assassinBefore + 1000)

    # Warp mock time past the (accept-rewritten) work-window deadline and
    # mine one block: the jump is far larger than a superblock interval, so
    # the block is a superblock and its expiry sweep runs.
    deadline = self.theHit ()["deadline"]
    self.env.setMockTime (deadline + 1)
    self.generate (1, superblocks=False)
    assert self.jobGone (jobId)
    self.assertEqual (self.historyOutcome (jobId), "completed")
    # The four unearned slices refund the poster; the pass counts once at the
    # value actually earned (1 * 1000).
    self.assertEqual (self.available ("poster"), posterBefore + 4000)
    stats = self.getAccounts ()["assassin"].data["jobstats"]
    self.assertEqual (stats["completed"], statBefore + 1)

  def testExpiryZeroKillsFails (self):
    self.mainLogger.info ("A hit with no kills fails at expiry...")
    self.sendMove ("poster", {"j": [{
      "t": "assassination", "d": 86400, "wd": 86400,
      "r": 4000, "co": 0, "name": "victim", "n": 2,
    }]})
    self.generate (1)
    jobId = self.theHit ()["id"]
    self.hire (jobId, "assassin")

    posterBefore = self.available ("poster")
    failBefore = self.getAccounts ()["assassin"].data["jobstats"]["failed"]
    posterFailBefore = \
        self.getAccounts ()["poster"].data["jobstats"]["posterfailed"]

    deadline = self.theHit ()["deadline"]
    self.env.setMockTime (deadline + 1)
    self.generate (1, superblocks=False)
    assert self.jobGone (jobId)
    self.assertEqual (self.historyOutcome (jobId), "failed")
    # The whole reward refunds (no collateral was ever posted); the assassin's
    # failed counter bumps but the poster is NOT marked (no forfeit to harvest).
    self.assertEqual (self.available ("poster"), posterBefore + 4000)
    stats = self.getAccounts ()["assassin"].data["jobstats"]
    self.assertEqual (stats["failed"], failBefore + 1)
    self.assertEqual (
        self.getAccounts ()["poster"].data["jobstats"]["posterfailed"],
        posterFailBefore)


if __name__ == "__main__":
  JobsAssassinationTest ().main ()
