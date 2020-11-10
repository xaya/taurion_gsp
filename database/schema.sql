--  GSP for the Taurion blockchain game
--  Copyright (C) 2019-2020  Autonomous Worlds Ltd
--
--  This program is free software: you can redistribute it and/or modify
--  it under the terms of the GNU General Public License as published by
--  the Free Software Foundation, either version 3 of the License, or
--  (at your option) any later version.
--
--  This program is distributed in the hope that it will be useful,
--  but WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--  GNU General Public License for more details.
--
--  You should have received a copy of the GNU General Public License
--  along with this program.  If not, see <https://www.gnu.org/licenses/>.

-- =============================================================================

-- Data for the characters in the game.
CREATE TABLE IF NOT EXISTS `characters` (

  -- The character ID, which is assigned based on libxayagame's AutoIds.
  `id` INTEGER PRIMARY KEY,

  -- The Xaya name that owns this character (and is thus allowed to send
  -- moves for it).
  `owner` TEXT NOT NULL,

  -- The faction (as integer corresponding to the Faction enum in C++).
  -- We need this for querying combat targets, which should be possible to
  -- do without decoding the proto.  Note that this field is in theory
  -- redundant with the owner account's faction, but we have it duplicated here
  -- for easy access and because the faction is often needed for characters.
  `faction` INTEGER NOT NULL,

  -- Current position of the character on the map.  We need this in the table
  -- so that we can look characters up based on "being near" a given other
  -- coordinate.  Coordinates are in the axial system (as everywhere else
  -- in the backend).  May be NULL if the character is inside a building.
  `x` INTEGER NULL,
  `y` INTEGER NULL,

  -- ID of the building the character is in, if any.  NULL indicates they are
  -- outside, in which case their position is given by x/y.
  `inbuilding` INTEGER NULL,

  -- If the character has indicated they want to enter a building (if it
  -- is or becomes possible), then this holds the desired building's ID.
  `enterbuilding` INTEGER NULL,

  -- Movement data for the character that changes frequently and is thus
  -- not part of the big main proto.
  `volatilemv` BLOB NOT NULL,

  -- Current HP data as an encoded HP proto.  This is stored directly in
  -- the database rather than the "proto" BLOB since it is a field that
  -- is changed frequently (e.g. during HP regeneration) and without
  -- any explicit action.  Thus having it separate reduces performance
  -- costs and undo data size.
  `hp` BLOB NOT NULL,

  -- Data about HP regeneration encoded as RegenData proto.  This is accessed
  -- often and independently from the core proto, and thus split out for
  -- performance reasons.  It is not updated often, mostly read.
  `regendata` BLOB NOT NULL,

  -- The attacked target (if any), as a serialised TargetId proto.
  -- The presence of this column also tells us that there are enemies in
  -- range, which is important for area attacks.   So even if e.g. a character
  -- has just area attacks, we need to select one target for them nevertheless,
  -- as we later on only process attacks of characters with a selected target.
  `target` BLOB NULL,

  -- Whether or not friendlies are in range of a friendly attack, which
  -- indicates that this character needs to be processed for "dealing damage"
  -- next time a block is processed.
  `friendlytargets` INTEGER NOT NULL,

  -- Any combat effects that apply to the character (or NULL if none).
  -- This is a serialised CombatEffects protocol buffer.  The effects
  -- are set by the combat damaging phase, and then in effect until
  -- after the next block's damaging phase (so that e.g. range effects
  -- stay active through targeting and when that target gets actually
  -- affected next block).
  `effects` BLOB NULL,

  -- Flag indicating if the character is currently moving.  This is set
  -- based on the encoded protocol buffer when updating the table, and is
  -- used so that we can efficiently retrieve only those characters that are
  -- moving when we do move updates.
  `ismoving` INTEGER NOT NULL,

  -- Flag indicating if the character is currently mining.  This is set
  -- based on the protocol buffer, but also has an index so that we can
  -- efficiently retrieve only characters that are mining.
  `ismining` INTEGER NOT NULL,

  -- The range of the longest attack this character has or NULL if there
  -- is no attack at all.  This is used to speed up target finding without
  -- the need to look through the character's attacks (and parse the
  -- full proto) every time.
  `attackrange` INTEGER NULL,

  -- The range of the longest "attack" affecting friendlies, or NULL if there
  -- is no such attack.
  `friendlyrange` INTEGER NULL,

  -- Flag indicating whether a character may need HP regeneration.  This is
  -- set here (based on the RegenData and current HP) so that we can only
  -- retrieve and process characters that need regeneration.
  `canregen` INTEGER NOT NULL,

  -- The character's inventory encoded as Inventory proto.
  `inventory` BLOB NOT NULL,

  -- Additional data encoded as a Character protocol buffer.
  `proto` BLOB NOT NULL

);

-- Non-unique indices for the characters table.
CREATE INDEX IF NOT EXISTS `characters_owner` ON `characters` (`owner`);
CREATE INDEX IF NOT EXISTS `characters_pos` ON `characters` (`x`, `y`);
CREATE INDEX IF NOT EXISTS `characters_building` ON `characters` (`inbuilding`);
CREATE INDEX IF NOT EXISTS `characters_enterbuilding`
  ON `characters` (`enterbuilding`);
CREATE INDEX IF NOT EXISTS `characters_ismoving` ON `characters` (`ismoving`);
CREATE INDEX IF NOT EXISTS `characters_ismining` ON `characters` (`ismining`);
CREATE INDEX IF NOT EXISTS `characters_attackrange`
  ON `characters` (`attackrange`);
CREATE INDEX IF NOT EXISTS `characters_friendlyrange`
  ON `characters` (`friendlyrange`);
CREATE INDEX IF NOT EXISTS `characters_canregen` ON `characters` (`canregen`);
CREATE INDEX IF NOT EXISTS `characters_target` ON `characters` (`target`);
CREATE INDEX IF NOT EXISTS `characters_friendlytargets`
  ON `characters` (`friendlytargets`);
CREATE INDEX IF NOT EXISTS `characters_effects` ON `characters` (`effects`);

-- =============================================================================

-- Base data for the buildings in the game.  This does not include
-- more complex stuff like inventories in the building or trade orders
-- placed for it.
CREATE TABLE IF NOT EXISTS `buildings` (

  -- The building ID, which is assigned based on libxayagame's AutoIds.
  `id` INTEGER PRIMARY KEY,

  -- Type of this building.
  `type` TEXT NOT NULL,

  -- The Xaya name that owns this building.  NULL for ancient buildings.
  `owner` TEXT NULL,

  -- The faction (as integer corresponding to the Faction enum in C++).
  -- We need this for querying combat targets, which should be possible to
  -- do without decoding the proto.  Note that this field is in theory
  -- redundant with the owner account's faction, but we have it duplicated here
  -- for easy access and because the faction is often needed for characters.
  --
  -- Unlike characters, buildings can also be of the "ancient" faction.
  `faction` INTEGER NOT NULL,

  -- Centre coordinate of the building on the map.  We need this in the table
  -- so that we can look buildings up based on "being near" a given other
  -- coordinate.  Coordinates are in the axial system (as everywhere else
  -- in the backend).
  `x` INTEGER NOT NULL,
  `y` INTEGER NOT NULL,

  -- Current HP data as an encoded HP proto.  This is stored directly in
  -- the database rather than the "proto" BLOB since it is a field that
  -- is changed frequently.
  `hp` BLOB NULL NOT NULL,

  -- Data about HP regeneration encoded as RegenData proto.
  `regendata` BLOB NOT NULL,

  -- The attacked target (if any), as a serialised TargetId proto.
  `target` BLOB NULL,

  -- Whether or not friendlies are in range and processing needs to happen.
  `friendlytargets` INTEGER NOT NULL,

  -- Any combat effects that apply to the building (or NULL if none).
  -- This is a serialised CombatEffects protocol buffer.
  `effects` BLOB NULL,

  -- The range of the longest attack this building has or NULL if there
  -- is no attack at all, and the longest friendly affecting "attack".
  `attackrange` INTEGER NULL,
  `friendlyrange` INTEGER NULL,

  -- Flag indicating whether a building may need HP regeneration.
  `canregen` INTEGER NULL NOT NULL,

  -- Additional data encoded as a Building protocol buffer.
  `proto` BLOB NOT NULL

);

CREATE INDEX IF NOT EXISTS `buildings_pos` ON `buildings` (`x`, `y`);
CREATE INDEX IF NOT EXISTS `buildings_attackrange`
  ON `buildings` (`attackrange`);
CREATE INDEX IF NOT EXISTS `buildings_friendlyrange`
  ON `buildings` (`friendlyrange`);
CREATE INDEX IF NOT EXISTS `buildings_canregen` ON `buildings` (`canregen`);
CREATE INDEX IF NOT EXISTS `buildings_target` ON `buildings` (`target`);
CREATE INDEX IF NOT EXISTS `buildings_friendlytargets`
  ON `buildings` (`friendlytargets`);
CREATE INDEX IF NOT EXISTS `buildings_effects` ON `buildings` (`effects`);

-- =============================================================================

-- Tracks the damage lists:  Who damaged some character in the last N blocks,
-- so that we can award fame to them later.
CREATE TABLE IF NOT EXISTS `damage_lists` (

  -- The character being attacked.
  `victim` INTEGER NOT NULL,

  -- The attacking character.
  `attacker` INTEGER NOT NULL,

  -- The block height of the last damage done.
  `height` INTEGER NOT NULL,

  -- For each (victim, attacker) combination, there should only be a single
  -- entry.  For each new round of damage done, we "INSERT OR REPLACE" into
  -- the table, which updates the height for existing rows rather than
  -- adding new ones.
  PRIMARY KEY (`victim`, `attacker`)

);

-- Additional indices we need.  Note that lookups per victim are already
-- possible due to the primary key.
CREATE INDEX IF NOT EXISTS `damage_lists_attacker`
    ON `damage_lists` (`attacker`);
CREATE INDEX IF NOT EXISTS `damage_lists_height` ON `damage_lists` (`height`);

-- =============================================================================

-- Data stored for the Xaya accounts (names) themselves.
CREATE TABLE IF NOT EXISTS `accounts` (

  -- The Xaya p/ name of this account.
  `name` TEXT PRIMARY KEY,

  -- The faction (as integer corresponding to the Faction enum in C++).
  -- Can be null for accounts that are not yet initialised.  They are
  -- not allowed to properly play in the game, but they can hold and
  -- transfer vCHI.
  `faction` INTEGER NULL,

  -- Additional data for the account as a serialised Account proto.
  `proto` BLOB NOT NULL

);

-- =============================================================================

-- Data for regions where we already have non-trivial data.  Rows here are
-- only created over time, for regions when the first change is made
-- away from the "default / empty" state.
CREATE TABLE IF NOT EXISTS `regions` (

  -- The region ID as defined by the base map data.  Note that ID 0 is a valid
  -- value for one of the regions.  This ranges up to about 700k, but not
  -- all values are real regions.
  `id` INTEGER PRIMARY KEY,

  -- Block height when it was last modified.  We have an index on this, so that
  -- we can query only for regions modified recently from the frontend.
  `modifiedheight` INTEGER NOT NULL,

  -- The amount of resources left to be mined.  The type of resource is
  -- defined in the region proto.  The value is undefined for regions that
  -- have not been prospected yet.
  --
  -- This is not stored in the proto, because it may be changed frequently
  -- (on every turn) while the region is being mined actively.  Thus we avoid
  -- frequent updates of the proto by keeping it directly in the DB.
  `resourceleft` INTEGER NOT NULL,

  -- Additional data encoded as a RegionData protocol buffer.
  `proto` BLOB NOT NULL

);

CREATE INDEX IF NOT EXISTS `regions_by_modifiedheight`
  ON `regions` (`modifiedheight`);

-- =============================================================================

-- Data for piles of loot on the ground.  These represent the inventory
-- of each tile of the map that has at least some loot.
CREATE TABLE IF NOT EXISTS `ground_loot` (

  -- The coordinates of the pile.
  `x` INTEGER NOT NULL,
  `y` INTEGER NOT NULL,

  -- Serialised inventory proto.
  `inventory` BLOB NOT NULL,

  PRIMARY KEY (`x`, `y`)

);

-- =============================================================================

-- Data for the inventories that accounts have in buildings.
CREATE TABLE IF NOT EXISTS `building_inventories` (

  -- The ID of the building this is for.
  `building` INTEGER NOT NULL,

  -- The account that owns the stuff.
  `account` TEXT NOT NULL,

  -- Serialised inventory proto.
  `inventory` BLOB NOT NULL,

  PRIMARY KEY (`building`, `account`)

);

CREATE INDEX IF NOT EXISTS `building_inventories_by_account`
  ON `building_inventories` (`account`);

-- =============================================================================

-- Data about orders on the DEX.  Each entry in this table represents a
-- currently open limit order for trading of some fungible item for
-- Cubits inside a particular building.
--
-- Resources required when the order is taken (Cubits for a bid and
-- items for an ask) are deducted from the normal balance of a user and
-- "reserved" inside the order.  They get restored to the normal balance
-- if the order is cancelled (or for Cubits if the building is destroyed).
--
-- This table contains all data directly in columns rather than encoded into
-- protocol buffers, because this allows us to efficiently perform some
-- operations (like order matching or the computation of reserved balances)
-- directly through database statements.
CREATE TABLE IF NOT EXISTS `dex_orders` (

  -- The ID of the order.  This is used in moves to cancel it.
  `id` INTEGER PRIMARY KEY,

  -- The building this is in.
  `building` INTEGER NOT NULL,

  -- The account this belongs to.
  `account` TEXT NOT NULL,

  -- Type of this order (bid or ask).  The numeric values match the
  -- enum values defined in dex.hpp.
  `type` INTEGER NOT NULL,

  -- The item to be sold or bought.
  `item` TEXT NOT NULL,

  -- The quantity of item this order is for.
  `quantity` INTEGER NOT NULL,

  -- The price in Cubits per item (not in total).
  `price` INTEGER NOT NULL

);

-- This index allows summing up the reserved Cubit balances of
-- any particular user account in the entire world.
CREATE INDEX IF NOT EXISTS `dex_orders_by_account`
  ON `dex_orders` (`type`, `account`);

-- This index allows summing up the reserved item balances of accounts
-- inside a building.
CREATE INDEX IF NOT EXISTS `dex_orders_by_building`
  ON `dex_orders` (`building`, `type`, `account`, `item`);

-- This index allows order matching.  It also allows querying of all
-- orders for one building with sorting useful for building up order
-- books, which we need for RPC methods.
CREATE INDEX IF NOT EXISTS `dex_orders_by_price`
  ON `dex_orders` (`building`, `item`, `type`, `price`, `id`);

-- Log table of all executed trades.  This is only written and never
-- read during the state transition, so it is purely here to be queried
-- by RPC and won't affect consensus.
CREATE TABLE IF NOT EXISTS `dex_trade_history` (

  -- The log ID of the trade, which is just there to order them by
  -- creation time.
  `id` INTEGER PRIMARY KEY,

  -- The block height of the trade.
  `height` INTEGER NOT NULL,

  -- The block timestamp of the trade.
  `time` INTEGER NOT NULL,

  -- The building in which the trade took place.
  `building` INTEGER NOT NULL,

  -- The item traded.
  `item` TEXT NOT NULL,

  -- The traded quantity.
  `quantity` INTEGER NOT NULL,

  -- The trade price per unit.
  `price` INTEGER NOT NULL,

  -- The seller account.
  `seller` TEXT NOT NULL,

  -- The buyer account.
  `buyer` TEXT NOT NULL

);

-- Querying of the price history by item and optionally building.
CREATE INDEX IF NOT EXISTS `dex_trade_history_by_item_building`
  ON `dex_trade_history` (`item`, `building`, `id`);

-- =============================================================================

-- Data about counts of items found for a particular type already (for
-- things where it matters, like prizes and blueprints).
CREATE TABLE IF NOT EXISTS `item_counts` (

  -- Name of the item type.
  `name` TEXT PRIMARY KEY,

  -- Number of items of this type found already.
  `found` INTEGER NOT NULL

);

-- =============================================================================

-- Data about the money supply of vCHI.  This is a table with just a few
-- defined entries (keyed by a string) and an associated vCHI amount.
--
-- Keys that we use:
--
--  * "burnsale": vCHI bought in the burnsale process (in exchange for
--                burnt CHI).  This total also defines how much more vCHI
--                can be minted in that way.
--
--  * "gifted": vCHI that have been gifted through god mode (only possible
--              on regtest for testing).
--
CREATE TABLE IF NOT EXISTS `money_supply` (

  -- The key of an entry.
  `key` TEXT PRIMARY KEY,

  -- The associated amount of vCHI.
  `amount` INTEGER NOT NULL

);

-- =============================================================================

-- Ongoing operations that take a couple of blocks and need processing
-- at a later time (when they are done / need updates).
CREATE TABLE IF NOT EXISTS `ongoing_operations` (

  -- Unique ID for the operation.
  `id` INTEGER PRIMARY KEY,

  -- The block height at which the next processing / update is required.
  `height` INTEGER NOT NULL,

  -- Character ID that is associated to this operation (might be NULL if there
  -- is no associated character).  If the character dies, then the operation
  -- is removed.
  `character` INTEGER NULL,

  -- Building ID that is associated to this operation (if any).
  `building` INTEGER NULL,

  -- Main data for the operation encoded as serialised OngoingOperation proto.
  `proto` BLOB NOT NULL

);

CREATE INDEX IF NOT EXISTS `ongoing_operations_by_height`
  ON `ongoing_operations` (`height`);
CREATE INDEX IF NOT EXISTS `ongoing_operations_by_character`
  ON `ongoing_operations` (`character`);
CREATE INDEX IF NOT EXISTS `ongoing_operations_by_building`
  ON `ongoing_operations` (`building`);

-- =============================================================================
