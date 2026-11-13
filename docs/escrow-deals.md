# Jobs

A job is a deal between two accounts for which the game holds the coins.
The poster locks a reward.  The worker who accepts the job may have to lock
collateral.  When both confirm that the job is done, the worker is paid.  When
one of them disputes it, the arbiter that the poster named rules which
percentage of the job counts as done, and reward and collateral are split
accordingly.  What is still open at the deadline is settled then.

The game does not know what a job is about and cannot check whether it was
done.  It only holds the coins and releases them by the rules below.

## Moves

The `j` field of a move is an array of operations.  They are processed in
order, and an operation that is malformed or not valid is skipped.  The account
has to be initialised.  Job operations depend on the block timestamp, so that
they are processed for confirmed blocks only and not for pending moves.

The empty string is a valid account name.  In the data of a job, it stands for
no worker or arbiter, so that the account with that name cannot take part in
jobs: it cannot send job operations or be named in them.

| Operation | Format |
| --- | --- |
| post | `{"t": "deal", "d": seconds, "r": reward, "co": collateral}` and the optional fields below |
| assign | `{"s": id, "w": account}` |
| accept | `{"a": id}` |
| cancel | `{"c": id}` |
| confirm | `{"dl": id, "confirm": true}` |
| dispute | `{"dl": id, "dispute": true}` |
| rule | `{"dl": id, "rule": percent}` |

An operation with any other key is malformed.  The optional fields of a post:

| Field | Meaning |
| --- | --- |
| `arbiter` | the account that rules on a dispute |
| `fee` | the fee of the arbiter in basis points (only with an arbiter) |
| `w` | the only account that may accept the job; with an empty string, nobody may until the poster assigns one |
| `tag` | a category from 0 to 100 |
| `terms` | free text of up to 1,000 bytes |

The game stores `tag` and `terms` for the parties and the arbiter, but does
not use them.

## The life of a job

**Post.**  The deadline is the block time plus `d`, which is at most
`max_listing_window`.  The reward is at least `min_job_reward` (and never
zero).  The collateral is at most `deal_max_collateral_bps` of the reward and
at most `deal_max_collateral`.  The arbiter and the account in `w` have to be
initialised accounts other than the poster and different from each other.  The
fee is at most `deal_max_fee_bps`, and tax and fee together are below 100%.
The poster needs the reward and the posting fee, which is `job_post_fee_bps` of
the reward but at least `job_post_fee_min`.  The reward is locked and the
posting fee is burnt.  No job can be posted while the poster has
`max_jobs_per_poster` jobs or there are `max_live_jobs` in total.

A job keeps the tax (`deal_tax_bps`) and the reaction window (see below) from
the config at the time it was posted, so that a later change of the config
does not affect it.

**Before it is accepted**, the poster can cancel the job, which returns the
reward but not the posting fee.  The poster can also assign it, which sets or
changes the only account that may accept it.

**Accept.**  Any initialised account other than the poster and the arbiter can
accept the job, unless it is reserved for another account.  The collateral is
locked.  The deadline stays as it is.

**After it is accepted**, the poster and the worker can each either confirm or
dispute the job, once.  With the second confirmation, the job is settled as
100% done.  After a dispute, nothing can be confirmed any more, and the arbiter
(if there is one) can rule which percentage of the job counts as done, in
multiples of ten.  The ruling settles the job.

**At the deadline.**  Once the block time has reached the deadline, no
operation is valid for the job any more.  With the next superblock it is
settled depending on its state:

| State | Result |
| --- | --- |
| not accepted | the reward goes back to the poster |
| accepted, neither confirmed nor disputed | reward and collateral go back |
| confirmed by one party and not disputed | settled as 100% done |
| disputed and not ruled on | reward and collateral go back |

Coins that go back are returned in full, without tax or fee.  For a dispute
that is not ruled on, each party thus gets back just what it locked.

This also means that the worker of a disputed job is paid only if the arbiter
rules for it.  A job without an arbiter is thus only suitable for a worker who
trusts the poster not to dispute work that was done, and a job with one for a
worker who trusts the arbiter that the poster named.

A job is deleted when it is settled, cancelled or expired.

### The reaction window

A job has a reaction window `W`, which is `deal_reaction_window` but at most
the duration `d` of the job.  If a confirmation is sent when less than `W` is
left until the deadline, the deadline is moved to `W` after the current block
time, so that the other party has time to answer.  The same holds for a dispute
on a job with an arbiter, so that the arbiter has time to rule.

This can happen at most twice for a job: for the first confirmation, and for
a dispute of the other party after it.  (A second confirmation settles the job,
and after a dispute only the ruling is left.)  Each time, the deadline moves by
less than `W`.

## Settlement

For a job with reward `R` and collateral `C` that counts as `p` percent done,
with tax `t` and arbiter fee `f` in basis points:

    tax     = R * t / 10000
    arbiter = R * f / 10000
    worker  = (R - tax - arbiter + C) * p / 100
    poster  = R + C - worker - arbiter - tax

Each division is rounded down.  The tax is burnt.  Tax and fee are taken from
the reward and do not depend on `p` or the collateral, so that an arbiter
earns the same whatever it rules.  Of what is left of the reward and of the
collateral, the worker gets `p` percent.  The poster gets the rest, so that
the four amounts always add up to `R + C`.  With a ruling of 0%, it thus pays
tax and fee from the reward it gets back.

A job that is settled as 100% done without a dispute pays the fee of its
arbiter as well.

## Statistics

Accounts have counters for their jobs, which are shown in the account JSON and
not used by the game:

- `dealstats.completed` and `value`: jobs the account was paid for as worker
  and its part `R * p / 100` of their rewards (before tax and fee).  A job
  counts if `p` is not zero and tax was paid.
- `dealstats.posted` and `postedvalue`: the same jobs for their poster.
- `dealstats.disputed`: jobs of the account (as poster or worker) that were
  disputed.
- `arbiterstats.rulings` and `valueruled`: disputes the account ruled on as
  arbiter, and the reward plus collateral of those jobs.

Players can raise their own numbers with jobs between accounts of theirs, for
the price of the tax and the posting fees.  Nothing is recorded for an arbiter
that does not rule, since the poster names the arbiter without asking it.

## Limits

All jobs that are due on a superblock are settled with it, each with a constant
amount of work.  Their number is bounded by `max_live_jobs`, and each of them
has cost its poster the posting fee and locks at least the minimum reward.

## Parameters

| Parameter | Default | |
| --- | --- | --- |
| `job_post_fee_min` | 1 | minimum posting fee |
| `job_post_fee_bps` | 100 | posting fee in basis points of the reward |
| `max_listing_window` | 2,592,000 | maximum duration in seconds (30 days) |
| `max_live_jobs` | 10,000 | maximum number of jobs |
| `max_jobs_per_poster` | 200 | maximum number of jobs of one poster |
| `min_job_reward` | 1,000 | minimum reward |
| `deal_max_collateral_bps` | 20,000 | maximum collateral in basis points of the reward |
| `deal_max_collateral` | all coins | maximum collateral as amount |
| `deal_max_fee_bps` | 1,000 | maximum arbiter fee in basis points of the reward |
| `deal_tax_bps` | 300 | tax in basis points of the reward |
| `deal_reaction_window` | 86,400 | reaction window in seconds (30 on regtest) |

They can be changed with the `roconfig` admin command like the rest of the
config.  A limit of zero disables posting; existing jobs are not affected.

## RPC

- `getjobs` returns all jobs.  Each has `id`, `poster`, `reward`,
  `collateral`, `deadline`, `fee`, `tax`, `tag`, `reactionwindow`,
  `posterconfirmed`, `workerconfirmed`, `disputed` and `state` (`open` or
  `accepted`), and where they apply `worker`, `designated`, `inviteonly`,
  `arbiter`, `terms` and `disputetime`.  The jobs are also part of the full
  game state.
- `getjobsparams` returns the parameters above from the current config, by
  these names.
- In the account JSON, `balance.reserved` includes the coins an account has
  locked in jobs.
