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

#include <xayautil/jsonutils.hpp>

#include <glog/logging.h>

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace pxd
{

/* ************************************************************************** */
/* Shared settlement helpers.                                                 */

namespace
{

/** Upper bound on the length of an ad content hash (hex string).  */
constexpr size_t MAX_AD_HASH_LENGTH = 200;

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
 * Bumps the per-account completion counters: consensus-stored vetting
 * signals that no consensus rule ever reads back -- clients surface them
 * so posters can vet applicants and workers can vet posters.
 *
 * The increments are deliberately unchecked at the actual field widths.
 * The counts are uint32, so wrapping one takes 2^32 (~4.3e9) settlements,
 * each burning a posting fee; the value counter is uint64 and grows by at
 * most MAX_COIN_AMOUNT (1e11) per settlement, so wrapping it takes ~1.8e8
 * max-value jobs, each escrowing that value and burning its fee.  Both are
 * economically unreachable, and a wrap would in any case be deterministic
 * across nodes (no split risk).  Saturation is deliberately NOT used: it
 * would add consensus-visible code for an unreachable boundary.  Raw
 * counts are a display-only signal that colluding pairs can inflate for
 * the fee price; the value-weighted counter is the stronger, fee-backed
 * face-value signal -- a colluding pair can still buy credit on it, but
 * only at the posting-fee burn (~1% of the face value credited), so it
 * anchors to burned cost rather than proving arm's-length work.  Any
 * future consumer of these stats must weigh by value (or add thresholds)
 * rather than trust counts.
 *
 * `times` folds several settlements into one call (the aggregated
 * wanted-pool payout, where one kill completes `times` pools at once);
 * `value` is always the TOTAL across them.
 */
void
BumpJobStats (Account& a, const Amount value, const unsigned times = 1)
{
  auto& pb = a.MutableProto ();
  pb.set_jobs_completed (pb.jobs_completed () + times);
  pb.set_jobs_value_completed (pb.jobs_value_completed () + value);
}

/**
 * Bumps the DEAL reputation counters (kept disjoint from the jobs/wanted
 * counters above -- red-team T7).  Called once for the worker on a settled
 * deal with p>0 and a real tax burned; `value` is the worker's earned reward.
 */
void
BumpDealStats (Account& a, const Amount value)
{
  auto& pb = a.MutableProto ();
  pb.set_deals_completed (pb.deals_completed () + 1);
  pb.set_deals_value_completed (pb.deals_value_completed () + value);
}

/**
 * Validates that a job's terms carry zero worker collateral (the open-claim
 * and payer/payee-swapped types, where nobody posts a bond).  The generic
 * parse has already verified "co" is a well-formed amount.
 */
bool
RequireZeroCollateral (const Json::Value& terms, const char* what)
{
  Amount collateral;
  CHECK (CoinAmountFromJson (terms["co"], collateral));
  if (collateral != 0)
    {
      LOG (WARNING) << what << " takes no collateral";
      return false;
    }
  return true;
}

/**
 * Pays a worker for a successful job: their locked collateral plus the reward
 * (fee-free), and bumps their completion counters.  The account handle is
 * passed in -- the hook's fetched worker already holds it.
 */
void
PayWorkerSuccess (Account& worker, const Job& job)
{
  ReleaseJobCoins (worker, job.GetReward () + job.GetCollateral ());
  BumpJobStats (worker, job.GetReward ());
}

/**
 * Hook-path settlement: the job succeeded (success-on-expiry types).  Pays
 * the worker the reward plus their collateral back and bumps their counters.
 * Must not be called while any account handle is live.
 */
void
SettleSuccessAtHook (const JobContext& jc, const Job& job)
{
  auto worker = GetAccountChecked (jc, job.GetWorker ());
  PayWorkerSuccess (*worker, job);
}

/**
 * Hook-path settlement: the job is void through neither party's fault (an
 * OPEN job expired, or a linked entity died on a no-fault type).  The reward
 * refunds to the poster and any locked collateral returns to the worker; no
 * counters change.  Must not be called while any account handle is live.
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

/* ************************************************************************** */
/* Wanted bounties (the kill-hook settled type).                              */

/**
 * Wanted-bounty: the standing, open-claim kill contract on an account name
 * (the linked_name).  The escrowed reward is a pool of `quota` equal
 * tranches of reward/quota, consumed one per qualifying kill of the
 * target's characters at the kill hook, split equally across the distinct
 * accounts on the victim's damage list; the pool completes when drained.
 * No accept step, no collateral -- settlement happens entirely at the kill
 * hook, and the only exit is the notice-based cancel.
 */
class WantedPredicate : public JobPredicate
{

public:

  bool
  SettlesOnTargetKill () const override
  {
    return true;
  }

  bool
  IsStanding () const override
  {
    return true;
  }

  Faction
  AudienceFaction (const Account& poster) const override
  {
    /* Visible to everyone (INVALID = no audience gate): a kill contract is not
       scoped to the poster's faction.  Legitimate collectors are the target's
       enemies -- a same-faction unit can only land damage via mentecon
       friendly-fire, and the jobs kill hook filters those owners out of the
       payout set -- so eligibility is bounded by the TARGET's faction, never
       the poster's.  */
    return Faction::INVALID;
  }

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys = {"name", "n"};
    return keys;
  }

  std::string
  PostLinkedName (const Json::Value& terms) const override
  {
    return terms["name"].asString ();
  }

  Amount
  MinReward (const JobContext& jc) const override
  {
    /* A kill contract occupies one of the target's capped listing slots, so
       it must lock real value: the runtime-tunable bounty floor.  */
    return jc.params.Get ("min-bounty-reward",
                          jc.ctx.RoConfig ()->params ().min_bounty_reward ());
  }

  bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const override
  {
    /* Open-claim: there is no accept step.  */
    return false;
  }

  bool ValidatePost (const JobContext& jc, const Account& poster,
                     const Json::Value& terms) const override;
  void ApplyPost (const JobContext& jc, Account& poster,
                  const Json::Value& terms, Job& job) const override;

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    /* Only reachable when a notice-cancel converted the standing pool into
       an expiring one and the notice ran out: refund whatever is unearned.  */
    CHECK (job.GetStatus () == Job::Status::OPEN);
    LOG (INFO)
        << "Wanted board " << job.GetId () << " closed; refunding unearned "
        << job.GetReward () << " to " << job.GetPoster ();
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  JobOutcome OnTargetKill (const JobContext& jc, Job& job,
                           const std::set<std::string>& killOwners,
                           Amount& sharePerOwner) const override;

};

bool
WantedPredicate::ValidatePost (const JobContext& jc,
                               const Account& poster,
                               const Json::Value& terms) const
{
  if (!terms["name"].isString ())
    return false;
  const std::string target = terms["name"].asString ();
  if (target == poster.GetName ())
    {
      LOG (WARNING)
          << poster.GetName () << " cannot post a kill contract on itself";
      return false;
    }
  {
    const auto t = jc.accounts.GetByName (target);
    if (t == nullptr || !t->IsInitialised ())
      {
        LOG (WARNING) << "Kill-contract target does not exist: " << target;
        return false;
      }
  }

  if (!terms["n"].isUInt () || !xaya::IsIntegerValue (terms["n"]))
    return false;
  const unsigned quota = terms["n"].asUInt ();
  const unsigned maxQuota = jc.ctx.RoConfig ()->params ().max_bounty_quota ();
  if (quota < 1 || quota > maxQuota)
    {
      LOG (WARNING) << "Kill quota out of range: " << quota;
      return false;
    }

  if (!RequireZeroCollateral (terms, "A kill contract"))
    return false;

  Amount reward;
  CHECK (CoinAmountFromJson (terms["r"], reward));
  if (reward / quota < 1)
    {
      LOG (WARNING)
          << "Reward " << reward << " is too small for quota " << quota;
      return false;
    }

  return true;
}

void
WantedPredicate::ApplyPost (const JobContext& jc, Account& poster,
                            const Json::Value& terms, Job& job) const
{
  job.SetLinkedName (PostLinkedName (terms));

  Amount reward;
  CHECK (CoinAmountFromJson (terms["r"], reward));
  const unsigned quota = terms["n"].asUInt ();
  auto* wp = job.MutableProto ().mutable_wanted ();
  wp->set_quota (quota);
  wp->set_remaining (quota);
  wp->set_tranche (reward / quota);
}

JobOutcome
WantedPredicate::OnTargetKill (const JobContext& jc, Job& job,
                               const std::set<std::string>& killOwners,
                               Amount& sharePerOwner) const
{
  CHECK (!killOwners.empty ());
  auto* wp = job.MutableProto ().mutable_wanted ();
  CHECK_GT (wp->remaining (), 0);

  const Amount tranche = wp->tranche ();
  CHECK_GE (job.GetReward (), tranche);

  /* One tranche leaves the escrow: split equally across the distinct killer
     accounts, with any division remainder burned (never redistributed).
     When the tranche cannot give every distinct killer at least one coin,
     nobody is paid -- crediting a zero-value completion would inflate the
     reputation counters for free.  The tranche is still consumed (burned as
     the remainder, which is never redistributed).  The caller accumulates
     the share across all pools and pays each owner once (PayKillShares).  */
  sharePerOwner = tranche / killOwners.size ();

  job.SetReward (job.GetReward () - tranche);
  wp->set_remaining (wp->remaining () - 1);

  LOG (INFO)
      << "Wanted board " << job.GetId () << " paid a tranche of " << tranche
      << " split across " << killOwners.size () << " hunter(s); "
      << wp->remaining () << " kill(s) remaining";

  if (wp->remaining () > 0)
    return JobOutcome::INVALID;

  /* Pool drained: whatever escrow is left is the division dust, burned by
     never crediting it anywhere.  */
  if (job.GetReward () > 0)
    LOG (INFO)
        << "Wanted board " << job.GetId () << " complete; burning dust "
        << job.GetReward ();
  return JobOutcome::DRAINED;
}


/* ************************************************************************** */
/* Ad-slot rentals.                                                           */

/**
 * Ad-slot rental: the poster (advertiser) escrows the rent on a building's
 * ad slot; the designated worker is the building's owner, whose accept IS
 * the content approval (committed by hash).  The rent pays out at the
 * first expiry sweep after the deadline (success-on-expiry); the entity hook
 * refunds it if the building dies first, and a sale of the building any time
 * before that sweep voids and refunds it too.  After accept, rendering is
 * unconditional -- the owner has no take-down lever for the paid period, and
 * clients render only hash-matching content.
 */
class AdPredicate : public JobPredicate
{

public:

  bool
  RequiresApproval () const override
  {
    return true;
  }

  JobLinkedKind
  LinkedEntityKind () const override
  {
    return JobLinkedKind::BUILDING;
  }

  Faction
  AudienceFaction (const Account& poster) const override
  {
    return Faction::INVALID;
  }

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys
        = {"b", "slot", "hash", "start"};
    return keys;
  }

  const std::vector<const char*>&
  PostLinkedIdKeys () const override
  {
    static const std::vector<const char*> keys = {"b"};
    return keys;
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
  {
    if (!RequireZeroCollateral (terms, "An ad-slot rental"))
      return false;

    if (!terms["slot"].isUInt () || !xaya::IsIntegerValue (terms["slot"]))
      return false;
    if (!terms["hash"].isString ())
      return false;
    const std::string hash = terms["hash"].asString ();
    if (hash.empty () || hash.size () > MAX_AD_HASH_LENGTH)
      return false;

    /* Optional scheduling: the window opens `start` seconds after post
       (0 / absent = immediately) and closes at the deadline.  The rented
       window itself must satisfy the min-ad-window floor, which forces
       it to be non-empty; the deadline cap already bounds how far ahead a
       slot can be booked.  An explicit null "start" is rejected like every
       other malformed term (strict grammar): absent means absent.  */
    int64_t startSecs = 0;
    if (terms.isMember ("start"))
      {
        if (!terms["start"].isInt64 () || !xaya::IsIntegerValue (terms["start"]))
          return false;
        startSecs = terms["start"].asInt64 ();
        if (startSecs < 0)
          return false;
      }
    if (!terms["d"].isInt64 ()
          || terms["d"].asInt64 () - startSecs
              < jc.ctx.RoConfig ()->params ().min_ad_window ())
      {
        LOG (WARNING) << "Ad window is too short for start " << startSecs;
        return false;
      }

    Database::IdT bId;
    if (!IdFromJson (terms["b"], bId))
      return false;
    const auto b = jc.buildings.GetById (bId);
    if (b == nullptr || b->GetProto ().foundation ()
          || b->GetFaction () == Faction::ANCIENT)
      {
        LOG (WARNING) << "Invalid ad-slot building " << bId;
        return false;
      }
    if (b->GetOwner () == poster.GetName ())
      {
        LOG (WARNING) << "Cannot rent an ad slot on one's own building";
        return false;
      }

    return true;
  }

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    Database::IdT bId;
    CHECK (IdFromJson (terms["b"], bId));
    job.SetLinkedId (bId);

    auto* ap = job.MutableProto ().mutable_ad ();
    ap->set_slot (terms["slot"].asUInt ());
    ap->set_content_hash (terms["hash"].asString ());
    if (terms.isMember ("start") && terms["start"].asInt64 () > 0)
      ap->set_start (jc.ctx.Timestamp () + terms["start"].asInt64 ());

    /* The payee is the building's owner at post time; their accept is the
       approval.  ValidateAccept re-verifies ownership at accept time in case
       the building changed hands in between.  */
    const auto b = jc.buildings.GetById (bId);
    CHECK (b != nullptr);
    job.MutableProto ().set_designated_worker (b->GetOwner ());
  }

  bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const override
  {
    const auto b = jc.buildings.GetById (job.GetLinkedId ());
    if (b == nullptr || b->GetOwner () != worker.GetName ())
      {
        LOG (WARNING)
            << worker.GetName () << " no longer owns the ad-slot building of "
            << "job " << job.GetId ();
        return false;
      }

    /* Slot exclusivity: the accept is the booking, so it is rejected while
       another ACCEPTED ad on the same (building, slot) overlaps this ad's
       window.  Windows are half-open [start, deadline): back-to-back
       bookings may share an endpoint.  The candidate's window is clamped to
       now (an ad accepted after its start renders from the accept), which
       also skips competitors already due for this block's expiry sweep.
       An unset start means the window opened at post.  OPEN ads never block
       each other -- the owner's accept picks the winner.  */
    const auto& ap = job.GetProto ().ad ();
    const int64_t now = jc.ctx.Timestamp ();
    const int64_t candLo = std::max<int64_t> (ap.start (), now);
    const int64_t candHi = job.GetDeadline ();

    /* The accept is the booking and the payout is always the full rent, so
       the approval must leave at least the minimum window actually
       displayable -- otherwise a last-second approval would collect the
       whole rent for (nearly) no display time.  An unapproved ad simply
       voids at its deadline and the advertiser's escrow refunds.  Accepting
       before a scheduled start always passes: post validation floors the
       booked window itself.  */
    if (candHi - candLo < jc.ctx.RoConfig ()->params ().min_ad_window ())
      {
        LOG (WARNING)
            << "Ad job " << job.GetId ()
            << " has too little of its window left to accept";
        return false;
      }
    /* The rows are read straight off the result (never through GetFromResult):
       the caller already holds the handle of the job being accepted, which
       this query returns as well.  */
    auto res = jc.jobs.QueryForLinkedId (job.GetLinkedId ());
    while (res.Step ())
      {
        const auto otherId
            = static_cast<Database::IdT> (res.Get<JobResult::id> ());
        if (otherId == job.GetId ()
              || static_cast<Job::Type> (res.Get<JobResult::type> ())
                  != Job::Type::AD
              || static_cast<Job::Status> (res.Get<JobResult::status> ())
                  != Job::Status::ACCEPTED)
          continue;
        const auto otherData = res.GetProto<JobResult::proto> ();
        const auto& op = otherData.Get ().ad ();
        if (op.slot () != ap.slot ())
          continue;
        /* Ads are always deadlined, but never read a NULL deadline column as
           0 (it would silently defeat the overlap test); skip defensively.  */
        if (res.IsNull<JobResult::deadline> ())
          continue;
        if (candLo < res.Get<JobResult::deadline> () && op.start () < candHi)
          {
            LOG (WARNING)
                << "Ad job " << job.GetId () << " overlaps accepted ad "
                << otherId << " on building "
                << job.GetLinkedId () << " slot " << ap.slot ();
            return false;
          }
      }

    return true;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    if (job.GetStatus () == Job::Status::ACCEPTED)
      {
        /* The rented period elapsed: the rent pays the owner.  */
        LOG (INFO)
            << "Ad job " << job.GetId () << " completed its period; paying "
            << job.GetReward () << " to " << job.GetWorker ();
        SettleSuccessAtHook (jc, job);
        return JobOutcome::COMPLETED;
      }
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  JobOutcome
  OnLinkedEntityDestroyed (const JobContext& jc, Job& job) const override
  {
    /* The building died mid-period: the advertiser gets the rent back.  */
    LOG (INFO)
        << "Voided ad job " << job.GetId () << ": building "
        << job.GetLinkedId () << " destroyed";
    VoidJobAtHook (jc, job);
    return JobOutcome::VOID;
  }

  bool
  OnLinkedBuildingTransferred (const JobContext& jc, Job& job) const override
  {
    /* A sale is the destruction case for an ad: the new owner never approved
       the content and the old owner is no longer the payee, so the advertiser
       gets the rent back.  This voids OPEN ads too -- their designated worker
       is the old owner, who could never pass the accept-time ownership
       re-check anyway.

       Pinned simple rule: this also voids an ACCEPTED ad already past its
       deadline but not yet swept, refunding rent the sweep would have paid
       the (old) owner.  At arm's length only the owner can trigger that and
       the owner is the losing payee; a colluding advertiser/owner pair
       extracts nothing (their rent circulates inside the pair either way).
       A status/due special case here would buy nothing but complexity.  */
    LOG (INFO)
        << "Voided ad job " << job.GetId () << ": building "
        << job.GetLinkedId () << " sold";
    VoidJobAtHook (jc, job);
    return true;
  }

};

/* ************************************************************************** */
/* Generic escrow deal.                                                       */

/** Upper bound on the byte length of a deal's free-text terms.  */
constexpr size_t MAX_DEAL_TERMS_LENGTH = 1'000;
/** Upper bound on a deal's cosmetic type tag.  */
constexpr uint32_t MAX_DEAL_TYPE_TAG = 100;

/**
 * The effective deal tax in bps (runtime param over the roconfig default).
 * Read in ValidatePost (the §6.3 door guard) and snapshot by ApplyPost, so
 * both see the same value within one post.
 */
int64_t
DealTaxBps (const JobContext& jc)
{
  return jc.params.Get ("deal-tax-bps",
                        jc.ctx.RoConfig ()->params ().deal_tax_bps ());
}

/**
 * The one primitive that replaces the per-type verification jobs: an escrowed
 * reward + a worker collateral, settled by a completion percentage that a bound
 * arbiter dials on a dispute (ComputeDealSettlement / SettleDeal), or by the
 * end-date sweep when no one rules.  Open to all factions; no linked entity; a
 * fixed posted end-date; settled only by the deal-specific confirm/dispute/
 * rule ops (which live in jobs.cpp), never a generic fulfil.
 */
class DealPredicate : public JobPredicate
{

public:

  Faction
  AudienceFaction (const Account& poster) const override
  {
    return Faction::INVALID;
  }

  const std::vector<std::string>&
  PostTermKeys () const override
  {
    static const std::vector<std::string> keys
        = {"arbiter", "fee", "tag", "terms", "dp"};
    return keys;
  }

  Amount
  MinReward (const JobContext& jc) const override
  {
    return jc.params.Get ("min-deal-reward",
                          jc.ctx.RoConfig ()->params ().min_deal_reward ());
  }

  bool
  ValidatePost (const JobContext& jc, const Account& poster,
                const Json::Value& terms) const override
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
    if (terms.isMember ("arbiter"))
      {
        if (!terms["arbiter"].isString ())
          return false;
        const std::string arb = terms["arbiter"].asString ();
        if (arb.empty ())
          return false;
        /* A self-arbiter (arbiter == poster) is the poster's own already-held,
           obviously-initialised handle -- re-opening it here would collide on
           the UniqueHandles tracker, so skip the lookup for that case.  */
        if (arb != poster.GetName ())
          {
            const auto a = jc.accounts.GetByName (arb);
            if (a == nullptr || !a->IsInitialised ())
              {
                LOG (WARNING) << "Deal arbiter not initialised: " << arb;
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
       params are later retuned to.  A misconfigured "deal-tax-bps" or
       "deal-max-fee-bps" thus rejects NEW posts (recoverable by fixing the
       param) instead of freezing bad bps into rows whose settlement would
       CHECK-halt every node.  */
    const int64_t tax = DealTaxBps (jc);
    if (tax < 0 || tax + fee >= 10'000)
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

  void
  ApplyPost (const JobContext& jc, Account& poster, const Json::Value& terms,
             Job& job) const override
  {
    /* Validation guarantees a fee only ever comes with an arbiter, and
       that both tax and fee satisfy the §6.3 precondition.  */
    auto& d = *job.MutableProto ().mutable_deal ();
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
  }

  bool
  ValidateAccept (const JobContext& jc, const Job& job,
                  const Account& worker) const override
  {
    /* The arbiter must stay a third party: as worker it would judge its own
       dispute (accept + self-dispute + self-rule in one block would capture
       the whole escrow).  Poster == arbiter remains allowed -- the worker
       consents by accepting what the board already shows.  */
    if (worker.GetName () == job.GetProto ().deal ().arbiter ())
      {
        LOG (WARNING)
            << "Arbiter " << worker.GetName () << " cannot accept deal "
            << job.GetId ();
        return false;
      }
    return true;
  }

  JobOutcome
  OnExpire (const JobContext& jc, Job& job) const override
  {
    if (job.GetStatus () != Job::Status::ACCEPTED)
      {
        /* Never accepted: refund the poster's reward (nothing else locked).  */
        VoidJobAtHook (jc, job);
        return JobOutcome::VOID;
      }
    const auto& d = job.GetProto ().deal ();
    if (d.disputed ())
      /* Ghosted arbiter (or a no-arbiter dispute): the blunt 50/50 fallback.
         The arbiter FORFEITS its fee here -- it was hired precisely to rule
         this dispute and did not, and paying it anyway would make ghosting
         strictly better than the ruling (which costs a transaction).  */
      return SettleDeal (jc, job, 50, nullptr, false);
    if (d.poster_confirmed () || d.worker_confirmed ())
      /* One side confirmed, the other neither confirmed nor disputed: done.  */
      return SettleDeal (jc, job, 100, nullptr, true);
    /* Neither party acted: return both stakes.  */
    return RefundBothDeal (jc, job);
  }

};

/* ************************************************************************** */

/** The process-lifetime predicate singletons.  */
const WantedPredicate WANTED_PREDICATE;
const AdPredicate AD_PREDICATE;
const DealPredicate DEAL_PREDICATE;

/**
 * The one place a job type is registered: enum value, move/JSON name and
 * predicate object.  Every lookup (name -> type, type -> predicate,
 * type -> name) derives from this table, so a new type cannot be half-wired.
 */
struct JobTypeEntry
{
  Job::Type type;
  const char* name;
  const JobPredicate* predicate;
};

const JobTypeEntry JOB_TYPE_REGISTRY[] =
  {
    {Job::Type::WANTED, "wanted", &WANTED_PREDICATE},
    {Job::Type::AD, "ad", &AD_PREDICATE},
    {Job::Type::DEAL, "deal", &DEAL_PREDICATE},
  };

} // anonymous namespace

Job::Type
JobTypeFromString (const std::string& name)
{
  for (const auto& entry : JOB_TYPE_REGISTRY)
    if (name == entry.name)
      return entry.type;
  return Job::Type::INVALID;
}

const JobPredicate*
PredicateForType (const Job::Type type)
{
  for (const auto& entry : JOB_TYPE_REGISTRY)
    if (type == entry.type)
      return entry.predicate;
  return nullptr;
}

std::vector<Database::IdT>
JobPredicate::PostLinkedIds (const Json::Value& terms) const
{
  std::vector<Database::IdT> ids;
  for (const auto* key : PostLinkedIdKeys ())
    {
      Database::IdT id;
      CHECK (IdFromJson (terms[key], id));
      ids.push_back (id);
    }
  return ids;
}

void
PayKillShares (const JobContext& jc, const std::set<std::string>& owners,
               const unsigned payingPools, const Amount totalShare)
{
  CHECK_GT (payingPools, 0);
  CHECK_GT (totalShare, 0);
  for (const auto& owner : owners)
    {
      auto a = GetAccountChecked (jc, owner);
      ReleaseJobCoins (*a, totalShare);
      BumpJobStats (*a, totalShare, payingPools);
    }
}

const char*
JobTypeName (const Job::Type type)
{
  for (const auto& entry : JOB_TYPE_REGISTRY)
    if (type == entry.type)
      return entry.name;
  return nullptr;
}

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

JobOutcome
SettleDeal (const JobContext& jc, const Job& job, const int p,
            Account* const executor, const bool payArbiterFee)
{
  const auto& d = job.GetProto ().deal ();
  /* A forfeited fee (the ghosted-dispute timeout) settles as if none had
     been agreed: each party keeps its own fee share.  */
  const int feeBps = payArbiterFee ? d.fee_bps () : 0;
  const DealSettlement s = ComputeDealSettlement (
      job.GetReward (), job.GetCollateral (), p, d.tax_bps (), feeBps);

  /* Accumulate the net credit per distinct account name, so a self-arbiter or
     poster==arbiter is opened exactly once and never collides with the
     executor's live handle.  The treasury tax is intentionally NOT credited --
     it is burned until a faction treasury exists to receive it (Phase 4).  */
  std::map<std::string, Amount> credit;
  credit[job.GetWorker ()] += s.worker;
  credit[job.GetPoster ()] += s.poster;
  if (!d.arbiter ().empty ())
    credit[d.arbiter ()] += s.arbiter;

  const Amount earnedReward = job.GetReward () * p / 100;
  const bool creditRep = (p > 0 && s.treasury >= 1);
  const std::string workerName = job.GetWorker ();

  for (const auto& entry : credit)
    {
      const std::string& name = entry.first;
      const Amount amount = entry.second;
      const bool isWorker = (name == workerName);
      if (executor != nullptr && name == executor->GetName ())
        {
          if (amount > 0)
            ReleaseJobCoins (*executor, amount);
          if (creditRep && isWorker)
            BumpDealStats (*executor, earnedReward);
        }
      else
        {
          auto held = GetAccountChecked (jc, name);
          if (amount > 0)
            ReleaseJobCoins (*held, amount);
          if (creditRep && isWorker)
            BumpDealStats (*held, earnedReward);
        }
    }

  return p > 0 ? JobOutcome::COMPLETED : JobOutcome::FAILED;
}

JobOutcome
RefundBothDeal (const JobContext& jc, const Job& job)
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
  return JobOutcome::VOID;
}

} // namespace pxd
