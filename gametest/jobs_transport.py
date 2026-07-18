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
Integration test for the on-chain jobs board, transport type.  Exercises the
full move path (moveprocessor "j" dispatch, block processing, the paged
getjobspage read RPC and settlement) end-to-end: post -> accept -> deliver,
plus cancel and the accept-then-cancel-rejected case.  The per-op logic is
covered exhaustively by the C++ unit tests; this proves the real chain path
and coin/inventory effects.
"""

from pxtest import PXTest


class JobsTransportTest (PXTest):

  def available (self, name):
    return self.getAccounts ()[name].getBalance ("available")

  def reserved (self, name):
    return self.getAccounts ()[name].getBalance ("reserved")

  def run (self):
    self.mainLogger.info ("Setting up accounts and a destination building...")
    self.initAccount ("poster", "r")
    self.initAccount ("courier", "r")
    self.generate (1)
    # Lower the minimum-reward floors (roconfig 100/1000) the same way an
    # admin would: the suite's rewards predate the floors.  The defaults
    # themselves are exercised in jobs_caps.py.
    self.adminCommand ({"param": [
      {"n": "min-job-reward", "v": 1},
      {"n": "min-bounty-reward", "v": 1},
    ]})
    self.generate (1)
    self.giftCoins ({"poster": 1000000, "courier": 1000000})

    self.build ("checkmark", "poster", {"x": 0, "y": 0}, rot=0)
    self.buildingId = max (self.getBuildings ().keys ())
    self.assertEqual (self.getBuildings ()[self.buildingId].getOwner (),
                      "poster")

    self.testDeliver ()
    self.testCancel ()
    self.testProgressDump ()
    self.testHaul ()
    self.testHaulDestinationCapRecheck ()
    self.testSuperblockGating ()
    self.checkHistoryFromTime ()

    self.mainLogger.info ("Jobs transport integration test succeeded.")

  def testProgressDump (self):
    self.mainLogger.info ("Delivering bit-by-bit from character cargo...")
    self.sendMove ("poster", {"j": [{
      "t": "transport", "d": 86400, "wd": 86400, "r": 2000, "co": 0,
      "to": self.buildingId, "items": {"foo": 5},
    }]})
    self.generate (1)
    jobId = self.newestJob ()["id"]
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)

    # Dock a courier character at the destination and load part of the
    # manifest into its cargo from the courier's building inventory.
    self.createCharacters ("courier")
    self.generate (1)
    self.moveCharactersTo ({"courier": {"x": 3, "y": 0}})
    cId = self.getCharacters ()["courier"].getId ()
    self.sendMove ("courier", {"c": {"id": cId, "eb": self.buildingId}})
    self.generate (1)
    assert self.getCharacters ()["courier"].isInBuilding ()
    self.dropIntoBuilding (self.buildingId, "courier", {"foo": 3})
    self.sendMove ("courier", {"c": {"id": cId, "pu": {"f": {"foo": 3}}}})
    self.generate (1)

    # First dump: 3 of 5 land with the poster, the job records progress and
    # nothing is paid yet.
    posterBefore = self.getBuildings ()[self.buildingId] \
        .getFungibleInventory ("poster").get ("foo", 0)
    reservedBefore = self.reserved ("poster")
    self.sendMove ("courier", {"j": [{"f": jobId, "ch": cId}]})
    self.generate (1)
    job = self.newestJob ()
    self.assertEqual (job["id"], jobId)
    self.assertEqual (dict (job["items"]), {"foo": 2})
    self.assertEqual (self.reserved ("poster"), reservedBefore)
    self.assertEqual (
        self.getBuildings ()[self.buildingId]
            .getFungibleInventory ("poster").get ("foo", 0),
        posterBefore + 3)

    # Second trip completes the manifest and settles the reward.
    self.dropIntoBuilding (self.buildingId, "courier", {"foo": 2})
    self.sendMove ("courier", {"c": {"id": cId, "pu": {"f": {"foo": 2}}}})
    self.generate (1)
    before = self.available ("courier")
    self.sendMove ("courier", {"j": [{"f": jobId, "ch": cId}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (self.available ("courier"), before + 2000)

  def testHaul (self):
    self.mainLogger.info ("Testing a haul (poster-supplied goods)...")
    self.build ("checkmark", "poster", {"x": 50, "y": 0}, rot=0)
    destId = max (self.getBuildings ().keys ())
    srcId = self.buildingId

    self.dropIntoBuilding (srcId, "poster", {"bar": 7})
    self.sendMove ("poster", {"j": [{
      "t": "haul", "d": 86400, "wd": 86400, "r": 1500, "co": 4000,
      "from": srcId, "to": destId, "items": {"bar": 7},
    }]})
    self.generate (1)
    job = self.newestJob ()
    jobId = job["id"]
    self.assertEqual (job["type"], "haul")
    self.assertEqual (job["from"], srcId)
    self.assertEqual (job["to"], destId)
    # The goods are reserved by the job: out of the poster's inventory.
    self.assertEqual (
        self.getBuildings ()[srcId].getFungibleInventory ("poster")
            .get ("bar", 0), 0)

    self.mainLogger.info ("Accepting hands the goods to the worker...")
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (
        self.getBuildings ()[srcId].getFungibleInventory ("courier"),
        {"bar": 7})

    # The courier hauls them over (staged into their inventory at the
    # destination) and fulfils all-at-once.
    self.dropIntoBuilding (destId, "courier", {"bar": 7})
    before = self.available ("courier")
    self.sendMove ("courier", {"j": [{"f": jobId}]})
    self.generate (1)
    assert self.jobGone (jobId)
    self.assertEqual (
        self.getBuildings ()[destId].getFungibleInventory ("poster"),
        {"bar": 7})
    # Reward + collateral back.
    self.assertEqual (self.available ("courier"), before + 1500 + 4000)

  def testHaulDestinationCapRecheck (self):
    self.mainLogger.info ("Haul destination re-checks the entity cap "
                          "at accept...")
    # One shared destination and two FRESH sources (buildings from earlier
    # segments may still carry links).  With the per-entity cap at 1 both
    # POSTs admit (an OPEN haul counts against its source); the second
    # ACCEPT is what must reject, keeping that job OPEN until the gate
    # reopens.
    self.build ("checkmark", "poster", {"x": 100, "y": 0}, rot=0)
    dest = max (self.getBuildings ().keys ())
    self.build ("checkmark", "poster", {"x": 150, "y": 0}, rot=0)
    src1 = max (self.getBuildings ().keys ())
    self.build ("checkmark", "poster", {"x": 200, "y": 0}, rot=0)
    src2 = max (self.getBuildings ().keys ())
    self.dropIntoBuilding (src1, "poster", {"bar": 1})
    self.dropIntoBuilding (src2, "poster", {"bar": 1})

    self.adminCommand ({"param": [
      {"n": "max-jobs-per-linked-entity", "v": 1},
    ]})
    self.generate (1)

    ids = []
    for src in (src1, src2):
      before = len (self.getJobs ())
      self.sendMove ("poster", {"j": [{
        "t": "haul", "d": 86400, "wd": 86400, "r": 1500, "co": 0,
        "from": src, "to": dest, "items": {"bar": 1},
      }]})
      self.generate (1)
      self.assertEqual (len (self.getJobs ()), before + 1)
      ids.append (self.newestJob ()["id"])

    def state (jobId):
      return next (j["state"] for j in self.getJobs () if j["id"] == jobId)

    self.sendMove ("courier", {"j": [{"a": ids[0]}]})
    self.generate (1)
    self.assertEqual (state (ids[0]), "accepted")
    self.sendMove ("courier", {"j": [{"a": ids[1]}]})
    self.generate (1)
    self.assertEqual (state (ids[1]), "open")

    self.mainLogger.info ("Removing the override reopens the gate...")
    self.adminCommand ({"param": [
      {"n": "max-jobs-per-linked-entity", "v": None},
    ]})
    self.generate (1)
    self.sendMove ("courier", {"j": [{"a": ids[1]}]})
    self.generate (1)
    self.assertEqual (state (ids[1]), "accepted")

  def testSuperblockGating (self):
    self.mainLogger.info ("Testing a deadline crossing an ORDINARY block...")
    # Jobs settle by moves on every block, but the expiry sweep only runs on
    # superblocks.  Thread the deadline into the middle of a superblock
    # window: the ordinary block past it must leave the overdue job on the
    # board (and reject a fulfil), and only the next superblock settles it.
    self.sendMove ("poster", {"j": [{
      "t": "transport", "d": 86400, "wd": 7200, "r": 1000, "co": 500,
      "to": self.buildingId, "items": {"foo": 1},
    }]})
    self.generate (1)
    jobId = self.newestJob ()["id"]

    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)
    # Accepting starts the work clock: read the (rewritten, accept-relative)
    # deadline only now.
    deadline = self.newestJob ()["deadline"]
    # Stage the goods so the late fulfil would succeed but for the deadline.
    self.dropIntoBuilding (self.buildingId, "courier", {"foo": 1})

    # A block is a superblock iff its timestamp is >= superblock_seconds
    # past the previous superblock's -- setting explicit mock times around
    # that threshold lets us pick each block's kind exactly.
    sbSecs = self.roConfig ().params.superblock_seconds
    assert sbSecs >= 4, "test window needs superblock_seconds >= 4"

    # A superblock just inside the deadline: nothing is due yet.
    lastSb = deadline - 2
    self.env.setMockTime (lastSb)
    self.generate (1, superblocks=False)
    assert not self.jobGone (jobId)

    # An ORDINARY block past the deadline (gap 3 < superblock_seconds): the
    # fulfil move runs and is rejected (at/past deadline), no sweep runs --
    # the overdue job stays on the board.
    self.sendMove ("courier", {"j": [{"f": jobId}]})
    self.env.setMockTime (deadline + 1)
    self.generate (1, superblocks=False)
    assert not self.jobGone (jobId)
    self.assertEqual (self.newestJob ()["state"], "accepted")

    # The next superblock sweeps it: expiry FAILED, the worker's collateral
    # forfeits to the poster and the reward refunds.
    posterBefore = self.available ("poster")
    courierBefore = self.available ("courier")
    self.env.setMockTime (lastSb + sbSecs)
    self.generate (1, superblocks=False)
    assert self.jobGone (jobId)
    self.assertEqual (self.historyOutcome (jobId), "failed")
    self.assertEqual (self.available ("poster"), posterBefore + 1000 + 500)
    self.assertEqual (self.available ("courier"), courierBefore)

  def checkHistoryFromTime (self):
    """Guards the getjobshistory param wiring: a fromtime above every
    settlement returns nothing.  A fromtime/afterid transposition (the
    alphabetical stub binding) would instead treat it as an id cursor and
    return the whole history, so this deterministically catches the swap."""
    rows = self.historyRows ()
    assert rows, "expected some settled jobs by now"
    maxT = max (e["settledtime"] for e in rows)
    self.assertEqual (
        self.getRpc ("getjobshistory", fromtime=str (maxT + 1),
                     aftertime="0", afterid="0", limit=1000),
        [])

  def testDeliver (self):
    self.mainLogger.info ("Posting a transport job...")
    self.sendMove ("poster", {"j": [{
      "t": "transport", "d": 86400, "wd": 43200, "r": 2000, "co": 8000,
      "to": self.buildingId, "items": {"foo": 5},
    }]})
    self.generate (1)

    job = self.onlyJob ()
    jobId = job["id"]
    self.assertEqual (job["state"], "open")
    self.assertEqual (job["poster"], "poster")
    self.assertEqual (job["reward"], 2000)
    # The work window is a visible term on the board.
    self.assertEqual (job["wd"], 43200)
    self.assertEqual (job["building"], self.buildingId)
    self.assertEqual (dict (job["items"]), {"foo": 5})
    # Reward 2000 escrowed + fee max(1, 2000*100/10000 = 20) = 20 burned.
    self.assertEqual (self.available ("poster"), 1000000 - 2000 - 20)
    self.assertEqual (self.reserved ("poster"), 2000)

    self.mainLogger.info ("Accepting the job...")
    openDeadline = job["deadline"]
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)
    job = self.onlyJob ()
    self.assertEqual (job["state"], "accepted")
    self.assertEqual (job["worker"], "courier")
    # The accept rewrote the deadline to accept time + the work window
    # (43200 < the 86400 listing window, so it moved strictly closer).
    assert job["deadline"] < openDeadline
    self.assertEqual (self.available ("courier"), 1000000 - 8000)
    self.assertEqual (self.reserved ("courier"), 8000)

    self.mainLogger.info ("Delivering from the worker's own inventory at B...")
    # Stage the goods over what would be one or more trips, then fulfil once.
    self.dropIntoBuilding (self.buildingId, "courier", {"foo": 5})
    self.sendMove ("courier", {"j": [{"f": jobId}]})
    self.generate (1)

    self.assertEqual (self.getJobs (), [])
    b = self.getBuildings ()[self.buildingId]
    self.assertEqual (b.getFungibleInventory ("poster"), {"foo": 5})
    self.assertEqual (b.getFungibleInventory ("courier"), {})
    # Worker paid the reward and got the collateral back; no reserve left.
    self.assertEqual (self.available ("courier"),
                      1000000 - 8000 + 2000 + 8000)
    self.assertEqual (self.reserved ("courier"), 0)
    # The chain deleted the live row; the history holds the true outcome.
    self.assertEqual (self.historyOutcome (jobId), "completed")

  def testCancel (self):
    self.mainLogger.info ("Testing cancel and accept-then-cancel-rejected...")
    post = {"j": [{
      "t": "transport", "d": 86400, "wd": 86400, "r": 1000, "co": 500,
      "to": self.buildingId, "items": {"foo": 1},
    }]}

    self.sendMove ("poster", post)
    self.generate (1)
    jobId = self.onlyJob ()["id"]
    before = self.available ("poster")
    self.assertEqual (self.reserved ("poster"), 1000)

    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    self.assertEqual (self.getJobs (), [])
    self.assertEqual (self.available ("poster"), before + 1000)
    self.assertEqual (self.reserved ("poster"), 0)
    self.assertEqual (self.historyOutcome (jobId), "cancelled")

    # Repost, have the courier accept, then the poster's cancel is rejected.
    self.sendMove ("poster", post)
    self.generate (1)
    jobId = self.onlyJob ()["id"]
    self.sendMove ("courier", {"j": [{"a": jobId}]})
    self.generate (1)
    self.assertEqual (self.onlyJob ()["state"], "accepted")

    self.sendMove ("poster", {"j": [{"c": jobId}]})
    self.generate (1)
    self.assertEqual (self.onlyJob ()["state"], "accepted")


if __name__ == "__main__":
  JobsTransportTest ().main ()
