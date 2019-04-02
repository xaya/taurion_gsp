-- Data for the characters in the game.
CREATE TABLE IF NOT EXISTS `characters` (

  -- The character ID, which is assigned based on libxayagame's AutoIds.
  `id` INTEGER PRIMARY KEY,

  -- The Xaya name that owns this character (and is thus allowed to send
  -- moves for it).
  `owner` TEXT NOT NULL,

  -- The faction (as integer corresponding to the Faction enum in C++).
  -- We need this for querying combat targets, which should be possible to
  -- do without decoding the proto.
  `faction` INTEGER NOT NULL,

  -- Current position of the character on the map.  We need this in the table
  -- so that we can look characters up based on "being near" a given other
  -- coordinate.  Coordinates are in the axial system (as everywhere else
  -- in the backend).
  `x` INTEGER NOT NULL,
  `y` INTEGER NOT NULL,

  -- Movement data for the character that changes frequently and is thus
  -- not part of the big main proto.
  `volatilemv` BLOB NOT NULL,

  -- Current HP data as an encoded HP proto.  This is stored directly in
  -- the database rather than the "proto" BLOB since it is a field that
  -- is changed frequently (e.g. during HP regeneration) and without
  -- any explicit action.  Thus having it separate reduces performance
  -- costs and undo data size.
  `hp` BLOB NOT NULL,

  -- If non-zero, then the number represents for how many more blocks the
  -- character is "locked" at being busy (e.g. prospecting).
  `busy` INTEGER NOT NULL,

  -- Flag indicating if the character is currently moving.  This is set
  -- based on the encoded protocol buffer when updating the table, and is
  -- used so that we can efficiently retrieve only those characters that are
  -- moving when we do move updates.
  `ismoving` INTEGER NOT NULL,

  -- Flag indicating whether or not the character has a combat target.
  -- This is used so we can later efficiently retrieve only those characters
  -- that need to be processed for combat damage.
  `hastarget` INTEGER NOT NULL,

  -- Additional data encoded as a Character protocol buffer.
  `proto` BLOB NOT NULL

);

-- Non-unique indices for the characters table.
CREATE INDEX IF NOT EXISTS `characters_owner` ON `characters` (`owner`);
CREATE INDEX IF NOT EXISTS `characters_pos` ON `characters` (`x`, `y`);
CREATE INDEX IF NOT EXISTS `characters_busy` ON `characters` (`busy`);
CREATE INDEX IF NOT EXISTS `characters_ismoving` ON `characters` (`ismoving`);
CREATE INDEX IF NOT EXISTS `characters_hastarget` ON `characters` (`hastarget`);

-- =============================================================================

-- Data for regions where we already have non-trivial data.  Rows here are
-- only created over time, for regions when the first change is made
-- away from the "default / empty" state.
CREATE TABLE IF NOT EXISTS `regions` (

  -- The region ID as defined by the base map data.  Note that ID 0 is a valid
  -- value for one of the regions.  This ranges up to about 700k, but not
  -- all values are real regions.
  `id` INTEGER PRIMARY KEY,

  -- Additional data encoded as a RegionData protocol buffer.
  `proto` BLOB NOT NULL

);
