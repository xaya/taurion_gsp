/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

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

#include "jobs.hpp"

#include "jsonutils.hpp"

#include "database/params.hpp"

#include <xayautil/jsonutils.hpp>

#include <glog/logging.h>

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pxd
{

/* ************************************************************************** */
/* Shared settlement helpers.                                                 */

namespace
{

/**
 * Whether a deadlined job is at or past its deadline (deadline <= now, the
 * exclusive boundary): such a job belongs to the expiry sweep, and the
 * lifecycle operations must not touch it.  The sweep only runs on superblocks
 * while moves run on every block, so without this guard an operation landing
 * between the deadline and the next superblock could change the settlement:
 * an accept would resurrect a listing that should void (an elapsed ad would
 * otherwise pay its full rent for zero display
 * time).  A job without a deadline is never due.
 */
bool
JobIsDue (const Job& job, const JobContext& jc)
{
  return job.GetDeadline () <= jc.ctx.Timestamp ();
}

/**
 * Applies the deal reaction window (design escrow-v1.1 §1): when a deal action
 * that leaves a live counter-move (a single CONFIRM, or an arbiter-bound
 * DISPUTE) executes at `now` strictly within the row's snapshotted
 * reaction_window of the deadline, the deadline is pushed to now + window so
 * the successor move always keeps a full window to answer.  Properties, all
 * test-pinned: the trigger is STRICT '<' (at deadline - now == W nothing is
 * needed, exactly W remains; at == W-1 it fires), so it never shortens
 * (now + W > deadline iff deadline - now < W) and is one-shot per set-once flag,
 * bounding total extension at 2W past the original deadline; W=0 is provably
 * inert (JobIsDue guarantees deadline > now at execution, so
 * deadline - now >= 1 > 0).  The moved deadline column shifts the
 * jobs_by_deadline index row, and since now + W > now an extended deal is
 * never swept in its own block.
 */
void
ExtendForReactionWindow (Job& job, const int64_t now)
{
  const int64_t w = job.GetProto ().reaction_window ();
  if (w > 0 && job.GetDeadline () - now < w)
    job.SetDeadline (now + w);
}

/**
 * Deletes a settled job's row, releasing the row handle first so the delete
 * cannot collide with it on the unique-handle tracker.  Every terminal
 * transition funnels through here.
 */
void
DeleteSettled (JobsTable& jobs, JobsTable::Handle job)
{
  const auto id = job->GetId ();
  job.reset ();
  jobs.DeleteById (id);
}

/**
 * Fetches an account by name, CHECK-failing if it does not exist.  Callers
 * must ensure no other live handle to the same account row exists
 * (UniqueHandles): safe in the block hooks, but move-op code must use the
 * executor handle it already holds for that account.
 */
AccountsTable::Handle
GetAccountChecked (const JobContext& jc, const std::string& name)
{
  auto a = jc.accounts.GetByName (name);
  CHECK (a != nullptr) << "Job party account missing: " << name;
  return a;
}

/**
 * Bumps the worker's DEAL reputation counters.  Called once on any tax-bearing
 * settlement with p>0 -- a both-confirm/single-confirm release, a ghosted 50/50
 * split and a partial ruling all qualify, so "completed" counts the tax-bearing
 * settlements the worker earned on, not full deliveries.  `value` is the
 * worker's earned reward share (reward * p / 100), which scales the value
 * counter down on a split or a low-p ruling.
 */
void
BumpDealStats (Account& a, const Amount value)
{
  auto& pb = a.MutableProto ();
  pb.set_deals_completed (pb.deals_completed () + 1);
  pb.set_deals_value_completed (pb.deals_value_completed () + value);
}

/**
 * Bumps the POSTER's mirror of the counters above: called under the same
 * tax-bearing gate, with the same `value` (what the worker earned is what the
 * poster released), so one settlement moves both sides' records together.
 */
void
BumpPosterDealStats (Account& a, const Amount value)
{
  auto& pb = a.MutableProto ();
  pb.set_deals_posted_completed (pb.deals_posted_completed () + 1);
  pb.set_deals_posted_value (pb.deals_posted_value () + value);
}

/**
 * Records that a deal ended in a dispute, for one of its two parties.  Not
 * tax-gated (see the proto comment): a dispute is a fact about the deal, not
 * an earning, and it is the one counter nobody has an incentive to inflate.
 */
void
BumpDisputedStats (Account& a)
{
  auto& pb = a.MutableProto ();
  pb.set_deals_disputed (pb.deals_disputed () + 1);
}

/**
 * Records one dispute RULED by the arbiter that was bound to it, weighted by
 * the pot its judgement directed.  Only ever called for the arbiter's own
 * signed ruling move -- a ghosted dispute and a no-arbiter dispute both leave
 * no arbiter trace here on purpose (see the proto comment: the arbiter is
 * bound without consent, so consensus must not brand it for inaction).
 */
void
BumpArbiterRulingStats (Account& a, const Amount pot)
{
  auto& pb = a.MutableProto ();
  pb.set_arbiter_rulings (pb.arbiter_rulings () + 1);
  pb.set_arbiter_value_ruled (pb.arbiter_value_ruled () + pot);
}

/**
 * Hook-path settlement: the job is void through neither party's fault (an
 * OPEN job expired).  The reward refunds to the poster and any locked
 * collateral returns to the worker; no counters change.  Must not be called
 * while any account handle is live.
 */
void
VoidJobAtHook (const JobContext& jc, const Job& job)
{
  {
    auto poster = GetAccountChecked (jc, job.GetPoster ());
    ReleaseJobCoins (*poster, job.GetReward ());
  }
  if (job.GetStatus () == Job::Status::ACCEPTED)
    {
      auto worker = GetAccountChecked (jc, job.GetWorker ());
      ReleaseJobCoins (*worker, job.GetCollateral ());
    }
}

/** Upper bound on the byte length of a deal's free-text terms.  */
constexpr size_t MAX_DEAL_TERMS_LENGTH = 1'000;
/** Upper bound on a deal's cosmetic type tag.  */
constexpr uint32_t MAX_DEAL_TYPE_TAG = 100;

/**
 * The effective deal tax in bps (runtime param over the roconfig default).
 * Read in ValidateDealPost (the §6.3 door guard) and snapshot by ApplyDealPost,
 * so both see the same value within one post.
 */
int64_t
DealTaxBps (const JobContext& jc)
{
  return jc.params.Get ("deal-tax-bps",
                        jc.ctx.RoConfig ()->params ().deal_tax_bps ());
}

/**
 * The POST term keys beyond the generic t / d / r / co handled by the caller.
 * POST parsing rejects a move containing any other member, so the POST grammar
 * is exactly as strict as the lifecycle ops' (typos surface as rejections
 * instead of being silently ignored).  Returned by reference to a static so
 * the hot parse path allocates nothing.
 */
const std::vector<std::string>&
DealPostTermKeys ()
{
  static const std::vector<std::string> keys
      = {"arbiter", "fee", "tag", "terms", "dp", "w"};
  return keys;
}

/**
 * The minimum reward a POST must escrow, beyond the global "min-job-reward"
 * floor (the larger of the two applies), so occupying a capped board slot
 * always locks real value.
 */
Amount
MinDealReward (const JobContext& jc)
{
  return jc.params.Get ("min-deal-reward",
                        jc.ctx.RoConfig ()->params ().min_deal_reward ());
}

/**
 * Validates the deal terms of a POST move (no state change).  The generic
 * reward / collateral / deadline / affordability checks are done by the
 * caller; this only checks the deal terms.  Returns false to reject.
 */
bool
ValidateDealPost (const JobContext& jc, const Account& poster,
                  const Json::Value& terms)
{
  const auto& p = jc.ctx.RoConfig ()->params ();

  Amount reward, collateral;
  CHECK (CoinAmountFromJson (terms["r"], reward));
  CHECK (CoinAmountFromJson (terms["co"], collateral));

  /* Collateral is bounded relative to the reward AND by an absolute ceiling,
     so a poster cannot lure a worker into an unbounded stake (red-team T1).  */
  const int64_t maxColBps
      = jc.params.Get ("deal-max-collateral-bps",
                       p.deal_max_collateral_bps ());
  /* reward * maxColBps / 10000, overflow-guarded: a reward large enough to
     overflow the product means the ratio cap is effectively unlimited (the
     collateral is already bounded by CoinAmountFromJson), so only the
     absolute cap binds; a zero/negative bps freezes collateral to nothing.  */
  Amount maxByRatio;
  if (maxColBps <= 0)
    maxByRatio = 0;
  else if (reward > std::numeric_limits<Amount>::max () / maxColBps)
    maxByRatio = std::numeric_limits<Amount>::max ();
  else
    maxByRatio = reward * maxColBps / 10'000;
  const Amount maxAbs
      = jc.params.Get ("deal-max-collateral", p.deal_max_collateral ());
  if (collateral > maxByRatio || collateral > maxAbs)
    {
      LOG (WARNING) << "Deal collateral " << collateral << " exceeds the cap";
      return false;
    }

  /* Arbiter is optional; if named, it must be a non-empty, initialised
     account (an empty string would be a silently-meaningless member,
     which the strict grammar rejects rather than ignores).  */
  std::string arbiter;
  if (terms.isMember ("arbiter"))
    {
      if (!terms["arbiter"].isString ())
        return false;
      arbiter = terms["arbiter"].asString ();
      if (arbiter.empty ())
        return false;
      /* Poster == arbiter is FORBIDDEN (design §1): under the reaction window
         a poster-arbiter could dispute a late single-confirm (+W) and then
         rule p=0 for a total seizure that v1's hard deadline capped at the
         50/50 ghost split -- an armed trap primitive with no legitimate use,
         banned at the post door.  This guarantees a distinct ACCOUNT, not a
         distinct person: nothing stops one operator from arbitrating their
         own deal under a second Xaya name, and a named-arbiter model has no
         Sybil-resistant identity to check.  The protection is disclosure --
         the arbiter is bound at POST, so the worker sees who will judge
         before staking collateral.  */
      if (arbiter == poster.GetName ())
        {
          LOG (WARNING) << "Deal arbiter cannot be the poster: " << arbiter;
          return false;
        }
      const auto a = jc.accounts.GetByName (arbiter);
      if (a == nullptr || !a->IsInitialised ())
        {
          LOG (WARNING) << "Deal arbiter not initialised: " << arbiter;
          return false;
        }
    }

  /* Optional worker designation at POST (design §3, closing F1's negotiate-
     then-invite snipe): a non-empty "w" makes the deal private from birth --
     it must be an existing initialised account, != the poster, and != the
     arbiter named in the same post.  An empty string ("w":"") is the legal
     private-unassigned state (invite-only, designee chosen later via ASSIGN).
     Any violation rejects the WHOLE post (nothing charged).  ApplyDealPost
     persists the designation and the invite_only flag.  */
  if (terms.isMember ("w"))
    {
      if (!terms["w"].isString ())
        return false;
      const std::string w = terms["w"].asString ();
      if (!w.empty ())
        {
          if (w == poster.GetName ())
            {
              LOG (WARNING) << "Deal worker cannot be the poster: " << w;
              return false;
            }
          if (w == arbiter)
            {
              LOG (WARNING) << "Deal worker cannot be the arbiter: " << w;
              return false;
            }
          const auto a = jc.accounts.GetByName (w);
          if (a == nullptr || !a->IsInitialised ())
            {
              LOG (WARNING) << "Deal designated worker not initialised: " << w;
              return false;
            }
        }
    }

  /* Arbiter fee in bps, bounded by the cap.  A fee without an arbiter to
     earn it is rejected, not silently dropped (strict grammar): the poster
     most likely mistyped or forgot the arbiter member.  */
  int64_t fee = 0;
  if (terms.isMember ("fee"))
    {
      if (!terms.isMember ("arbiter"))
        {
          LOG (WARNING) << "Deal fee given without an arbiter";
          return false;
        }
      if (!terms["fee"].isInt64 () || !xaya::IsIntegerValue (terms["fee"]))
        return false;
      fee = terms["fee"].asInt64 ();
      if (fee < 0
            || fee > jc.params.Get ("deal-max-fee-bps",
                                    p.deal_max_fee_bps ()))
        return false;
    }

  /* The §6.3 settlement precondition (0 <= tax and tax + fee < 10000),
     enforced at the door on the values this row would SNAPSHOT: every
     future settlement of the row must satisfy it whatever the runtime
     params are later retuned to.  Each operand is bounded on its own
     BEFORE the sum -- the runtime params are arbitrary int64s, so an
     unbounded pair could overflow the signed sum (UB) past this guard and
     then narrow to small uint32s whose persisted sum halts settlement.  */
  const int64_t tax = DealTaxBps (jc);
  if (tax < 0 || tax >= 10'000 || fee >= 10'000 || tax + fee >= 10'000)
    {
      LOG (WARNING)
          << "Deal tax " << tax << " + fee " << fee
          << " violate the settlement precondition";
      return false;
    }

  /* Cosmetic type tag (optional, bounded; NEVER read by settlement).  */
  if (terms.isMember ("tag"))
    {
      if (!terms["tag"].isUInt () || !xaya::IsIntegerValue (terms["tag"])
            || terms["tag"].asUInt () > MAX_DEAL_TYPE_TAG)
        return false;
    }

  /* Free-text terms (optional, byte-bounded; NFC/charset filter = Phase 2).  */
  if (terms.isMember ("terms"))
    {
      if (!terms["terms"].isString ()
            || terms["terms"].asString ().size () > MAX_DEAL_TERMS_LENGTH)
        return false;
    }

  /* Advisory destroyed-% (optional): 0..100.  */
  if (terms.isMember ("dp"))
    {
      if (!terms["dp"].isInt64 () || !xaya::IsIntegerValue (terms["dp"]))
        return false;
      const int64_t dp = terms["dp"].asInt64 ();
      if (dp < 0 || dp > 100)
        return false;
    }

  return true;
}

/**
 * Applies the (already-validated) terms to the freshly-created OPEN job.
 * Called from POST after the generic escrow + row creation.
 */
void
ApplyDealPost (const JobContext& jc, const Json::Value& terms, Job& job)
{
  /* Validation guarantees a fee only ever comes with an arbiter, and
     that both tax and fee satisfy the §6.3 precondition.  */
  auto& d = job.MutableProto ();
  if (terms.isMember ("arbiter"))
    d.set_arbiter (terms["arbiter"].asString ());
  if (terms.isMember ("fee"))
    d.set_fee_bps (static_cast<uint32_t> (terms["fee"].asInt64 ()));
  /* Snapshot the tax at post so §6.3 is a pure function of the row.  */
  d.set_tax_bps (static_cast<uint32_t> (DealTaxBps (jc)));
  if (terms.isMember ("tag"))
    d.set_type_tag (terms["tag"].asUInt ());
  if (terms.isMember ("terms"))
    d.set_terms (terms["terms"].asString ());
  if (terms.isMember ("dp"))
    d.set_destroyed_p (static_cast<uint32_t> (terms["dp"].asInt64 ()));

  /* Worker designation (design §3): a "w" term makes the deal invite-only
     from birth; a non-empty designee is the designated worker (the accept
     gate enforces it), an empty "w" is the private-unassigned state named
     later via ASSIGN.  */
  if (terms.isMember ("w"))
    {
      const std::string w = terms["w"].asString ();
      if (!w.empty ())
        d.set_designated_worker (w);
      d.set_invite_only (true);
    }

  /* Snapshot the reaction window at post -- W = min(the clamped
     deal-reaction-window param, the posted duration d) -- exactly like the
     tax snapshot above and for the same reason: the deal's terms must be a
     pure function of the row, so no admin retune can strip an in-flight
     deal's advertised protection, and a short deal never carries a window
     longer than its own duration (§1).  DealOperation::Execute reads THIS.  */
  const int64_t wParam
      = CappedParam (jc.params, "deal-reaction-window",
                     jc.ctx.RoConfig ()->params ().deal_reaction_window (),
                     CAP_DEAL_REACTION_WINDOW);
  d.set_reaction_window (std::min<int64_t> (wParam, terms["d"].asInt64 ()));
}

/**
 * Extra validation of an ACCEPT (no state change), on top of the generic
 * designation / runway / collateral checks.
 */
bool
ValidateDealAccept (const Job& job, const Account& worker)
{
  /* The arbiter must stay a third party: as worker it would judge its own
     dispute (accept + self-dispute + self-rule in one block would capture
     the whole escrow).  Poster == arbiter is barred at the post door now
     (design §1), so the arbiter is a distinct third account from both sides.  */
  if (worker.GetName () == job.GetProto ().arbiter ())
    {
      LOG (WARNING)
          << "Arbiter " << worker.GetName () << " cannot accept deal "
          << job.GetId ();
      return false;
    }
  return true;
}

} // anonymous namespace

/* ************************************************************************** */
/* Escrow-deal settlement.                                                    */

DealSettlement
ComputeDealSettlement (const Amount reward, const Amount collateral,
                       const int p, const int taxBps, const int feeBps)
{
  CHECK_GE (p, 0);
  CHECK_LE (p, 100);
  CHECK_GE (reward, 0);
  CHECK_GE (collateral, 0);
  CHECK_LT (taxBps + feeBps, 10'000);

  const Amount workerReward = reward * p / 100;
  const Amount returnedC = collateral * p / 100;
  const Amount seizedC = collateral - returnedC;
  /* Each party bears tax + fee on its OWN transacted share -- the worker on the
     reward it earned, the poster on the reward it reclaims plus the collateral
     it seizes.  No 128-bit intermediate is needed, and honest completion is
     never net-negative for the worker (workerTax + workerFee < workerReward).  */
  const Amount posterTransacted = (reward - workerReward) + seizedC;
  const Amount workerTax = workerReward * taxBps / 10'000;
  const Amount workerFee = workerReward * feeBps / 10'000;
  const Amount posterTax = posterTransacted * taxBps / 10'000;
  const Amount posterFee = posterTransacted * feeBps / 10'000;

  DealSettlement s;
  s.treasury = workerTax + posterTax;
  s.arbiter = workerFee + posterFee;
  s.worker = workerReward - workerTax - workerFee + returnedC;
  /* Remainder pinned to the poster => exact conservation for all inputs.  */
  s.poster = (reward + collateral) - s.worker - s.arbiter - s.treasury;

  CHECK_GE (s.worker, 0);
  CHECK_GE (s.poster, 0);
  CHECK_EQ (s.worker + s.poster + s.arbiter + s.treasury, reward + collateral);
  return s;
}

void
SettleDeal (const JobContext& jc, Job& job, const int p,
            Account* const executor, const bool payArbiterFee,
            const DealSettleMode mode)
{
  const auto& d = job.GetProto ();
  /* A forfeited fee (the ghosted-dispute timeout) settles as if none had
     been agreed: each party keeps its own fee share.  */
  const int feeBps = payArbiterFee ? d.fee_bps () : 0;
  const DealSettlement s = ComputeDealSettlement (
      job.GetReward (), job.GetCollateral (), p, d.tax_bps (), feeBps);

  /* Accumulate the net credit per distinct account name, so a self-arbiter or
     poster==arbiter is opened exactly once and never collides with the
     executor's live handle.  The treasury tax is intentionally NOT credited --
     it is burned until a faction treasury exists to receive it (Phase 4).

     A named arbiter joins that map only when there is something to do for it:
     a fee to pay, or a ruling of its own to record.  Skipping it otherwise
     spares an account read on the pro-bono happy path -- and, on a GHOST_SPLIT,
     keeps settlement from so much as OPENING the row of an account that was
     bound as arbiter without ever consenting and never acted.  */
  const bool ruledByArbiter = (mode == DealSettleMode::RULING);
  std::map<std::string, Amount> credit;
  credit[job.GetWorker ()] += s.worker;
  credit[job.GetPoster ()] += s.poster;
  if (!d.arbiter ().empty () && (s.arbiter > 0 || ruledByArbiter))
    credit[d.arbiter ()] += s.arbiter;

  const Amount earnedReward = job.GetReward () * p / 100;
  const Amount pot = job.GetReward () + job.GetCollateral ();
  const bool creditRep = (p > 0 && s.treasury >= 1);
  const std::string workerName = job.GetWorker ();
  const std::string posterName = job.GetPoster ();
  const std::string arbiterName = d.arbiter ();
  /* What took this deal off the happy path: either the bound arbiter ruled it,
     or nobody did and the sweep fell back to the blunt 50/50 split.  */
  const bool disputed = (ruledByArbiter
                            || mode == DealSettleMode::GHOST_SPLIT);

  for (const auto& entry : credit)
    {
      const std::string& name = entry.first;
      const Amount amount = entry.second;
      const bool isWorker = (name == workerName);
      const bool isPoster = (name == posterName);
      const bool isArbiter = (!arbiterName.empty () && name == arbiterName);
      /* The executor's row is already open, so reuse that handle rather than
         opening a second one for the same account; `held` stays null then and
         dies with the iteration, exactly as the two branches did before.  */
      AccountsTable::Handle held;
      if (executor == nullptr || name != executor->GetName ())
        held = GetAccountChecked (jc, name);
      Account& acc = (held != nullptr ? *held : *executor);

      if (amount > 0)
        ReleaseJobCoins (acc, amount);
      /* The role bumps are independent rather than mutually exclusive.  All
         three names are distinct today (post bars poster == arbiter, accept
         bars worker == poster and worker == arbiter), but written this way an
         overlap could only ever record MORE, never silently drop a party's
         record -- and no extra account read is taken: every name here already
         has its handle open for the payout above.  */
      if (creditRep && isWorker)
        BumpDealStats (acc, earnedReward);
      if (creditRep && isPoster)
        BumpPosterDealStats (acc, earnedReward);
      if (disputed && (isWorker || isPoster))
        BumpDisputedStats (acc);
      if (ruledByArbiter && isArbiter)
        BumpArbiterRulingStats (acc, pot);
    }
}

void
RefundBothDeal (const JobContext& jc, Job& job)
{
  /* Accumulated per name so worker == poster could never double-open a row
     (unreachable today -- accept rejects the poster -- but structural, like
     SettleDeal's credit map).  Hook-path only: no account handle is live.  */
  std::map<std::string, Amount> credit;
  credit[job.GetWorker ()] += job.GetCollateral ();
  credit[job.GetPoster ()] += job.GetReward ();
  for (const auto& entry : credit)
    if (entry.second > 0)
      {
        auto held = GetAccountChecked (jc, entry.first);
        ReleaseJobCoins (*held, entry.second);
      }
}

void
ExpireJob (const JobContext& jc, Job& job)
{
  if (job.GetStatus () != Job::Status::ACCEPTED)
    {
      /* Never accepted: refund the poster's reward (nothing else locked).  */
      VoidJobAtHook (jc, job);
      return;
    }

  const auto& d = job.GetProto ();
  if (d.disputed ())
    {
      /* Ghosted arbiter (or a no-arbiter dispute): the blunt 50/50 fallback.
         The arbiter FORFEITS its fee here -- it was hired precisely to rule
         this dispute and did not, and paying it anyway would make ghosting
         strictly better than the ruling (which costs a transaction).  */
      SettleDeal (jc, job, 50, nullptr, false, DealSettleMode::GHOST_SPLIT);
      return;
    }

  if (d.poster_confirmed () || d.worker_confirmed ())
    {
      /* One side confirmed, the other neither confirmed nor disputed: done.  */
      SettleDeal (jc, job, 100, nullptr, true,
                  DealSettleMode::SINGLE_CONFIRM);
      return;
    }

  /* Neither party acted: return both stakes.  */
  RefundBothDeal (jc, job);
}

/* ************************************************************************** */
/* The move-op lifecycle.                                                     */

namespace
{

/**
 * POST: locks the reward + burns the posting fee and creates an OPEN deal.
 */
class PostOperation : public JobOperation
{

private:

  /** The relative deadline in seconds; -1 = absent (rejected at validation).  */
  const int64_t deadlineSecs;

  const Amount reward;
  const Amount collateral;

  /** The full raw post object, carrying the deal terms.  */
  const Json::Value terms;

  /** Computes the burned posting fee for the reward.  */
  Amount
  Fee () const
  {
    const auto& p = jc.ctx.RoConfig ()->params ();
    const Amount proportional = reward * p.job_post_fee_bps () / 10000;
    return std::max<Amount> (p.job_post_fee_min (), proportional);
  }

public:

  PostOperation (Account& a, const JobContext& c, const int64_t d,
                 const Amount rew, const Amount col, const Json::Value& tm)
    : JobOperation(a, c), deadlineSecs(d),
      reward(rew), collateral(col), terms(tm)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
PostOperation::IsValid () const
{
  if (account.GetFaction () == Faction::INVALID)
    {
      LOG (WARNING)
          << account.GetName () << " has no faction, cannot post a job";
      return false;
    }

  /* The reward must be strictly positive: a zero-reward job would still bump
     the worker's completion counter on settlement, farming the count-based
     half of the reputation for just the posting fee.  Collateral MAY be
     zero.  */
  if (reward <= 0 || collateral < 0)
    return false;

  /* A deal is deadline-bound, so "d" is required.  */
  if (deadlineSecs < 0)
    {
      LOG (WARNING) << "Job post is missing its deadline";
      return false;
    }
  /* A listing may sit on the board up to the booking horizon; the deadline's
     own lower bound is left to each type.  */
  const auto& p = jc.ctx.RoConfig ()->params ();
  if (deadlineSecs > p.max_listing_window ())
    {
      LOG (WARNING) << "Job listing window too long: " << deadlineSecs;
      return false;
    }

  if (!ValidateDealPost (jc, account, terms))
    return false;

  /* Admission caps: the deterministic ceiling on every future atomic
     sweep, enforced at the door so settlement semantics never change for
     rows already admitted.  The effective values are the admin-tunable
     parameter overrides (the "param" command) falling back to the roconfig
     defaults; 0 -- or any negative value -- freezes the respective
     admission entirely.  Checked after ValidatePost so the terms are
     known-valid.  */
  {
    /* Minimum escrowed value: the global floor, raised further by the deal
       floor, so a capped board slot cannot be occupied for pocket change.  */
    const Amount minReward
        = std::max<Amount> (jc.params.Get ("min-job-reward",
                                           p.min_job_reward ()),
                            MinDealReward (jc));
    if (reward < minReward)
      {
        LOG (WARNING)
            << "Reward " << reward << " is below the minimum " << minReward;
        return false;
      }

    if (jc.jobs.CountAll ()
          >= CappedParam (jc.params, "max-live-jobs", p.max_live_jobs (),
                          CAP_MAX_LIVE_JOBS))
      {
        LOG (WARNING) << "Jobs board is at the live-jobs cap";
        return false;
      }

    if (jc.jobs.CountForPoster (account.GetName ())
          >= CappedParam (jc.params, "max-jobs-per-poster",
                          p.max_jobs_per_poster (), CAP_MAX_JOBS_PER_POSTER))
      {
        LOG (WARNING)
            << account.GetName () << " is at their live-jobs cap";
        return false;
      }
  }

  const Amount fee = Fee ();
  if (account.GetBalance () < reward + fee)
    {
      LOG (WARNING)
          << account.GetName () << " cannot afford reward " << reward
          << " + fee " << fee << " (balance " << account.GetBalance () << ")";
      return false;
    }

  return true;
}

void
PostOperation::Execute ()
{
  const Amount fee = Fee ();

  LOG (INFO)
      << account.GetName () << " posting a job (reward " << reward
      << ", collateral " << collateral << ", fee " << fee << ")";

  /* Escrow the reward (tracked in the job row, released on settlement) and
     burn the posting fee (removed from circulation, never credited).  */
  LockJobCoins (account, reward);
  account.AddBalance (-fee);

  auto job = jc.jobs.CreateNew (account.GetName (), reward, collateral,
                                jc.ctx.Timestamp () + deadlineSecs);
  ApplyDealPost (jc, terms, *job);
}

/* ************************************************************************** */

/**
 * ASSIGN: the poster names (or changes) the designated worker before anyone
 * has accepted.
 */
class AssignOperation : public JobOperation
{

private:

  const Database::IdT jobId;
  const std::string designated;

public:

  AssignOperation (Account& a, const JobContext& c, const Database::IdT id,
                   const std::string& w)
    : JobOperation(a, c), jobId(id), designated(w)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
AssignOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (jobId);
  if (job == nullptr || job->GetStatus () != Job::Status::OPEN)
    {
      LOG (WARNING) << "Job " << jobId << " not open to assign";
      return false;
    }
  if (job->GetPoster () != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " does not own job " << jobId;
      return false;
    }
  /* Assignment designates an exclusive worker before anyone accepts: on the
     one assignable type -- the generic deal -- only the designated worker may
     accept from here on (the generic accept gate enforces the pin).  It does
     NOT set invite_only: that flag is the POST-time born-private
     discriminator, and a publicly posted deal stays invite_only=false once
     assigned (proto JobData.invite_only -- exclusive is the pair
     `invite_only || designated_worker != ""`, not the bit alone).  */
  /* An expired listing is the sweep's to void (see JobIsDue); a designation
     landing in the gap would be dead data on a job that never runs.  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Job " << jobId << " is at or past its deadline; cannot assign";
      return false;
    }
  if (designated == account.GetName ())
    {
      LOG (WARNING) << "Cannot designate oneself as worker for job " << jobId;
      return false;
    }
  /* The designee must not be the deal's bound arbiter: an arbiter-worker
     would judge its own dispute.  The accept gate already bars it, so
     assigning it could only strand the row until expiry (F7).  */
  const std::string& arbiter = job->GetProto ().arbiter ();
  if (!arbiter.empty () && designated == arbiter)
    {
      LOG (WARNING)
          << "Cannot designate the bound arbiter as worker for job " << jobId;
      return false;
    }

  const auto w = jc.accounts.GetByName (designated);
  if (w == nullptr || !w->IsInitialised ())
    {
      LOG (WARNING) << "Designated worker " << designated << " does not exist";
      return false;
    }

  return true;
}

void
AssignOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;
  LOG (INFO)
      << account.GetName () << " assigning job " << jobId << " to "
      << designated;
  job->MutableProto ().set_designated_worker (designated);
}

/* ************************************************************************** */

/**
 * ACCEPT: the worker locks their collateral and binds the job to them.  The
 * designated-worker rules are enforced here, plus the deal's own
 * arbiter-cannot-accept check (ValidateDealAccept).
 */
class AcceptOperation : public JobOperation
{

private:

  const Database::IdT jobId;

public:

  AcceptOperation (Account& a, const JobContext& c, const Database::IdT id)
    : JobOperation(a, c), jobId(id)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
AcceptOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (jobId);
  if (job == nullptr || job->GetStatus () != Job::Status::OPEN)
    {
      LOG (WARNING) << "Job " << jobId << " not open to accept";
      return false;
    }

  /* An expired listing is the sweep's to void (see JobIsDue).  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Job " << jobId << " is at or past its deadline; cannot accept";
      return false;
    }

  if (job->GetPoster () == account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " cannot accept own job " << jobId;
      return false;
    }

  /* Designated-worker gate.  An invite-only deal with no designee yet (posted
     with w:"") is acceptable by NOBODY -- the poster invites later via ASSIGN.
     An empty designated_worker is otherwise "open to all", so the flag is what
     disambiguates the private-unassigned state (design §3).  */
  const std::string& designated = job->GetProto ().designated_worker ();
  if (job->GetProto ().invite_only () && designated.empty ())
    {
      LOG (WARNING)
          << "Job " << jobId << " is invite-only with no designee yet";
      return false;
    }
  if (!designated.empty () && designated != account.GetName ())
    {
      LOG (WARNING)
          << "Job " << jobId << " is designated to " << designated
          << ", not " << account.GetName ();
      return false;
    }

  if (!ValidateDealAccept (*job, account))
    return false;

  if (account.GetBalance () < job->GetCollateral ())
    {
      LOG (WARNING)
          << account.GetName () << " cannot afford collateral "
          << job->GetCollateral () << " for job " << jobId;
      return false;
    }

  return true;
}

void
AcceptOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;

  LOG (INFO)
      << account.GetName () << " accepting job " << jobId
      << ", locking collateral " << job->GetCollateral ();

  LockJobCoins (account, job->GetCollateral ());
  job->SetWorker (account.GetName ());
  job->SetStatus (Job::Status::ACCEPTED);
}

/* ************************************************************************** */

/**
 * CANCEL: the poster withdraws an OPEN job before anyone accepts, and the
 * reward is refunded (the posting fee is not).
 */
class CancelOperation : public JobOperation
{

private:

  const Database::IdT jobId;

public:

  CancelOperation (Account& a, const JobContext& c, const Database::IdT id)
    : JobOperation(a, c), jobId(id)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
CancelOperation::IsValid () const
{
  const auto job = jc.jobs.GetById (jobId);
  if (job == nullptr || job->GetStatus () != Job::Status::OPEN)
    {
      LOG (WARNING) << "Job " << jobId << " not open to cancel";
      return false;
    }
  if (job->GetPoster () != account.GetName ())
    {
      LOG (WARNING)
          << account.GetName () << " does not own job " << jobId
          << " to cancel";
      return false;
    }

  /* An expired listing is the sweep's to void (see JobIsDue); a cancel in
     the gap would record CANCELLED history for a job that expired.  The
     refund is the same either way.  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Job " << jobId << " is at or past its deadline; cannot cancel";
      return false;
    }

  return true;
}

void
CancelOperation::Execute ()
{
  auto job = jc.jobs.GetById (jobId);
  CHECK (job != nullptr) << "Job disappeared: " << jobId;

  const Amount reward = job->GetReward ();

  LOG (INFO)
      << account.GetName () << " cancelling job " << jobId
      << ", refunding reward " << reward;

  ReleaseJobCoins (account, reward);
  DeleteSettled (jc.jobs, std::move (job));
}

/* ************************************************************************** */

/**
 * DEAL: the escrow-deal-specific actions beyond the generic post/accept --
 * confirm (happy path), dispute, and the bound arbiter's ruling.  One "dl"
 * object per op, exactly two members:
 *   {"dl":<id>,"confirm":true}   either party marks the deal done
 *   {"dl":<id>,"dispute":true}   either party contests it
 *   {"dl":<id>,"rule":<p>}       the bound arbiter rules p in {0,10,...,100}
 */
class DealOperation : public JobOperation
{

public:

  enum class Kind { CONFIRM, DISPUTE, RULE };

private:

  const Database::IdT id;
  const Kind kind;
  /** The ruling percentage, only for RULE (range-checked at parse).  */
  const int rulP;

public:

  DealOperation (Account& a, const JobContext& c, const Database::IdT i,
                 const Kind k, const int p)
    : JobOperation(a, c), id(i), kind(k), rulP(p)
  {}

  bool IsValid () const override;
  void Execute () override;

};

bool
DealOperation::IsValid () const
{
  auto job = jc.jobs.GetById (id);
  if (job == nullptr)
    return false;
  /* Every deal action needs the escrow locked (both stakes in).  */
  if (job->GetStatus () != Job::Status::ACCEPTED)
    {
      LOG (WARNING) << "Deal op on a non-accepted deal " << id;
      return false;
    }

  /* An elapsed deal is the sweep's to settle (see JobIsDue), exactly like
     every other lifecycle op: a confirm, dispute or ruling landing at or past
     the end date would still change the settlement of a deal whose outcome is
     already fixed by the state at its (possibly extended) deadline.  The end
     date stays a HARD boundary for ops.  v1.1 semantics (design §1): a confirm
     (any deal, leaving a live counter-move) or a dispute (arbiter-bound only)
     landing strictly within the row's reaction_window of the deadline is NOT
     terminal -- DealOperation::Execute extends the deadline to now+window, so
     the successor move keeps a full window; the extension is one-shot per
     set-once flag, never shortens, and totals at most 2W.  A no-arbiter dispute
     is terminal BY DESIGN (its only successor is the sweep's Option-B 50/50 at
     the unmoved deadline, so an extension would be pure settlement delay).  */
  if (JobIsDue (*job, jc))
    {
      LOG (WARNING)
          << "Deal " << id << " is at or past its end date; only the sweep"
          << " settles it now";
      return false;
    }

  const auto& d = job->GetProto ();
  const std::string me = account.GetName ();
  const bool isParty = (me == job->GetPoster () || me == job->GetWorker ());

  switch (kind)
    {
    case Kind::CONFIRM:
      if (!isParty)
        return false;
      /* Once disputed, only the arbiter's ruling settles the deal (or the
         sweep's 50/50 fallback) -- the documented DealPayload invariant.
         A both-confirm racing the ruling would bypass the arbiter.  */
      if (d.disputed ())
        return false;
      /* No double-confirm.  */
      if (me == job->GetPoster () && d.poster_confirmed ())
        return false;
      if (me == job->GetWorker () && d.worker_confirmed ())
        return false;
      return true;

    case Kind::DISPUTE:
      if (!isParty)
        return false;
      if (d.disputed ())
        return false;
      /* A confirmation is irrevocable and waives only the confirmer's OWN
         dispute right (design §6.2): the counterparty may still dispute a
         shoddy job.  Without this guard a confirmed all-clear could be
         revoked -- downgrading the one-confirm p=100 timeout into the
         disputed 50/50 fallback, or (with a poster-owned arbiter) reopening
         a ruling path after the worker relied on the confirmation.  */
      if (me == job->GetPoster () && d.poster_confirmed ())
        return false;
      if (me == job->GetWorker () && d.worker_confirmed ())
        return false;
      return true;

    case Kind::RULE:
      /* Only the bound arbiter may rule, and only a raised dispute.  */
      if (d.arbiter ().empty () || me != d.arbiter ())
        return false;
      if (!d.disputed ())
        return false;
      return true;
    }

  return false;
}

void
DealOperation::Execute ()
{
  auto job = jc.jobs.GetById (id);
  CHECK (job != nullptr) << "Deal disappeared: " << id;
  auto& d = job->MutableProto ();

  switch (kind)
    {
    case Kind::CONFIRM:
      if (account.GetName () == job->GetPoster ())
        d.set_poster_confirmed (true);
      else
        d.set_worker_confirmed (true);
      if (d.poster_confirmed () && d.worker_confirmed ())
        {
          /* Both sides agree it is done: release at p=100.  Nothing is left to
             answer, so no reaction-window extension applies.  */
          SettleDeal (jc, *job, 100, &account, true,
                      DealSettleMode::BOTH_CONFIRM);
          DeleteSettled (jc.jobs, std::move (job));
          return;
        }
      /* A single confirm persists (flushed when the handle destructs); it
         leaves the counterparty a live confirm-settle or dispute, so a confirm
         landing in the final window extends the deadline (§1).  */
      ExtendForReactionWindow (*job, jc.ctx.Timestamp ());
      return;

    case Kind::DISPUTE:
      d.set_disputed (true);
      /* Stamp the dispute time (§2) unconditionally.  An arbiter-bound dispute
         leaves the arbiter a live ruling move, so a dispute in the final window
         extends the deadline to guarantee >= W ruling time (§1); a no-arbiter
         dispute has no successor but the sweep's 50/50, so it never extends. */
      d.set_dispute_time (jc.ctx.Timestamp ());
      if (!d.arbiter ().empty ())
        ExtendForReactionWindow (*job, jc.ctx.Timestamp ());
      return;

    case Kind::RULE:
      {
        SettleDeal (jc, *job, rulP, &account, true, DealSettleMode::RULING);
        DeleteSettled (jc.jobs, std::move (job));
      }
      return;
    }
}

/* ************************************************************************** */

} // anonymous namespace

std::unique_ptr<JobOperation>
JobOperation::Parse (Account& acc, const Json::Value& data,
                     const JobContext& jc)
{
  if (!data.isObject ())
    return nullptr;

  /* Exactly one of the five discriminator keys must be present.  */
  const bool hasT = data.isMember ("t");
  const bool hasS = data.isMember ("s");
  const bool hasA = data.isMember ("a");
  const bool hasC = data.isMember ("c");
  const bool hasDl = data.isMember ("dl");
  if (hasT + hasS + hasA + hasC + hasDl != 1)
    return nullptr;

  std::unique_ptr<JobOperation> op;

  if (hasT)
    {
      /* POST: {"t":"deal","d":<secs>,"r":<reward>,"co":<collateral>,...}.
         The listing deadline "d" is required (checked in IsValid).  The "t"
         value names the operation rather than selecting among kinds: the
         board carries escrow deals only, so anything else is rejected.  */
      if (!data["t"].isString () || data["t"].asString () != "deal")
        return nullptr;

      /* The POST grammar is exactly as strict as the lifecycle ops' below:
         beyond the generic keys, only the deal term keys are allowed, so a
         typo'd or unknown member rejects the move instead of being silently
         ignored.  */
      static const std::set<std::string> generic = {"t", "d", "r", "co"};
      const auto& typeKeys = DealPostTermKeys ();
      for (const auto& member : data.getMemberNames ())
        if (generic.count (member) == 0
              && std::find (typeKeys.begin (), typeKeys.end (), member)
                   == typeKeys.end ())
          {
            LOG (WARNING) << "Unknown key \"" << member << "\" in job post";
            return nullptr;
          }

      int64_t deadlineSecs = -1;
      if (data.isMember ("d"))
        {
          if (!data["d"].isInt64 () || !xaya::IsIntegerValue (data["d"]))
            return nullptr;
          deadlineSecs = data["d"].asInt64 ();
          if (deadlineSecs < 0)
            return nullptr;
        }
      Amount reward, collateral;
      if (!CoinAmountFromJson (data["r"], reward)
            || !CoinAmountFromJson (data["co"], collateral))
        return nullptr;
      op = std::make_unique<PostOperation> (acc, jc, deadlineSecs,
                                            reward, collateral, data);
    }
  else if (hasS)
    {
      /* ASSIGN: {"s":<id>,"w":<account>} -- exactly those two members.  */
      if (data.size () != 2)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["s"], id) || !data["w"].isString ())
        return nullptr;
      op = std::make_unique<AssignOperation> (acc, jc, id,
                                              data["w"].asString ());
    }
  else if (hasA)
    {
      /* ACCEPT: {"a":<id>}.  */
      if (data.size () != 1)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["a"], id))
        return nullptr;
      op = std::make_unique<AcceptOperation> (acc, jc, id);
    }
  else if (hasC)
    {
      /* CANCEL: {"c":<id>}.  */
      if (data.size () != 1)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["c"], id))
        return nullptr;
      op = std::make_unique<CancelOperation> (acc, jc, id);
    }
  else
    {
      /* DEAL action: {"dl":<id>, <one of confirm/dispute/rule>} -- exactly two
         members, exactly one action.  */
      CHECK (hasDl);
      if (data.size () != 2)
        return nullptr;
      Database::IdT id;
      if (!IdFromJson (data["dl"], id))
        return nullptr;
      const bool hasConfirm = data.isMember ("confirm");
      const bool hasDispute = data.isMember ("dispute");
      const bool hasRule = data.isMember ("rule");
      if (hasConfirm + hasDispute + hasRule != 1)
        return nullptr;
      if (hasConfirm)
        {
          if (!data["confirm"].isBool () || !data["confirm"].asBool ())
            return nullptr;
          op = std::make_unique<DealOperation> (
              acc, jc, id, DealOperation::Kind::CONFIRM, 0);
        }
      else if (hasDispute)
        {
          if (!data["dispute"].isBool () || !data["dispute"].asBool ())
            return nullptr;
          op = std::make_unique<DealOperation> (
              acc, jc, id, DealOperation::Kind::DISPUTE, 0);
        }
      else
        {
          if (!data["rule"].isInt64 () || !xaya::IsIntegerValue (data["rule"]))
            return nullptr;
          const int64_t p = data["rule"].asInt64 ();
          if (p < 0 || p > 100 || p % 10 != 0)
            return nullptr;
          op = std::make_unique<DealOperation> (
              acc, jc, id, DealOperation::Kind::RULE, static_cast<int> (p));
        }
    }

  return op;
}

/* ************************************************************************** */
/* Per-block hooks (confirmed processing only).                               */

namespace
{

/**
 * Locally-constructed table handles for a block hook plus the JobContext they
 * form.  The hooks are standalone (not nested inside the move processor's
 * handles), so they own their table handles for the duration of the call.
 */
struct BlockHookTables
{
  AccountsTable accounts;
  JobsTable jobs;
  ParamsTable params;

  explicit BlockHookTables (Database& d)
    : accounts(d), jobs(d), params(d)
  {}

  JobContext
  MakeContext (const pxd::Context& ctx)
  {
    return {ctx, accounts, jobs, params};
  }
};

} // anonymous namespace

void
ExpireJobs (Database& db, const Context& ctx)
{
  JobsTable jobs(db);

  /* Snapshot the due jobs (fully consuming the query) before mutating any
     balances or deleting rows.  On idle blocks this is an indexed no-op.  */
  std::vector<Database::IdT> due;
  {
    auto res = jobs.QueryForDeadline (ctx.Timestamp ());
    while (res.Step ())
      {
        auto j = jobs.GetFromResult (res);
        CHECK_LE (j->GetDeadline (), ctx.Timestamp ())
            << "Job " << j->GetId () << " with a future deadline in expiry";
        due.push_back (j->GetId ());
      }
  }

  if (due.empty ())
    return;

  BlockHookTables tables(db);
  const JobContext jc = tables.MakeContext (ctx);
  for (const auto id : due)
    {
      auto j = tables.jobs.GetById (id);
      CHECK (j != nullptr);
      ExpireJob (jc, *j);
      DeleteSettled (tables.jobs, std::move (j));
    }
}

/* ************************************************************************** */

void
ValidateJobs (Database& db)
{
  AccountsTable accounts(db);
  JobsTable jobs(db);

  auto res = jobs.QueryAll ();
  while (res.Step ())
    {
      auto j = jobs.GetFromResult (res);
      const auto id = j->GetId ();

      {
        const auto poster = accounts.GetByName (j->GetPoster ());
        CHECK (poster != nullptr && poster->IsInitialised ())
            << "Job " << id << " has an invalid poster";
      }

      switch (j->GetStatus ())
        {
        case Job::Status::OPEN:
          CHECK (j->GetWorker ().empty ())
              << "Open job " << id << " has a worker";
          break;
        case Job::Status::ACCEPTED:
          {
            CHECK (!j->GetWorker ().empty ())
                << "Accepted job " << id << " has no worker";
            /* Every worker-setting path guarantees an initialised account
               (the move-processor's init gate, the assign check), so the
               validator pins the full invariant -- same as the poster's.  */
            const auto worker = accounts.GetByName (j->GetWorker ());
            CHECK (worker != nullptr && worker->IsInitialised ())
                << "Job " << id << " has an invalid worker";
          }
          break;
        default:
          LOG (FATAL) << "Job " << id << " has an invalid status";
        }
    }
}

} // namespace pxd
