CREATE TABLE IF NOT EXISTS `characters` (
  `id` INTEGER PRIMARY KEY,

  -- The Xaya name that owns this character (and is thus allowed to send
  -- moves for it).
  `owner` TEXT NOT NULL,

  -- The name of the character in the game world.  This has to be unique
  -- among all characters and is used mainly for display purposes.  It is
  -- tracked directly in the database (rather than the encoded proto) so
  -- that we can verify uniqueness when creating a new character.
  `name` TEXT NOT NULL UNIQUE,

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

  -- Partial movement already done towards next tile.  This is used to
  -- "accumulate" movement when the speed is slower than one tile per block.
  -- This is a field in the database rather than the proto data so that it
  -- can be updated without replacing the entire proto BLOB.  This leads
  -- to more efficient undo data for the very common case of movement
  -- along the stepped path.
  `partialstep` INTEGER,

  -- Current HP data as an encoded HP proto.  This is stored directly in
  -- the database rather than the "proto" BLOB since it is a field that
  -- is changed frequently (e.g. during HP regeneration) and without
  -- any explicit action.  Thus having it separate reduces performance
  -- costs and undo data size.
  `hp` BLOB NOT NULL,

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
CREATE INDEX IF NOT EXISTS `characters_ismoving` ON `characters` (`ismoving`);
CREATE INDEX IF NOT EXISTS `characters_hastarget` ON `characters` (`hastarget`);
