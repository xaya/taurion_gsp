# The jobs board and the escrow deal

Design notes for the jobs subsystem: a player-driven contracts board on chain,
carrying one job type — the generic **escrow deal**.

This document describes the code as it stands in this repository.  It is aimed
at someone reviewing or extending the subsystem, and it concentrates on the
parts where a mistake would be a consensus bug rather than a gameplay one.

## 1. What it is, and why it is shaped this way

A **job** is an escrowed reward, plus a worker collateral, plus a deadline.  It
may additionally be exclusive to one designated worker.

The obvious way to build player contracts in a game like this is a catalogue of
verified types — transport this cargo, escort that vehicle, rent me a slot —
each with its own consensus rule that watches the world and decides whether the
term was met.  That approach was built and then abandoned, because most of what
players actually want to contract about is **not observable to consensus**.  The
chain can see that a vehicle reached a tile; it cannot see whether the cargo was
the agreed cargo, whether the escort actually protected anything, or whether a
service was rendered to the buyer's satisfaction.  Every type that pretends
otherwise is an approximation, and each approximation is an exploit surface.

So the chain does one thing it *can* do perfectly: **hold value and release it
according to a rule the parties agreed to in advance**.  A deal escrows the
poster's reward and the worker's collateral; a completion percentage `p` splits
both pots; and `p` comes from the parties themselves (both confirming), from an
arbiter they both consented to, or from a mechanical timeout rule.  The job
"category" (transport, haul, escort, …) survives only as a cosmetic integer tag
that consensus never reads.  Consensus adjudicates nothing it cannot see.

## 2. Where the code lives

| File | Responsibility |
| --- | --- |
| `database/jobs.{hpp,cpp}` | The board table: rows, queries, the admission counts and the reserved-coin sums. |
| `database/params.{hpp,cpp}` | Runtime-tunable named parameters (added in the preceding commit). |
| `src/jobs.{hpp,cpp}` | The whole subsystem above the table: the move-op lifecycle (post / assign / accept / cancel, plus the deal actions), the settlement math, and the superblock expiry sweep. |
| `proto/jobs.proto` | Everything in a deal that is not a real table column. |
| `database/schema.sql` | The `jobs` table and its indexes. |

There is no per-type abstraction: the board carries escrow deals and nothing
else, so the code names the deal directly rather than dispatching to it.

## 3. Data model

The split between columns and proto follows one rule: **anything the board or
the expiry sweep must filter, sort or sum on is a real column**; everything else
lives in the serialised `JobData` blob.

`jobs`: `id`, `status`, `poster`, `worker`, `reward`, `collateral`, `deadline`,
`proto`.

Indexes: `jobs_by_deadline` (the sweep and the "expiring soon" ordering), and
`jobs_by_poster` / `jobs_by_worker` (the per-account views and the
reserved-coin sums).

`Job::Status` is `OPEN` or `ACCEPTED`.  There is no settled status: a terminal
transition pays out and **deletes** the live row, which is what keeps the table
bounded regardless of how much the board is used.  What became of a settled
deal is therefore not retained on chain — see §10.

Consensus hygiene note: the state hash covers raw serialised proto bytes, and
protobuf map ordering is not guaranteed, so no consensus decision iterates a
proto map.

## 4. Move surface

All job operations arrive under the move key `"j"`, as an array.  Each object is
one operation, and each is validated to have **exactly** its expected members —
an unexpected or misspelled key makes the operation malformed and skipped, never
silently reinterpreted.

```
{"t":"deal","d":<secs>,"r":<reward>,"co":<collateral>, …}  post
{"s":<id>,"w":<account>}                                   assign a designated worker
{"a":<id>}                                                 accept
{"c":<id>}                                                 cancel (poster, while open)
{"dl":<id>,"confirm":true}                                 either party marks it done
{"dl":<id>,"dispute":true}                                 either party contests it
{"dl":<id>,"rule":<p>}                                     the bound arbiter rules
```

A post additionally accepts `arbiter`, `fee`, `tag`, `terms`, `dp` (the
advisory if-destroyed percentage) and `w` (designate a worker at post time).
The `"t"` value names the operation rather than selecting among kinds: the
board carries deals only, so anything else is rejected.

`d` is a **duration in seconds**, turned into an absolute consensus timestamp
deadline, so nothing about the board depends on block cadence.

Jobs are confirmed-only: the pending-move path never dispatches `"j"`.

## 5. Lifecycle

A post escrows the reward and burns a posting fee of
`max(job_post_fee_min, reward * job_post_fee_bps / 10000)`, so spam costs real
money at any reward size.  An accept escrows the worker's collateral.

Settlement reaches `p` by one of five routes, recorded in the history row as
`DealPayload.SettleMode`:

| Mode | When |
| --- | --- |
| `BOTH_CONFIRM` | both parties confirmed → `p = 100`, settles immediately |
| `RULING` | the bound arbiter ruled → the recorded `p` |
| `SINGLE_CONFIRM` | deadline reached, exactly one confirm, no dispute → `p = 100` |
| `GHOST_SPLIT` | deadline reached on an unruled dispute → `p = 50`, arbiter fee forfeited |
| `REFUND` | deadline reached, neither party acted → both stakes returned, untaxed |

`p` is restricted to multiples of ten.  This is a legibility choice, not a
mathematical one — it keeps rulings human-readable and arguable.

Two identity rules: **`poster == arbiter` is rejected at post**, and
`worker == arbiter` is rejected at accept (including via assign).  Both close
the same hole — an "arbiter" ruling on its own deal.

### The refund-both rule is scoped

A wholly untouched deal — neither party confirmed, no dispute — refunds both
stakes **in full and untaxed**, and the arbiter earns nothing (`fee_paid=false`;
its fee was never earned, including a pro-bono arbiter whose fee is zero).
Nobody transacted, so there is nothing to tax, and the burned posting fee plus
zero reputation accrual already close the capital-recycling loop a settlement
tax would have been defending against.

Refund-both is deliberately *not* used once someone has acted: it would rob
whoever already delivered, since the worker's labour is sunk while the poster's
reward is refundable.  A genuine deliverer protects itself by confirming, which
routes to `SINGLE_CONFIRM` instead.

### The reaction window

A `confirm` (any deal) or a `dispute` (arbiter-bound only) that executes with
`deadline - now < W` — strict `<` — and that still leaves a live counter-move
extends the deadline to `now + W`.  Both-confirm and a ruling settle instantly
and are never extended.

Each of the three flags (`poster_confirmed`, `worker_confirmed`, `disputed`) is
set-once, so the extension is one-shot per flag, strictly monotonic — it can
never shorten a deadline — and bounded at **`2W` past the original deadline**.

`W` is a per-deal snapshot taken at post: `W = min(the clamped runtime
parameter, the posted duration d)`, with a 30-day ceiling.  `0` disables the
mechanism.  A governance retune therefore reaches only deals posted after it; an
in-flight deal keeps the window it was posted with, exactly like the tax and fee
snapshots.  `ExtendForReactionWindow` in `src/jobs.cpp` is the canonical site.

The purpose is narrow: guarantee that a counterparty gets a real chance to
answer a last-moment confirm or dispute, rather than losing to a move landing in
the final block.

## 6. Settlement math

With `R` = reward, `C` = collateral, `t` = tax bps, `f` = arbiter fee bps
(0 with no arbiter), and `p` the completion percentage:

```
worker_reward     = R * p / 100                     // reward the worker earned
returned_C        = C * p / 100                     // collateral the worker keeps
seized_C          = C - returned_C                  // collateral the poster gets
poster_transacted = (R - worker_reward) + seized_C

worker_tax = worker_reward * t / 10000;      worker_fee = worker_reward * f / 10000
poster_tax = poster_transacted * t / 10000;  poster_fee = poster_transacted * f / 10000

treasury = worker_tax + poster_tax        // burned until a faction treasury exists
arbiter  = worker_fee + poster_fee
worker   = worker_reward - worker_tax - worker_fee + returned_C
poster   = (R + C) - worker - arbiter - treasury
```

Three properties matter, and each is there for a reason:

**Tax and fee fall only on transacted value**, never on the worker's returned
stake.  Taxing a returned bond would make accepting a deal cost money even when
nothing happened.

**Each party bears tax and fee on its own transacted share.** This keeps honest
completion strictly positive for the worker, and it avoids any 128-bit
intermediate — every multiplication stays inside the coin range.

**The poster's payout is the remainder**, which pins `Σ == R + C` exactly.
Division dust is burned, never redistributed; redistributing it would make the
split depend on rounding order.

`ComputeDealSettlement` is a pure function of the row.  `tax_bps`, `fee_bps` and
the reaction window are all snapshot into the row at post, and post validation
enforces `tax + fee < 10000` **on the snapshot values** — so no admissible row
can ever violate the settlement precondition, whatever the parameters are
retuned to afterwards.  This is also why a min-reward floor raised later cannot
reject a historical post on replay.

`DealTests.SettlementConservesExhaustive` pins conservation and non-negativity
over a 1,980-case grid: rewards `{0, 1, 1000, 50000, 1e11}` against collaterals
`{0, 7, 999, 50000}`, tax `{0, 300, 1000}` × fee `{0, 500, 1000}`, and every `p`
step.  The grid is chosen for edges rather than volume — zero pots, a pot at the
absolute coin cap, collateral both far below and above the reward, a
deliberately awkward `C = 7` for rounding, and the zero and maximum tax/fee
corners.

## 7. Expiry sweep and bounded work

`ExpireJobs` (called from `PXLogic::UpdateState`) runs **only on superblocks**,
while moves are processed on every block.  A deadline passing on an ordinary
block therefore does not settle anything; the job stays on the board until the
next sweep.  Operations at `deadline <= now` are rejected by `JobIsDue`, so a
due job belongs to the sweep alone and cannot be resurrected in the
move-before-sweep window.  `PXLogicTests.JobsExpireOnlyOnSuperblocks` pins this.

Bounding the sweep is done by **admission control at the door** rather than by
capping the sweep itself: a post past the global or per-poster cap is rejected.
The reason is that a sweep bound would change settlement semantics for rows
already on the board — a job could sit unsettled past its deadline through no
fault of its own — whereas a rejected post never existed.  Setting a cap to `0`
freezes posting outright without touching anything already listed.

`ValidateJobs`, called from `ValidateStateSlow`, checks the board's invariants.

## 8. Parameters

roconfig holds the defaults; the `parameters` table holds explicit overrides set
by the `"param"` admin command.  Reads pass their roconfig default as the
fallback, so removing an override cleanly restores it.

| roconfig field | runtime name | mainnet default |
| --- | --- | --- |
| `job_post_fee_min` | — | 1 vCHI, burned |
| `job_post_fee_bps` | — | 100 (1% of reward) |
| `max_listing_window` | — | 2592000 (30 days) |
| `max_live_jobs` | `max-live-jobs` | 10000 |
| `max_jobs_per_poster` | `max-jobs-per-poster` | 200 |
| `min_job_reward` | `min-job-reward` | 100 vCHI |
| `min_deal_reward` | `min-deal-reward` | 1000 vCHI |
| `deal_max_collateral_bps` | `deal-max-collateral-bps` | 20000 (2× reward) |
| `deal_max_collateral` | `deal-max-collateral` | the coin cap |
| `deal_max_fee_bps` | `deal-max-fee-bps` | 1000 (10%) |
| `deal_tax_bps` | `deal-tax-bps` | 300 (3%), burned |
| `deal_reaction_window` | `deal-reaction-window` | 86400 (24h) |

Collateral is bounded both relative to the reward and by an absolute ceiling, so
a poster cannot lure a worker into an unbounded stake.

`getjobsparams` reports every one of these as the **post-clamp effective value**
consensus would use right now, reusing the same clamping helper, so the reported
figure equals the enforced figure by construction.

## 9. Reputation counters

`proto/account.proto` carries seven counters: `deals_completed`,
`deals_value_completed`, `deals_posted_completed`, `deals_posted_value`,
`deals_disputed`, `arbiter_rulings` and `arbiter_value_ruled`.

**No consensus rule reads any of them back.** They exist so clients can show a
vetting signal, and they are stored in consensus state only because they must be
identical on every node that displays them.

They are raw tallies, not trust scores, and the honest limits are worth stating
plainly:

- A ring of sock puppets can wash-trade its own deals and pay only the burn
  (tax plus posting fee, about 4% of face value at the current defaults) for the
  record it manufactures.  The **value** counters are what make that cost
  proportional, so any consumer must weight by value and never read a bare count
  as an endorsement.
- `arbiter_value_ruled` is a **weaker** anti-Sybil anchor than the reward-based
  counters, because collateral returned at a high `p` bears no tax and so rides
  in for free — roughly 1.3% of the credited figure versus about 4%.  The two
  must not be compared as if they were the same currency.
- `deals_disputed` is bumped for **both** parties, since which side was at fault
  is exactly what consensus cannot know.  A hostile counterparty can inflate
  someone else's count by entering a deal and disputing it.  Read it relative to
  the completion counts, never as fault.
- `completed` means "settled with the worker earning something" — any
  tax-bearing settlement with `p > 0` — **not** "delivered in full".

Arbiter **ghosting** is deliberately *not* counted.  A post binds an arbiter
unilaterally, with no consent step, so a permanent per-account mark would be
inflictable on a non-consenting third party for the price of one throwaway deal.
Nothing is lost: the settled history row carries `arbiter`, `settle_mode`
(`GHOST_SPLIT`), `fee_paid` and `dispute_time` — `dispute_time` exists precisely
so a later reputation layer can attribute a ghost, where it stays scoped,
decayable and rebuttable instead of branded into consensus state.

The increments are unchecked at their declared widths.  Wrapping a `uint32`
count takes 2³² settlements, each burning a posting fee; wrapping a `uint64`
value counter takes ~1.8e8 maximum-value deals, each escrowing that value.  Both
are economically unreachable, and a wrap would be deterministic across nodes
anyway.  Saturating arithmetic was rejected as consensus-visible code for an
unreachable boundary.

## 10. Settled deals are not retained (deferred to archival)

A terminal transition deletes the row.  Nothing on chain then records that the
deal existed, how it settled, or who settled it — the board shows only what is
live.

An earlier revision of this change carried a `job_history` table: a snapshot
row written immediately before the live row was deleted, with a deterministic
retention prune to keep it bounded, served by a paged RPC.  It was removed
because settled-record retention is a general problem that deserves one general
mechanism, not a bespoke table per feature.  When that archival mechanism
lands, a settled deal should carry:

| Field | Meaning |
| --- | --- |
| `outcome` | completed / failed / cancelled / void |
| `settled_height` | the **real chain height** of the settling block, not the superblock counter — settlements happen on ordinary blocks too |
| `settled_time` | the settling block's consensus timestamp |
| `settled_p` | the effective completion percentage paid out; absent on a refund, where nothing transacted |
| `settle_mode` | `BOTH_CONFIRM` / `RULING` / `SINGLE_CONFIRM` / `GHOST_SPLIT` / `REFUND` |
| `fee_paid` | whether the agreed fee *schedule* was honoured — true on a both-confirm or a ruling (including a pro-bono arbiter, where honouring it moves zero coins), false only where the fee was forfeited |

plus a snapshot of the row's own columns and proto.  `settled_p` matters
because without it a `p=10` and a `p=90` ruling leave indistinguishable
records; `settle_mode` matters because it is what separates an arbiter that
ruled from one that ghosted.

What is **not** lost meanwhile: the aggregate record in §9 lives in
`account.proto` and is bumped at settlement, so per-account reputation survives
the row's deletion.  Only the per-deal itemisation is deferred.

## 11. RPCs

- `getjobs()` — the live board, ordered by id.  Unpaged, like `getbuildings`
  and `gettradehistory`: the board is bounded by the admission caps (§7), not
  by a response limit.
- `getjobsparams()` — the effective parameter values (see §8).

## 12. Deliberately out of scope

- A trustless on-chain "if destroyed" fire.  The poster may pre-set an advisory
  percentage that the arbiter reads and applies; automating it would
  re-introduce the entity-linkage kill hook this design removes.
- Charset and NFC filtering of the free-text `terms`.  Clients **must** sanitise
  control and bidi-override characters when rendering, since the arbiter reads
  that text to rule.
- Routing the protocol tax to a faction treasury.  It is burned until one
  exists.
- Any consensus consumption of the reputation counters (§9).
- Retention of settled deals (§10), pending a general archival mechanism.
