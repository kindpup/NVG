//------------------------------------------------------------------------------------------------
// ACM_TDLinkTypes.c
// The shapes that cross the network, kept in one file so that what leaves the server is auditable
// in a single place rather than scattered through RPC signatures.
//
// THE RULE THIS FILE EXISTS TO ENFORCE:
//
//   Replication.FindItem(RplId) is public and returns the live object. A client handed a hostile's
//   replicated id can resolve the entity and poll GetOrigin() every frame - a wallhack reached
//   THROUGH the data link rather than around it. So a hostile RplId must never appear in anything
//   sent to a receiver, and the delivery payload below deliberately has no field that could carry
//   one.
//
// The server keeps rplId -> contactId privately. Receivers get contactId, which resolves to
// nothing without the server's map.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// How widely a reported contact is shared.
//
// The default is deliberately the narrowest useful setting. A faction-wide link is a very different
// game - it means every contact anyone spots is instantly known to everyone on the side, which
// removes the reason to talk to each other. Squad scope keeps the data link a squad asset and makes
// passing information between squads a decision someone has to make.
//------------------------------------------------------------------------------------------------
enum ACM_TDLinkScope
{
	// Contacts reach only the group that reported them. The default.
	SQUAD_ONLY,
	// Reserved. Needs a parent-formation concept that Reforger's group model does not currently
	// express, so it is NOT implemented - selecting it behaves as SQUAD_ONLY and says so in the log
	// rather than silently widening the net.
	PLATOON,
	// Every friendly on the side with a receiving device.
	FACTION,
	// Squad scope plus whatever explicit grants the server has issued. See ACM_TDLinkGrant.
	CUSTOM
}

//------------------------------------------------------------------------------------------------
// A server-issued permission for one group to receive another's contacts.
//
// SERVER-AUTHORITATIVE BY CONSTRUCTION: there is no RPC anywhere that creates one of these. A grant
// can only be made by server-side code - an admin tool, a mission script, a future command
// interface - because a client that could subscribe itself to another squad's feed would make the
// whole scope system decorative.
//------------------------------------------------------------------------------------------------
class ACM_TDLinkGrant
{
	int fromGroupId;
	int toGroupId;

	// When true the grant runs both ways, which is what "Squad A <-> Squad C are linked" means.
	// One-way is the JTAC case: aircrew hear the JTAC without the JTAC inheriting their picture.
	bool bidirectional;

	// Absolute time the grant lapses, or -1 for one that lasts the session.
	float expiresAt;

	//------------------------------------------------------------------------------------------------
	void ACM_TDLinkGrant(int from, int to, bool both, float expiry)
	{
		fromGroupId = from;
		toGroupId = to;
		bidirectional = both;
		expiresAt = expiry;
	}

	//------------------------------------------------------------------------------------------------
	bool Covers(int producerGroupId, int consumerGroupId)
	{
		if (fromGroupId == producerGroupId && toGroupId == consumerGroupId)
			return true;

		return bidirectional && fromGroupId == consumerGroupId && toGroupId == producerGroupId;
	}

	//------------------------------------------------------------------------------------------------
	bool IsExpired(float now)
	{
		return expiresAt > 0 && now >= expiresAt;
	}
}

//------------------------------------------------------------------------------------------------
// Broad classification of what a contact is. Travels with the contact so a receiver can style or
// filter without another networking change, and so AI and player contacts can be budgeted apart.
//------------------------------------------------------------------------------------------------
enum ACM_TDContactClass
{
	UNKNOWN,
	INFANTRY_AI,
	INFANTRY_PLAYER,
	VEHICLE
}

//------------------------------------------------------------------------------------------------
// Why a report was refused. Server-side only - a rejection reason is never sent back to the
// reporting client, because telling a probing client WHICH gate caught it is telling it how to
// build a report that gets through.
//------------------------------------------------------------------------------------------------
enum ACM_TDRejectReason
{
	ACCEPTED,
	NO_REPORTER,
	RATE_LIMITED,
	NO_CAPABILITY,
	TARGET_MISSING,
	NOT_HOSTILE,
	OUT_OF_RANGE,
	POSITION_IMPLAUSIBLE,
	LOS_AUDIT_FAILED,
	TABLE_FULL
}

//------------------------------------------------------------------------------------------------
// One tracked contact, server side.
//
// This object never leaves the server. The fields a receiver is allowed to see are copied out
// individually at delivery time - never the object - so adding a field here cannot accidentally
// widen the payload.
//------------------------------------------------------------------------------------------------
class ACM_TDLinkContact
{
	// Opaque, server-issued, monotonically increasing. The only identity receivers ever see.
	int contactId;

	// THE SECRET. Replicated id of the target entity, used for dedup and for server-side
	// validation against ground truth. Never copied into a delivery.
	int targetRplId;

	vector lastKnownPos;
	float lastConfirmedTime;

	// What kind of sensor made the most recent confirmation. Shared; carries no authority.
	ACM_TDSensor sensor;

	ACM_TDContactClass classification;

	// Faction key of the side this contact belongs TO - that is, the side that may receive it.
	// Contacts are delivered only to their own side, so this is the scoping key.
	string ownerFactionKey;

	// Who last confirmed it. Sent to receivers so a HUD can credit the spotter.
	int reporterPlayerId;

	// Every group that has independently reported this contact.
	//
	// A set rather than a single reporter, because dedup collapses one target into one contact no
	// matter how many people see it. If eligibility were judged on "the last reporter's group",
	// two squads watching the same enemy would take turns owning it and each would lose the marker
	// whenever the other reported - the contact would flicker between them. A group that earned a
	// contact keeps it until the contact expires.
	ref array<int> contributingGroups;

	// Position at last delivery, so the manager can send deltas rather than a stream.
	vector lastDeliveredPos;
	float lastDeliveredTime;

	//------------------------------------------------------------------------------------------------
	void ACM_TDLinkContact(int id, int rplId)
	{
		contactId = id;
		targetRplId = rplId;
		lastConfirmedTime = -1;
		lastDeliveredTime = -1;
		sensor = ACM_TDSensor.VISUAL;
		classification = ACM_TDContactClass.UNKNOWN;
		reporterPlayerId = -1;
		contributingGroups = {};
	}

	//------------------------------------------------------------------------------------------------
	bool IsAiContact()
	{
		return classification == ACM_TDContactClass.INFANTRY_AI;
	}

	//------------------------------------------------------------------------------------------------
	void AddContributingGroup(int groupId)
	{
		// A group id of -1 means the reporter had no group. Deliberately not stored: an ungrouped
		// reporter must not create an "ungrouped" bucket that every other ungrouped player on the
		// server would then match against.
		if (groupId < 0)
			return;

		if (contributingGroups.Find(groupId) == -1)
			contributingGroups.Insert(groupId);
	}

	//------------------------------------------------------------------------------------------------
	bool WasReportedByGroup(int groupId)
	{
		return groupId >= 0 && contributingGroups.Find(groupId) != -1;
	}
}

//------------------------------------------------------------------------------------------------
// Per-reporter rate budget. A token bucket rather than a simple interval, so a spotter who has
// been quiet can report a burst when a squad walks into view - which is exactly when the link is
// most useful - without a sustained stream being possible.
//------------------------------------------------------------------------------------------------
class ACM_TDLinkBudget
{
	float tokens;
	float lastRefill;

	// Consecutive sampled-LOS failures. Not reset by a single pass: a client that fails a third of
	// its audits is not unlucky, and one clean report should not launder that.
	int losFailures;
	int losAudits;

	// Suspended until this timestamp after too many audit failures.
	float suspendedUntil;

	//------------------------------------------------------------------------------------------------
	void ACM_TDLinkBudget(float capacity, float now)
	{
		tokens = capacity;
		lastRefill = now;
		suspendedUntil = -1;
	}

	//------------------------------------------------------------------------------------------------
	// Returns true and spends a token, or returns false and spends nothing.
	//------------------------------------------------------------------------------------------------
	bool TryConsume(float now, float capacity, float refillPerSecond)
	{
		if (suspendedUntil > 0 && now < suspendedUntil)
			return false;

		float elapsed = now - lastRefill;
		if (elapsed > 0)
		{
			tokens = Math.Min(capacity, tokens + (elapsed * refillPerSecond));
			lastRefill = now;
		}

		if (tokens < 1.0)
			return false;

		tokens = tokens - 1.0;

		return true;
	}
}
