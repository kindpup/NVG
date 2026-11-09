//------------------------------------------------------------------------------------------------
// ACM_RemoteFeedSourceComponent.c
// Makes any entity a Remote Feed camera source. Drone, dog, vehicle, helmet, fence post - the
// presence of the component is the opt-in.
//
// Keep it generic. No UAV mod, K9 mod or external class may be referenced here. A source needing
// special handling does it through the attributes below or a subclass in its own addon.
//
// The component holds placement data and answers questions. It never creates a camera - the
// renderer asks for a transform, a field of view and a callsign, and that is the whole contract.
//
// Two things that look like bugs and are not:
//
//   The camera rides the carrier, not this entity. An attachment's own origin does not animate, so
//   a camera parked on it sits still while the wearer walks off.
//
//   A bone matrix is entity-local. Used raw it puts the camera near the map origin, which on most
//   maps is open water - and featureless grey looks like a broken render target.
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Remote Feed", description: "Makes this entity available as an IVAS Remote Feed camera source.")]
class ACM_RemoteFeedSourceComponentClass : ScriptComponentClass
{
}

class ACM_RemoteFeedSourceComponent : ScriptComponent
{
	// Further than this from its own carrier means the transform was never world space.
	protected static const float ANCHOR_SANITY_METRES = 50;

	// An uncarried entity this close to the origin was never placed.
	protected static const float UNPLACED_ORIGIN_METRES = 1.0;

	// Pathological hierarchies must never spin the carrier walk.
	protected static const int HIERARCHY_GUARD = 8;

	// Runs every frame, so the debug line is rate limited.
	protected static const float DEBUG_LOG_INTERVAL_MS = 1000;
	protected float m_fLastDebugLog;
	protected bool m_bReportedBoneMiss;
	protected ref ACM_RemoteFeedProfile m_DefaultProfile;

	//------------------------------------------------------------------------------------------------
	// Identity
	//------------------------------------------------------------------------------------------------
	[Attribute("0", UIWidgets.ComboBox, desc: "What is carrying this camera. Labelling only - no behaviour depends on it.", category: "ACM Remote Feed - Identity", enums: ParamEnumArray.FromEnum(ACM_ERemoteFeedSourceType))]
	protected ACM_ERemoteFeedSourceType m_eSourceType;

	[Attribute("", UIWidgets.EditBox, desc: "Callsign shown on the feed, e.g. RAVEN-01 or REX. Left blank, the source type is used instead.", category: "ACM Remote Feed - Identity")]
	protected string m_sCallsign;

	[Attribute("1", UIWidgets.CheckBox, desc: "Whether this source is available at all. Turn off to keep the component authored but exclude it from the cycle.", category: "ACM Remote Feed - Identity")]
	protected bool m_bEnabled;

	[Attribute("OPERATOR", UIWidgets.EditBox, desc: "Word used for whoever this source belongs to. OPERATOR for a drone, HANDLER for a dog, CREW for a vehicle mount. Authored rather than derived from the source type, so no code has to branch on what kind of thing this is.", category: "ACM Remote Feed - Identity")]
	protected string m_sOwnerRole;

	[Attribute("", UIWidgets.EditBox, desc: "Fallback owner shown when nobody is controlling this source - a fixed post name, say. Left blank, an uncontrolled source reads UNASSIGNED.", category: "ACM Remote Feed - Identity")]
	protected string m_sOwnerFallback;

	[Attribute("0", UIWidgets.CheckBox, desc: "Tick for gear that only makes sense while somebody is wearing or holding it - a helmet camera, a headset. Such a source is hidden from the cycle whenever it has no carrier, which keeps spare units in crates and on the ground out of the feed. Leave off for anything that carries itself, like a drone or a fixed camera.", category: "ACM Remote Feed - Identity")]
	protected bool m_bRequiresCarrier;

	//------------------------------------------------------------------------------------------------
	// Camera mount
	//------------------------------------------------------------------------------------------------
	[Attribute(desc: "THE MOUNT. Click 'set class', choose PointInfo, and its Bone field lists the real bones of this asset's XOB - the engine builds that list from the mesh, which is the same picker a muzzle or a weapon sight uses. Set the bone, and the camera rides it.", category: "ACM Remote Feed - Mount", uiwidget: UIWidgets.Object)]
	protected ref PointInfo m_CameraAnchor;

	[Attribute("0 0 0", UIWidgets.EditBox, desc: "Camera offset from the mount, in mount space: right, up, forward.", category: "ACM Remote Feed - Mount")]
	protected vector m_vAnchorOffset;

	[Attribute("0 0 0", UIWidgets.EditBox, desc: "Camera angle correction in degrees: yaw, pitch, roll. Negative pitch looks down. If the picture comes out sideways or mirrored, correct it here rather than in the mesh.", category: "ACM Remote Feed - Mount")]
	protected vector m_vAnchorAngles;

	[Attribute("0", UIWidgets.ComboBox, desc: "FOLLOW_ANCHOR rides the mount, so turning the carrier turns the view - a camera bolted to a dog. WORLD_STABILISED keeps the authored angles regardless of how the carrier is oriented - a gimbal.", category: "ACM Remote Feed - Mount", enums: ParamEnumArray.FromEnum(ACM_ERemoteFeedAimMode))]
	protected ACM_ERemoteFeedAimMode m_eAimMode;

	//------------------------------------------------------------------------------------------------
	// Alternative anchors, used only when no bone is set or the bone does not resolve.
	//------------------------------------------------------------------------------------------------
	[Attribute("", UIWidgets.EditBox, desc: "Escape hatch: a bone name typed by hand. Only needed to ride a bone on the CARRIER rather than on this asset - a character's head, say - because the mount above lists this asset's bones and not the wearer's. If the name misses, the log prints every bone both skeletons actually have.", category: "ACM Remote Feed - Alternative Mount")]
	protected string m_sCameraBone;

	[Attribute("0", UIWidgets.ComboBox, desc: "Whose skeleton the typed bone name above is looked up on. AUTO tries this asset first, then the carrier.", category: "ACM Remote Feed - Alternative Mount", enums: ParamEnumArray.FromEnum(ACM_ERemoteFeedBoneSource))]
	protected ACM_ERemoteFeedBoneSource m_eBoneSource;

	[Attribute("", UIWidgets.EditBox, desc: "Alternative mount: name of a child entity to ride. Useful when the camera should follow a sub-object that moves on its own.", category: "ACM Remote Feed - Alternative Mount")]
	protected string m_sAnchorChildName;

	[Attribute("", UIWidgets.EditBox, desc: "Alternative mount: named slot on this entity, such as a gimbal or camera hardpoint.", category: "ACM Remote Feed - Alternative Mount")]
	protected string m_sAnchorSlotName;

	//------------------------------------------------------------------------------------------------
	// Camera
	//
	// These live here rather than on the renderer deliberately: a long-lens gimbal and a wide
	// bodycam are different cameras, and that difference belongs to the thing carrying them.
	//------------------------------------------------------------------------------------------------
	[Attribute("70", UIWidgets.Slider, desc: "Vertical field of view in degrees. A body camera reads well around 70-85; lower is more zoomed in.", category: "ACM Remote Feed - Camera", params: "10 120 1")]
	protected float m_fFieldOfView;

	[Attribute("1.0", UIWidgets.EditBox, desc: "Brightness as a MULTIPLIER on the main view's exposure. 1 matches the world. This is not an absolute value - a raw number blows the picture to white.", category: "ACM Remote Feed - Camera")]
	protected float m_fExposureScale;

	[Attribute("0", UIWidgets.CheckBox, desc: "Log mount resolution and camera placement for this source.", category: "ACM Remote Feed - Camera")]
	protected bool m_bDebugLog;

	//------------------------------------------------------------------------------------------------
	// Link
	//
	// Signal characteristics belong to the source, never to the renderer. A new kind of camera
	// should need a new profile here, not a new branch in the display code.
	//------------------------------------------------------------------------------------------------
	[Attribute(desc: "Link characteristics for this source: ranges, obstruction weight, antenna gain. Leave unset and sensible defaults are chosen from the source type above.", category: "ACM Remote Feed - Link", uiwidget: UIWidgets.Object)]
	protected ref ACM_RemoteFeedProfile m_LinkProfile;

	[Attribute("1", UIWidgets.CheckBox, desc: "Assume this source is powered when its power state cannot be read. Third-party UAV and K9 mods rarely replicate a power flag, and pretending we can prove one would be worse than admitting we cannot - so this is the honest default. Untick only for a source whose power state ACM actually owns.", category: "ACM Remote Feed - Link")]
	protected bool m_bAssumePowered;

	[Attribute("1", UIWidgets.ComboBox, desc: "Who may watch this camera. Evaluated before any signal is calculated: an unauthorised source is not listed, has no link percentage, and gives away nothing about its existence. A source with no operator falls back to this rather than becoming public.", category: "ACM Remote Feed - Link", enums: ParamEnumArray.FromEnum(ACM_ERemoteFeedAccess))]
	protected ACM_ERemoteFeedAccess m_eAccessPolicy;

	//------------------------------------------------------------------------------------------------
	// Registry
	//
	// Static rather than a manager component - a source has to be discoverable the moment it
	// exists, including on entities spawned long after any manager was built.
	//------------------------------------------------------------------------------------------------
	protected static ref array<ACM_RemoteFeedSourceComponent> s_aSources;

	//------------------------------------------------------------------------------------------------
	// Sources this viewer may see. Filtered here rather than at the display: an unauthorised source
	// must not appear, must not be cycled onto and must never have signal calculated. A link
	// percentage on a camera you cannot access still tells you it exists.
	//------------------------------------------------------------------------------------------------
	static int GetAuthorisedSources(int viewerPlayerId, out notnull array<ACM_RemoteFeedSourceComponent> outSources)
	{
		outSources.Clear();

		if (!s_aSources)
			return 0;

		foreach (ACM_RemoteFeedSourceComponent source : s_aSources)
		{
			if (!source)
				continue;

			if (!source.IsAvailable())
				continue;

			if (!source.CanViewRemoteFeed(viewerPlayerId))
				continue;

			outSources.Insert(source);
		}

		return outSources.Count();
	}

	//------------------------------------------------------------------------------------------------
	// Where this source actually is right now, for diagnostics. Returns false when it has no
	// usable position - stowed in a crate, or no carrier at all.
	//------------------------------------------------------------------------------------------------
	bool GetDiagnosticPosition(out vector outPos)
	{
		bool stowed;
		IEntity carrier = ResolveCarrier(stowed);
		if (!carrier || stowed)
			return false;

		outPos = carrier.GetOrigin();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!s_aSources)
			s_aSources = new array<ACM_RemoteFeedSourceComponent>();

		if (s_aSources.Find(this) == -1)
			s_aSources.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (s_aSources)
		{
			int index = s_aSources.Find(this);
			if (index != -1)
				s_aSources.Remove(index);
		}

		// Tell the renderer rather than leaving it rendering a dead mount.
		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();
		if (renderer)
			renderer.OnSourceRemoved(this);

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	bool IsAvailable()
	{
		if (!m_bEnabled)
			return false;

		if (!GetOwner())
			return false;

		// A spare unit in a supply crate is a real component with a real owner and no useful
		// position. Offering it in the cycle means the feed can land on a camera that is nowhere.
		bool stowed;
		IEntity carrier = ResolveCarrier(stowed);
		if (!carrier || stowed)
			return false;

		// Gear that only means something while worn: no carrier, no feed.
		if (m_bRequiresCarrier && carrier == GetOwner())
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	string GetCallsign()
	{
		if (m_sCallsign != string.Empty)
			return m_sCallsign;

		return SCR_Enum.GetEnumName(ACM_ERemoteFeedSourceType, m_eSourceType);
	}

	//------------------------------------------------------------------------------------------------
	ACM_ERemoteFeedSourceType GetSourceType()
	{
		return m_eSourceType;
	}

	// A ladder, because every mod represents control differently and none of it may leak into the
	// renderer. Possession first - a player flying a drone while their body stands elsewhere - then
	// direct control, then whatever was authored. Zero means nobody, which is a valid answer.
	//------------------------------------------------------------------------------------------------
	int GetControllerPlayerId()
	{
		bool stowed;
		IEntity carrier = ResolveCarrier(stowed);
		if (!carrier)
			return 0;

		int playerId = SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(carrier);
		if (playerId > 0)
			return playerId;

		PlayerManager players = GetGame().GetPlayerManager();
		if (!players)
			return 0;

		return players.GetPlayerIdFromControlledEntity(carrier);
	}

	//------------------------------------------------------------------------------------------------
	string GetControllerDisplayName()
	{
		int playerId = GetControllerPlayerId();
		if (playerId > 0)
		{
			PlayerManager players = GetGame().GetPlayerManager();
			if (players)
			{
				string name = ACM_PlayerNames.GetDisplayName(playerId);
				if (name != string.Empty)
					return name;
			}
		}

		if (m_sOwnerFallback != string.Empty)
			return m_sOwnerFallback;

		return "UNASSIGNED";
	}

	//------------------------------------------------------------------------------------------------
	string GetOwnerRole()
	{
		if (m_sOwnerRole != string.Empty)
			return m_sOwnerRole;

		return "OPERATOR";
	}

	//------------------------------------------------------------------------------------------------
	// So the HUD can say "yours" instead of repeating the viewer's own name back at them.
	//------------------------------------------------------------------------------------------------
	bool IsLocallyControlled()
	{
		int playerId = GetControllerPlayerId();
		if (playerId <= 0)
			return false;

		return playerId == SCR_PlayerController.GetLocalPlayerId();
	}

	// Link
	//------------------------------------------------------------------------------------------------
	ACM_RemoteFeedProfile GetLinkProfile()
	{
		if (m_LinkProfile)
			return m_LinkProfile;

		// Built once and kept, so an unauthored source does not allocate a profile per evaluation.
		if (!m_DefaultProfile)
			m_DefaultProfile = ACM_RemoteFeedProfileDefaults.For(m_eSourceType);

		return m_DefaultProfile;
	}

	//------------------------------------------------------------------------------------------------
	// ACM does not own these entities and most third-party mods do not replicate a power flag, so
	// this reports the authored assumption rather than inventing certainty.
	//------------------------------------------------------------------------------------------------
	bool IsPowered()
	{
		if (!IsAvailable())
			return false;

		return m_bAssumePowered;
	}

	//------------------------------------------------------------------------------------------------
	// Where the transmitter is. The camera anchor, being the part actually out in the world.
	//------------------------------------------------------------------------------------------------
	bool GetLinkOrigin(out vector outPos)
	{
		vector mat[4];
		if (!GetFeedTransform(mat))
			return false;

		outPos = mat[3];
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Access, evaluated before any signal. Separate from control: watching a drone's picture and
	// flying it are different rights.
	//------------------------------------------------------------------------------------------------
	bool CanViewRemoteFeed(int viewerPlayerId)
	{
		if (viewerPlayerId <= 0 || !IsAvailable())
			return false;

		if (m_eAccessPolicy == ACM_ERemoteFeedAccess.PUBLIC)
			return true;

		int controllerId = GetControllerPlayerId();

		// The operator always sees their own camera.
		if (controllerId > 0 && controllerId == viewerPlayerId)
			return true;

		if (m_eAccessPolicy == ACM_ERemoteFeedAccess.CONTROLLER_ONLY)
			return false;

		if (m_eAccessPolicy == ACM_ERemoteFeedAccess.SQUAD)
		{
			// No operator means no squad to belong to. Falling through to "everyone" here is what
			// turns an abandoned drone into a public camera.
			if (controllerId <= 0)
				return false;

			if (SameGroup(controllerId, viewerPlayerId))
				return true;

			// NO GROUP SYSTEM IN PLAY.
			//
			// SameGroup needs SCR_GroupsManagerComponent to hold a group for BOTH players. Plenty of
			// scenarios never assign one - Game Master, Workbench, and anything that does not use
			// the group system at all - and there the test cannot be evaluated rather than being
			// evaluated to "no".
			//
			// Denying on an unevaluable test is what makes squad cameras look broken. So when
			// NEITHER side has a group, the faction stands in for the squad.
			//
			// This narrows to the viewer's own side and no further. It cannot expose a camera to an
			// enemy, and it cannot fire when a group system IS running - if either player has a
			// group, SameGroup was a real answer and its "no" is respected.
			if (!HasResolvableGroup(controllerId) && !HasResolvableGroup(viewerPlayerId))
				return SameFaction(viewerPlayerId);

			return false;
		}

		if (m_eAccessPolicy == ACM_ERemoteFeedAccess.FACTION)
			return SameFaction(viewerPlayerId);

		// EXPLICIT_LINK. Group links live on the contact manager, so this defers to the same grants
		// the data link uses rather than keeping a second list.
		return HasExplicitLink(controllerId, viewerPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	protected bool SameFaction(int viewerPlayerId)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return false;

		PlayerManager players = GetGame().GetPlayerManager();
		if (!players)
			return false;

		IEntity viewer = players.GetPlayerControlledEntity(viewerPlayerId);
		if (!viewer)
			return false;

		Faction sourceFaction = SCR_Faction.GetEntityFaction(owner);
		Faction viewerFaction = SCR_Faction.GetEntityFaction(viewer);
		if (!sourceFaction || !viewerFaction)
			return false;

		return sourceFaction.GetFactionKey() == viewerFaction.GetFactionKey();
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasExplicitLink(int controllerId, int viewerPlayerId)
	{
		if (controllerId <= 0)
			return false;

		ACM_TacticalContactManager manager = ACM_TacticalContactManager.GetInstance();
		if (!manager)
			return false;

		return manager.GroupsAreLinked(controllerId, viewerPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	// Does the group system hold a group for this player at all?
	//
	// Distinct from "is in my group". Used to tell an answered question from an unanswerable one,
	// which is the difference between a permissions decision and a missing subsystem.
	//------------------------------------------------------------------------------------------------
	protected bool HasResolvableGroup(int playerId)
	{
		if (playerId <= 0)
			return false;

		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return false;

		return groups.GetPlayerGroup(playerId) != null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool SameGroup(int a, int b)
	{
		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return false;

		SCR_AIGroup groupA = groups.GetPlayerGroup(a);
		SCR_AIGroup groupB = groups.GetPlayerGroup(b);

		if (!groupA || !groupB)
			return false;

		return groupA.GetGroupID() == groupB.GetGroupID();
	}

	//------------------------------------------------------------------------------------------------
	float GetFieldOfView()
	{
		return m_fFieldOfView;
	}

	//------------------------------------------------------------------------------------------------
	float GetExposureScale()
	{
		return m_fExposureScale;
	}

	//------------------------------------------------------------------------------------------------
	// Whatever is actually carrying this entity. A helmet camera's own transform does not animate;
	// the character wearing it does.
	//------------------------------------------------------------------------------------------------
	IEntity GetCarrier()
	{
		bool stowed;
		return ResolveCarrier(stowed);
	}

	//------------------------------------------------------------------------------------------------
	// Three ways to be carried, and only the last is the entity hierarchy. Character equipment is
	// not parented - it sits in an inventory slot, so GetParent() from a worn helmet finds nothing
	// and leaves the camera on the item's own transform, which for a stowed item is the origin.
	//------------------------------------------------------------------------------------------------
	protected IEntity ResolveCarrier(out bool stowed)
	{
		stowed = false;

		IEntity owner = GetOwner();
		if (!owner)
			return null;

		// 1 - worn or carried by a character, by ANY route.
		//
		// Was FindCharacterOwnerFromEntity, which follows inventory slots only. That resolves gear
		// sitting directly in a character's slots and fails on a custom mount, where the chain runs
		// through a scene-child adapter first. When it failed, step 2 below walked scene parents,
		// hit the MOUNT, and returned the mount as the "carrier" - so the camera had a carrier that
		// was not a person, GetControllerPlayerId returned 0, and the default SQUAD access policy
		// refused it. That is the "squadmate's camera is missing or disabled" report: not a
		// permissions decision, a failed walk.
		ChimeraCharacter character = ACM_InventoryUtils.FindCharacterOwnerAnyRoute(owner);
		if (character)
			return character;

		// 2 - attached as a child entity, the way gear sits on an animal or a vehicle.
		IEntity carrier = owner;
		IEntity parent = carrier.GetParent();
		int guard = 0;

		while (parent)
		{
			guard++;
			if (guard > HIERARCHY_GUARD)
				break;

			carrier = parent;
			parent = carrier.GetParent();
		}

		if (carrier != owner)
			return carrier;

		// 3 - free-standing, or not really in the world at all. Two ways to be nowhere: sitting in
		// a crate, which the inventory slot reveals, or never placed - template and preview
		// instances register like anything else and sit at the origin.
		InventoryItemComponent item = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
		if (item && item.GetParentSlot())
			stowed = true;

		if (!stowed && vector.Distance(owner.GetOrigin(), vector.Zero) < UNPLACED_ORIGIN_METRES)
			stowed = true;

		return owner;
	}

	//------------------------------------------------------------------------------------------------
	// On demand, not cached - gear is attached after the carrier is built, so an init-time lookup
	// runs before the skeleton exists.
	//------------------------------------------------------------------------------------------------
	protected string GetCameraBoneName()
	{
		return Trim(m_sCameraBone);
	}

	//------------------------------------------------------------------------------------------------
	// A typed bone name picks up stray whitespace astonishingly easily, and " w_nvgRotator" misses
	// a bone called "w_nvgRotator" with a message that blames the bone rather than the space.
	//------------------------------------------------------------------------------------------------
	protected string Trim(string value)
	{
		int length = value.Length();

		int start = 0;
		while (start < length && value.Get(start) == " ")
			start++;

		int end = length - 1;
		while (end >= start && value.Get(end) == " ")
			end--;

		if (end < start)
			return string.Empty;

		return value.Substring(start, end - start + 1);
	}

	//------------------------------------------------------------------------------------------------
	// Resolves the bone and reports WHICH entity it was found on, because that is the entity its
	// matrix has to be composed with. A bone matrix is entity-local, so pairing a bone from one
	// skeleton with another entity's world transform puts the camera somewhere arbitrary.
	//------------------------------------------------------------------------------------------------
	protected int ResolveBone(IEntity owner, IEntity carrier, out IEntity outHost)
	{
		outHost = null;

		string boneName = GetCameraBoneName();
		if (boneName == string.Empty)
			return -1;

		if (m_eBoneSource != ACM_ERemoteFeedBoneSource.CARRIER)
		{
			int ownBone = BoneIndexOn(owner, boneName);
			if (ownBone != -1)
			{
				outHost = owner;
				return ownBone;
			}
		}

		if (m_eBoneSource != ACM_ERemoteFeedBoneSource.OWNER)
		{
			int carrierBone = BoneIndexOn(carrier, boneName);
			if (carrierBone != -1)
			{
				outHost = carrier;
				return carrierBone;
			}
		}

		ReportBoneMiss(boneName, owner, carrier);
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	protected int BoneIndexOn(IEntity entity, string boneName)
	{
		if (!entity)
			return -1;

		Animation anim = entity.GetAnimation();
		if (!anim)
			return -1;

		return anim.GetBoneIndex(boneName);
	}

	//------------------------------------------------------------------------------------------------
	// Once per component, and from both skeletons - you cannot guess a bone name and the two hold
	// completely different sets.
	//------------------------------------------------------------------------------------------------
	protected void ReportBoneMiss(string boneName, IEntity owner, IEntity carrier)
	{
		if (m_bReportedBoneMiss)
			return;

		m_bReportedBoneMiss = true;

		Print("[ACM RemoteFeed] " + GetCallsign() + " bone '" + boneName + "' not found - riding the mount's origin instead.", LogLevel.WARNING);

		PrintBones("this entity", owner);

		if (carrier != owner)
			PrintBones("its carrier", carrier);
	}

	//------------------------------------------------------------------------------------------------
	protected void PrintBones(string label, IEntity entity)
	{
		if (!entity)
			return;

		array<string> boneNames = new array<string>();
		entity.GetBoneNames(boneNames);

		int total = boneNames.Count();
		if (total == 0)
		{
			Print("[ACM RemoteFeed]   " + label + " has no skeleton.", LogLevel.DEBUG);
			return;
		}

		int listed = total;
		if (listed > 60)
			listed = 60;

		string available;
		for (int i = 0; i < listed; i++)
		{
			available = available + boneNames[i];
			if (i < listed - 1)
				available = available + ", ";
		}

		Print("[ACM RemoteFeed]   " + label + " has " + total.ToString() + " bones: " + available, LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Where the lens sits in world space right now.
	//------------------------------------------------------------------------------------------------
	bool GetFeedTransform(out vector outMat[4])
	{
		bool stowed;
		IEntity carrier = ResolveCarrier(stowed);
		if (!carrier || stowed)
			return false;

		vector base[4];
		bool resolved = false;

		// 1 - the mount. A PointInfo carries its own bone and local transform and resolves both,
		// which is why it is the field with the real bone list.
		IEntity boneHost;
		int bone = -1;

		if (m_CameraAnchor)
		{
			// Init is re-checked here rather than trusted from OnPostInit: an uninitialised
			// PointInfo does not fail, it quietly returns its LOCAL transform as if it were world.
			if (!m_CameraAnchor.GetOwner())
				m_CameraAnchor.Init(GetOwner());

			m_CameraAnchor.GetWorldTransform(base);
			boneHost = GetOwner();
			resolved = true;
		}

		// 2 - a bone name typed by hand, on this asset's skeleton or the carrier's.
		if (!resolved)
		{
			bone = ResolveBone(GetOwner(), carrier, boneHost);
			if (bone != -1 && boneHost)
			{
				Animation anim = boneHost.GetAnimation();
				if (anim)
				{
					vector boneMat[4];
					if (anim.GetBoneMatrix(bone, boneMat))
					{
						// Entity-local to world, against the skeleton the bone came from.
						// Skipping this is what puts the camera in the ocean.
						vector hostWorld[4];
						boneHost.GetWorldTransform(hostWorld);
						Math3D.MatrixMultiply4(hostWorld, boneMat, base);
						resolved = true;
					}
				}
			}
		}

		// 3 - named child entity.
		if (!resolved && m_sAnchorChildName != string.Empty)
		{
			IEntity anchorEntity = FindChildByName(GetOwner(), m_sAnchorChildName);
			if (anchorEntity)
			{
				anchorEntity.GetWorldTransform(base);
				resolved = true;
			}
		}

		// 4 - named slot.
		if (!resolved && m_sAnchorSlotName != string.Empty)
		{
			SlotManagerComponent slots = SlotManagerComponent.Cast(GetOwner().FindComponent(SlotManagerComponent));
			if (slots)
			{
				EntitySlotInfo slot = slots.GetSlotByName(m_sAnchorSlotName);
				if (slot)
				{
					slot.GetWorldTransform(base);
					resolved = true;
				}
			}
		}

		// 5 - the carrier itself. Always available.
		if (!resolved)
			carrier.GetWorldTransform(base);

		// Any rung above can hand back a local transform instead of a world one, and it fails
		// silently. A mount nowhere near its own carrier did not resolve; it is not a long boom.
		IEntity mountHost = carrier;
		if (boneHost)
			mountHost = boneHost;

		float stray = vector.Distance(base[3], mountHost.GetOrigin());
		if (stray > ANCHOR_SANITY_METRES)
		{
			Print("[ACM RemoteFeed] Mount for " + GetCallsign() + " resolved to " + base[3].ToString() + " but its mount is at " + mountHost.GetOrigin().ToString() + ", " + stray.ToString() + "m away - that is a local transform, not a world one. Falling back to the carrier.", LogLevel.WARNING);
			carrier.GetWorldTransform(base);
		}

		// Angle correction, then push the lens out along the mount's own axes so the offset means
		// what it looks like it means.
		vector rot[3];
		Math3D.AnglesToMatrix(m_vAnchorAngles, rot);

		vector lens[4];

		if (m_eAimMode == ACM_ERemoteFeedAimMode.WORLD_STABILISED)
		{
			// Gimbal: keep the mount's position, discard its rotation.
			lens[0] = rot[0];
			lens[1] = rot[1];
			lens[2] = rot[2];
		}
		else
		{
			Math3D.MatrixMultiply3(base, rot, lens);
		}

		lens[3] = base[3]
			+ base[0] * m_vAnchorOffset[0]
			+ base[1] * m_vAnchorOffset[1]
			+ base[2] * m_vAnchorOffset[2];

		Math3D.MatrixCopy(lens, outMat);

		// Throttled. This runs every frame, and an unthrottled line here buries every other
		// message in the console within seconds.
		if (m_bDebugLog)
		{
			float now = System.GetTickCount();
			if (now - m_fLastDebugLog > DEBUG_LOG_INTERVAL_MS)
			{
				m_fLastDebugLog = now;
				string mountedOn = "carrier origin";
				if (m_CameraAnchor)
					mountedOn = "camera anchor";
				else if (bone != -1)
					mountedOn = "bone " + bone.ToString();

				Print("[ACM RemoteFeed] " + GetCallsign() + " lens at " + lens[3].ToString() + " via " + mountedOn + " (carrier at " + carrier.GetOrigin().ToString() + ").", LogLevel.DEBUG);
			}
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	// By name, not index - an index re-points the camera the moment anyone reorders the children.
	//------------------------------------------------------------------------------------------------
	protected IEntity FindChildByName(IEntity parent, string name)
	{
		if (!parent)
			return null;

		IEntity child = parent.GetChildren();
		while (child)
		{
			if (child.GetName() == name)
				return child;

			IEntity found = FindChildByName(child, name);
			if (found)
				return found;

			child = child.GetSibling();
		}

		return null;
	}
}
