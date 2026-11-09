// ACM Thermal Test Component - a real thermal sight built on the NIGHT VISION component
//
// WHY IT DERIVES FROM ACM_NightVisionComponent AND NOT FROM THE THERMAL ONE.
//
// ACM_ThermalComponentV2 drives PostProcessEffectType.ThermalImaging in slot 11, and that effect is
// the thing this rig exists to get away from. It is a screen-space colour remap: it takes the frame
// the rasteriser already produced and pushes its luminance through a lookup material. It never
// learns what anything is MADE of. A person is not warm to it - a person is however many pixels the
// scene lighting happened to give him. Man in shadow reads dark. Man against a sunlit wall vanishes
// into the wall. Those are the exact cases a thermal sight exists for, and vanilla answers all of
// them wrong. Building on that component would mean inheriting the very code being replaced.
//
// The night vision component is the right parent because its post-process stack is already the one
// this needs. It CLEARS slot 11 rather than using it, then puts a colorisation material into slot
// 16 as HDR and drives SetCameraHDRBrightness on top. That HDR path is a straight luminance
// treatment of the scene - exactly what a thermal image is - and it is the same path, the same
// slot and the same brightness plumbing the goggles already use, which is what was asked for.
//
// Everything else comes along for free and none of it had to be rewritten: deferred owner
// resolution, the worn debounce, ModeSwitch/ModeClear, the input registration, the animation
// signal, the device-visibility handling, the menu open/close recovery, and the detection-system
// keepalive. All of it is inherited, all of it already flight-proven.
//
// WHERE THE HEAT COMES FROM.
//
// The engine exposes no per-object temperature channel. But EMISSION survives to the post-process
// stage independent of scene lighting, so it is the one per-object property that can carry heat.
// Every character in range is painted by ACM_ScopedHighlightComponent - the SAME actuator the IVAS
// scoped highlight already drives, on the base character prefab, already working. A second parallel
// actuator was written for this and it was a mistake: it duplicated a solved problem, and because
// two components with independent save registers can both take over the same materials, it was a
// mistake with a corruption bug waiting inside it. One actuator, two callers.
//
// NOTHING EXISTING IS TOUCHED.
//
// This is a subclass in its own file. ACM_NightVisionComponent, ACM_ThermalComponentV2 and
// ACM_HeatSourceComponent are all unmodified, and none of this code is reachable until a prefab
// asks for it by name. Trying the rig is a component swap on one NVG device; abandoning it is the
// same swap in reverse.
//
// WHAT TO WATCH FOR IN FLIGHT.
//
//   - Bodies glow but stay dull and body-shaped: the clothing materials were not reached. The
//     diagnostic reports materials per body; a total equal to the painted count is one material
//     each, which is the naked base body with no loadout.
//   - Nothing glows: turn on Tint Albedo on the signature component. Albedo always renders, so if
//     colour changes with it on and nothing happens with it off, the emissive path is at fault and
//     not the material walk.
//   - Bodies read through walls: expected for now. There is no occlusion test in this pass. A real
//     thermal is stopped by a wall but not by darkness, and which of those to model is a decision
//     worth taking deliberately rather than inheriting by accident.

//------------------------------------------------------------------------------------------------
// HOW A BODY IS MADE TO READ HOT.
//
// SCOPE_HIGHLIGHT CANNOT DO WHITE. That is arithmetic, not a bug, and it is worth writing down
// because it wasted several attempts.
//
// Light() drives two things. SetColor is an albedo TINT, and a tint multiplies - multiplying by
// white is the identity operation, so a white tint changes nothing by definition. SetEmissiveColor
// needs an emission channel the character materials do not have. So of the two mechanisms Light()
// uses, white is precisely the value that makes the first a no-op and the second is dead anyway.
//
// This is why the scope highlight looks like it works and thermal looked like it did not: the scope
// passes a SATURATED contact colour, which the tint can express. Raising the multiplier from 8 to
// 24 changed nothing for the same reason - scaling a no-op is still a no-op. The mode is kept
// because it is genuinely correct for a coloured signature, and useless for a white one.
//
// SCOPE_HIGHLIGHT, for the record, is not new code. It is the same
// ACM_ScopedHighlightComponent.Light() call the IVAS scope already makes to light a contact through
// a magnified optic, handed white instead of the contact colour.
//
// It was written off far too early. The first attempt looked like it did nothing, and the
// conclusion drawn was that character materials have no emission channel - which is true, and was
// not the reason. The reason was the MULTIPLIER. This ran at 8; the scope runs at 24, a value
// somebody had already dialled in by looking through the optic until contacts read against the
// background. The mechanism was right the whole time and merely turned down too far to see.
//
// The lesson is worth keeping: a mechanism producing no visible output is not evidence that the
// mechanism is wrong. There was an existing, working, already-tuned caller of this exact function
// sitting in the same codebase, and its parameters were the whole answer.
//
// ALBEDO is a tint, and tints MULTIPLY. That is not a limitation to work around, it is a hard
// ceiling: multiplying can only ever darken. Tinting a camo uniform white leaves it a camo uniform,
// which is exactly what the flight test showed - nine bodies painted, sixty-three materials
// written, and no visible change anywhere. The scoped highlight gets away with albedo only because
// it tints a SATURATED contact colour, and shifting a body's hue is a different job from making it
// bright. Useful as a diagnostic - if albedo shows and emissive does not, the material walk is fine
// and only the emission channel is missing - and useless as a heat signature.
//
// OUTLINE works, and works as the wrong thing. It is the engine ESP outliner - a hard stencil edge
// traced round a silhouette, rendered through geometry. It says "a person is there", which is what
// a target marker says. It is not a heat signature, the body inside it is untouched, and dressing a
// wallhack up as a sensor is not the feature. Kept because it is genuinely useful for confirming
// the pass is selecting the right bodies.
//
// MATERIAL IS DESTRUCTIVE. DO NOT SHIP IT AS IT STANDS.
//
// SCR_Global.SetMaterial replaces a mesh's material and there is NO getter to read what was there
// first, so there is nothing to put back. Clearing with an empty ResourceName does not restore the
// original - it leaves the mesh with no material at all, and for a character that means the naked
// base body, normally hidden under clothing by a clipping map in ITS OWN material, renders straight
// through the uniform. It looks like a second body, it survives the goggles being switched off, and
// only respawning the character fixes it.
//
// Adding the base body's clipping map to the replacement material does not solve this. It patches
// the symptom while the swap is active and does nothing about the state left behind afterwards,
// because the damage is the missing original, not the missing map.
//
// This is a one-way operation dressed up as a toggle, and the lifetime machinery around it -
// expiry, sweep, StripAllSkins, OnDelete - is all worthless when "restore" cannot actually restore.
// ACM_ScopedHighlightComponent by contrast reads GetColor/GetEmissiveC/GetEmissiveM before writing
// and puts those exact values back, which is why it is safe and this is not.
//
// Kept selectable ONLY for experiments on throwaway characters. It needs a real restore path -
// most likely the original ResourceName recorded per mesh on the character prefab - before it is
// worth anything.
//
// MATERIAL was meant to be the one that does what was actually asked. SCR_Global.SetMaterial swaps an entity's
// material outright, recursively through its hierarchy - the same mechanism vanilla's editor uses
// to recolour entities by state. Point it at an .emat that HAS emission enabled and the body is
// genuinely emissive, because the material under it now genuinely has an emissive channel. That is
// the difference from the EMISSIVE mode above: rather than writing a parameter into a shader with
// nowhere to put it, this replaces the shader with one that does.
//
// It is also the only mode here that changes what the mesh is made of rather than how it is tinted,
// so it is the only one that can produce a white-hot bloom instead of a recolour or an edge. Its
// drawback is that every adjustment means editing the .emat and reimporting it.
//
// MATERIAL_TUNED is the two put together, and it is NOT usually the one to use.
//
// It sounded like the answer - live sliders instead of an asset round-trip - and it is genuinely
// worse at the only thing that matters here, which is precision at the very bottom of the range.
// SetEmissiveMultiplier is an int, and packed colour is 8 bits a channel, so the finest step script
// can express is about 1/255 of a multiplier unit. The .emat takes Emissive as FLOATS: the
// glowstick sits at 0.027 and reads correctly, a value the script path cannot even represent.
//
// So MATERIAL is the default and the material is the tuning surface. MATERIAL_TUNED stays for the
// case where something must change at runtime - a signature that fades with distance, or cools over
// time - which the .emat cannot do at all.
//
// Swap the material FIRST so the body is made of something with an emission channel, then drive
// that channel with SetEmissiveColor and SetEmissiveMultiplier. The same two calls that did nothing
// all session - because a cloth shader had nowhere to put them - now land on a material built to
// receive them. Colour and brightness become live component sliders instead of an asset round-trip,
// and the .emat stops being a tuning surface and becomes a blank canvas that just needs emission
// switched on.
//
// It also survives the HDR colorisation LUT, which the tint modes do not. A monochrome ramp maps
// LUMINANCE to one hue and discards the rest, so a red TINT reads as a dark body. A red EMISSIVE is
// bright, and brightness is the one property such a ramp preserves.
///------------------------------------------------------------------------------------------------
// One selectable signature material.
//
// A LIST rather than a slider, and that is forced by the API rather than chosen. Every setter on
// ParametricMaterialInstanceComponent takes an INT - SetColor, SetEmissiveColor,
// SetEmissiveMultiplier, SetUserParam1/2 - so the finest step script can express is roughly 1/255
// of a multiplier unit. An .emat takes Emissive as FLOATS, and the glowstick in this very project
// reads correctly at 0.027, a value the script path cannot even represent.
//
// So the emat keeps the precision and the component keeps the control: author as many graded
// materials as the job needs and pick between them here, in the editor or in flight. Wanting a step
// between two presets means adding a material, not hunting for a finer slider that does not exist.
//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true)]
class ACM_ThermalSignatureMaterial
{
	[Attribute("Signature", UIWidgets.EditBox, desc: "Name shown in the radial when cycling.")]
	string m_sName;

	[Attribute("", UIWidgets.ResourceNamePicker, desc: "Emissive .emat swapped onto a body at this step. Tune the Emissive floats inside the material - that is where the fine control lives.", "emat")]
	ResourceName m_Material;

	string GetName() { return m_sName; }
	ResourceName GetMaterial() { return m_Material; }
}

//------------------------------------------------------------------------------------------------
enum ACM_EThermalSignatureMode
{
	SCOPE_HIGHLIGHT,
	ALBEDO,
	OUTLINE,
	ALBEDO_AND_OUTLINE,
	MATERIAL,
	MATERIAL_TUNED
}

[ComponentEditorProps(category: "ACM/Thermal", description: "TEST. Real thermal built on the NVG component - uses the NVG HDR stack and paints characters as emissive heat signatures. Swap in place of ACM_NightVisionComponent on one device to trial it.")]
class ACM_ThermalTestComponentClass : ACM_NightVisionComponentClass
{
}

class ACM_ThermalTestComponent : ACM_NightVisionComponent
{
	[Attribute("0", UIWidgets.CheckBox, desc: "Skip the HDR colorisation pass entirely, leaving the scene rendered normally with only the bodies treated. Use it to prove what the LUT is doing: a monochrome ramp CANNOT show a red body, because it maps luminance to one hue and throws the rest away. Turn this on and a red signature appears instantly; turn it off and the same red goes dark.", category: "ACM Thermal Test - Optics")]
	protected bool m_bNoHDRColorisation;

	[Attribute("", UIWidgets.ResourceNamePicker, desc: "HDR colorisation material for the THERMAL image. Leave empty and the device renders with the inherited NVG phosphor material instead, which is why an unconfigured test rig looks exactly like ordinary night vision. Point this at a white-hot .emat to get a thermal picture.", "emat", category: "ACM Thermal Test - Optics")]
	protected ResourceName m_ThermalHDRMaterial;

	[Attribute("4", UIWidgets.ComboBox, desc: "How a body is made to read hot. MATERIAL is the default: the .emat alone decides, and its Emissive floats are a far finer dial than anything script can reach - SetEmissiveMultiplier is an INT and packed colour is 8-bit, so the material wins on precision by a wide margin. MATERIAL_TUNED adds live component control at that coarser resolution: swap to an emissive material, then drive its emission from Heat Colour and Emissive Strength below, so both are tunable here rather than in the .emat. SCOPE_HIGHLIGHT is the plain scope call: the same ACM_ScopedHighlightComponent.Light() the IVAS optic uses, driven by thermal instead of by the scope. It tints, so it needs a SATURATED Heat Colour - red works, white cannot. MATERIAL replaces the material outright and is the only way to get white: the exact call the IVAS scope already makes to light a contact, in white instead of the contact colour. MATERIAL swaps the whole material for an emissive one - brighter and blunter. See the note above the enum.", category: "ACM Thermal Test - Signature", enums: ParamEnumArray.FromEnum(ACM_EThermalSignatureMode))]
	protected ACM_EThermalSignatureMode m_eSignatureMode;

	[Attribute("", UIWidgets.Auto, desc: "Graded signature materials, dimmest first. Cycle them in flight from the radial to find the level that reads without overwhelming, then leave it there. Precision lives in each .emat's Emissive floats, which are finer than anything script can set.", category: "ACM Thermal Test - Signature")]
	protected ref array<ref ACM_ThermalSignatureMaterial> m_aSignatureMaterials;

	[Attribute("0", UIWidgets.Slider, desc: "Which entry of Signature Materials to start on.", category: "ACM Thermal Test - Signature", params: "0 31 1")]
	protected int m_iSignatureMaterialIndex;

	[Attribute("{1EA97A532BE828DF}Assets/Thermal/Thermal_White.emat", UIWidgets.ResourceNamePicker, desc: "FALLBACK material, used only when Signature Materials is empty. It MUST be an .emat with emission enabled - that is the entire point, and a non-emissive material here will look like the mode does nothing. Empty disables the swap and the diagnostic says so.", "emat", category: "ACM Thermal Test - Signature")]
	protected ResourceName m_ThermalBodyMaterial;

	[Attribute("0.55", UIWidgets.Slider, desc: "Outline thickness.", category: "ACM Thermal Test - Signature", params: "0.02 1 0.01")]
	protected float m_fOutlineThickness;

	[Attribute("1 0 0", UIWidgets.ColorPicker, desc: "Heat signature colour. In MATERIAL_TUNED this is the EMISSIVE colour on the swapped material, so any hue works including white, and it is live. Must be SATURATED for SCOPE_HIGHLIGHT: that mode tints the albedo, tints multiply, and multiplying by white changes nothing at any strength. Red reads as heat against the cold blue of the thermal picture and is what the tint can actually express.", category: "ACM Thermal Test - Signature")]
	protected ref Color m_HeatColor;

	[Attribute("1.0", UIWidgets.Slider, desc: "EMISSIVE STRENGTH. The live brightness dial for MATERIAL_TUNED, in fine steps - 0.001 at the bottom end where the useful range actually is. Emission compounds with the goggles' own HDR brightness, so a body goes from invisible to blinding across a very small span and a whole-number slider steps straight over it.", category: "ACM Thermal Test - Signature", params: "0.001 64 0.001")]
	protected float m_fEmissiveStrength;

	[Attribute("300", UIWidgets.Slider, desc: "How far heat signatures are painted, metres. Not a detection range - a cost limit. Beyond it a body is a couple of pixels and painting it buys nothing.", category: "ACM Thermal Test - Signature", params: "10 1000 10")]
	protected float m_fSignatureRange;

	[Attribute("48", UIWidgets.Slider, desc: "Most bodies painted per pass. A hard ceiling so a crowded objective cannot turn one frame into a stall.", category: "ACM Thermal Test - Signature", params: "1 128 1")]
	protected int m_iMaxSignatures;

	[Attribute("1", UIWidgets.CheckBox, desc: "Log the candidate, painted and material counts once per second while active.", category: "ACM Thermal Test - Diagnostics")]
	protected bool m_bSignatureDiagnostics;

	//! How often signatures are repainted. Bodies move and enter range; nothing needs this per frame.
	protected static const float REPAINT_INTERVAL = 0.1;

	//! How long a painted signature survives unrefreshed. Comfortably longer than the repaint
	//! interval, so one dropped frame does not make the whole field flicker.
	protected static const float SIGNATURE_HOLD = 0.5;

	protected float m_fRepaintTimer;
	protected float m_fNextSignatureDiag;
	protected bool m_bPainting;

	protected ref array<IEntity> m_aSignatureQuery = {};

	//! Bodies currently wearing the swapped material. A material override is STATE, not a per-frame
	//! submission, so it is applied once on entry and cleared once on exit - and it must be tracked,
	//! because an entity that walks out of range with a thermal skin on keeps it forever otherwise.
	protected ref array<IEntity> m_aMaterialised = {};

	//------------------------------------------------------------------------------------------------
	// THE POST-PROCESS STACK IS THE PARENT'S, UNCHANGED.
	//
	// super does the whole job - slot 11 cleared, grain into 9 and 8, the colorisation material into
	// slot 16 as HDR, SetCameraHDRBrightness, the brightness listeners, device visibility, revert on
	// the way back out. Reimplementing any of that here would mean two methods with opinions about
	// who owns the camera, which is the kind of argument that surfaces three weeks later as an
	// effect that will not switch off.
	//
	// The only thing added is who is painting.
	//------------------------------------------------------------------------------------------------
	override void ApplyVisualEffect(bool enable, bool force = false, bool touchDetection = true)
	{
		super.ApplyVisualEffect(enable, force, touchDetection);

		if (enable && m_bIsWorn)
			StartPainting();
		else
			StopPainting();
	}

	//------------------------------------------------------------------------------------------------
	//! The material for the current step, falling back to the single-slot attribute when no presets
	//! are configured so an existing setup keeps working untouched.
	protected ResourceName GetSignatureMaterial()
	{
		if (m_aSignatureMaterials && !m_aSignatureMaterials.IsEmpty())
		{
			int index = ValidSignatureIndex(m_iSignatureMaterialIndex);

			ACM_ThermalSignatureMaterial entry = m_aSignatureMaterials[index];
			if (entry && !entry.GetMaterial().IsEmpty())
				return entry.GetMaterial();
		}

		return m_ThermalBodyMaterial;
	}

	//------------------------------------------------------------------------------------------------
	protected int ValidSignatureIndex(int requested)
	{
		if (!m_aSignatureMaterials || m_aSignatureMaterials.IsEmpty())
			return 0;

		if (requested < 0)
			return 0;

		if (requested >= m_aSignatureMaterials.Count())
			return m_aSignatureMaterials.Count() - 1;

		return requested;
	}

	//------------------------------------------------------------------------------------------------
	int GetSignatureMaterialCount()
	{
		if (!m_aSignatureMaterials)
			return 0;

		return m_aSignatureMaterials.Count();
	}

	//------------------------------------------------------------------------------------------------
	array<string> GetSignatureMaterialNames()
	{
		array<string> names = {};

		if (!m_aSignatureMaterials)
			return names;

		foreach (ACM_ThermalSignatureMaterial entry : m_aSignatureMaterials)
		{
			if (entry)
				names.Insert(entry.GetName());
		}

		return names;
	}

	//------------------------------------------------------------------------------------------------
	int GetSignatureMaterialIndex() { return m_iSignatureMaterialIndex; }

	//------------------------------------------------------------------------------------------------
	//! Change step. Every body already wearing a skin is stripped so the next pass re-applies the
	//! new one - without that, everyone lit before the change keeps the old material forever and the
	//! selector appears to do nothing until they walk out of range and back.
	void SetSignatureMaterial(int index)
	{
		int next = ValidSignatureIndex(index);
		if (next == m_iSignatureMaterialIndex)
			return;

		m_iSignatureMaterialIndex = next;
		StripAllSkins();
	}

	//------------------------------------------------------------------------------------------------
	void CycleSignatureMaterial()
	{
		int count = GetSignatureMaterialCount();
		if (count <= 1)
			return;

		SetSignatureMaterial((m_iSignatureMaterialIndex + 1) % count);
	}

	//------------------------------------------------------------------------------------------------
	// THE PICTURE.
	//
	// The parent asks this for whatever goes into slot 16 as HDR, so overriding it is all it takes
	// to change what the world looks like through the device - no copy of ApplyVisualEffect, no
	// second opinion about who owns the camera.
	//
	// Empty falls through to the inherited phosphor material, and that fallback is the reason an
	// unconfigured test rig looks EXACTLY like ordinary night vision: green scene, no thermal
	// treatment, and the only difference is bodies that glow. If the picture is green, this
	// attribute is empty.
	//------------------------------------------------------------------------------------------------
	override protected ResourceName GetCurrentPhosphorMaterial()
	{
		// Empty means the parent never writes slot 16, so no colorisation is applied at all.
		if (m_bNoHDRColorisation)
			return "";

		if (!m_ThermalHDRMaterial.IsEmpty())
			return m_ThermalHDRMaterial;

		return super.GetCurrentPhosphorMaterial();
	}

	//------------------------------------------------------------------------------------------------
	// The parent's Update is engine-driven by the gadget system, so this rides the same tick as
	// everything else on the device.
	//------------------------------------------------------------------------------------------------
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);

		if (!m_bPainting)
			return;

		// The parent owns activation. If it went off underneath us - unequipped, stowed, killed,
		// menu opened - stop painting rather than leaving a field of bodies lit.
		if (!IsNVGActive() || !m_bIsWorn)
		{
			StopPainting();
			return;
		}

		// The outliner is a per-frame submission, not a state you set once - reissued every tick or
		// the silhouettes strobe at ten hertz. The material write IS state, so in the modes that use
		// only materials the cheaper interval still applies.
		bool outlining = (m_eSignatureMode == ACM_EThermalSignatureMode.OUTLINE || m_eSignatureMode == ACM_EThermalSignatureMode.ALBEDO_AND_OUTLINE);

		if (!outlining)
		{
			m_fRepaintTimer -= timeSlice;
			if (m_fRepaintTimer > 0)
				return;

			m_fRepaintTimer = REPAINT_INTERVAL;
		}

		PaintSignatures();
	}

	//------------------------------------------------------------------------------------------------
	//! Every character in range reads hot. No faction test, and that is not an oversight.
	//!
	//! A thermal sight detects temperature. It does not know whose side anyone is on, and a version
	//! that lit only enemies would be a wallhack wearing a sensor's clothes. It would also be a far
	//! worse instrument for a trial: with only enemies painted there is no way to tell a working
	//! emissive pass from a working faction filter, so when nothing glows you would not know which
	//! of the two to go and look at.
	protected void PaintSignatures()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		vector camMat[4];
		world.GetCurrentCamera(camMat);
		vector camPos = camMat[3];

		m_aSignatureQuery.Clear();
		world.QueryEntitiesBySphere(camPos, m_fSignatureRange, AddSignatureCandidate, FilterSignatureCandidate, EQueryEntitiesFlags.ALL);

		// Two statements, not one. Enforce will not accept a method call chained straight onto a
		// `new` expression - `new Color(...).PackToInt()` is a broken expression to the parser, and
		// it took the whole Game module down with it. A module that will not compile disables the
		// radial menu, every gadget toggle and every other system at once, which presents as "the
		// device will not switch on" and is nothing of the sort. Worth the extra line.
		Color heat = m_HeatColor;
		if (!heat)
			heat = new Color(1, 1, 1, 1);

		// SPLITTING ONE FLOAT ACROSS TWO INTEGER-ISH CONTROLS.
		//
		// SetEmissiveMultiplier takes an INT, so on its own the smallest step it can express is 1 -
		// and 1 is already past the point where a body blows out. All the useful range sits below
		// the first notch, which is why every attempt to tune this by the multiplier alone jumped
		// from nothing to a sun.
		//
		// The colour carries the remainder. Emission is colour multiplied by multiplier, so scaling
		// the colour down by the fractional part gives a continuous dial: the multiplier takes the
		// whole number, the colour takes what is left over.
		//
		// The honest limit: packed colour is 8 bits a channel, so the finest real step is about
		// 1/255 of one multiplier unit - roughly 0.004, not 0.001. The slider accepts thousandths
		// and several adjacent values will render identically. That is the hardware, not the code,
		// and it is far finer than the integer alone could ever be.
		float strength = m_fEmissiveStrength;
		if (strength < 0)
			strength = 0;

		int mult = Math.Ceil(strength);
		if (mult < 1)
			mult = 1;

		float scale = strength / mult;

		Color emissive = new Color(heat.R() * scale, heat.G() * scale, heat.B() * scale, 1);
		int packed = emissive.PackToInt();

		// OutlineEntity is declared on ChimeraWorld, not BaseWorld, and takes a Color rather than a
		// packed int - the detection system's outline pass is the reference for both.
		ChimeraWorld outlineWorld = null;
		if (m_eSignatureMode == ACM_EThermalSignatureMode.OUTLINE || m_eSignatureMode == ACM_EThermalSignatureMode.ALBEDO_AND_OUTLINE)
			outlineWorld = ChimeraWorld.CastFrom(GetGame().GetWorld());

		bool materialising = ((m_eSignatureMode == ACM_EThermalSignatureMode.MATERIAL || m_eSignatureMode == ACM_EThermalSignatureMode.MATERIAL_TUNED) && !GetSignatureMaterial().IsEmpty());

		// Every mode except plain MATERIAL drives the parametric component. MATERIAL leaves the
		// swapped material exactly as authored, so the .emat is the only thing deciding how it looks.
		bool tinting = (m_eSignatureMode != ACM_EThermalSignatureMode.MATERIAL);

		array<IEntity> stillHot = {};

		int painted = 0;
		int materials = 0;

		foreach (IEntity candidate : m_aSignatureQuery)
		{
			if (painted >= m_iMaxSignatures)
				break;

			if (!candidate || candidate == m_CharacterOwner)
				continue;

			ACM_ScopedHighlightComponent signature = ACM_ScopedHighlightComponent.Cast(candidate.FindComponent(ACM_ScopedHighlightComponent));
			if (!signature)
				continue;

			// SWAP FIRST, THEN TUNE. Order matters and it is the whole point of MATERIAL_TUNED.
			//
			// SetEmissiveColor/SetEmissiveMultiplier write into whatever material is bound at the
			// time. Written to a character's own cloth material they go nowhere, because that
			// material has no emission channel. Written AFTER the swap they land on a material that
			// does - so the same two calls that did nothing all session become live sliders.
			//
			// The parametric component is per-ENTITY, not per-material, so nothing needs
			// re-resolving across the swap: it keeps driving whatever is currently bound.
			if (materialising && m_aMaterialised.Find(candidate) == -1)
			{
				// See the DESTRUCTIVE warning above the enum. This cannot be undone.

				SCR_Global.SetMaterial(candidate, GetSignatureMaterial(), true);
				m_aMaterialised.Insert(candidate);
			}

			if (materialising)
				stillHot.Insert(candidate);

			if (tinting && !signature.Light(packed, mult, SIGNATURE_HOLD))
				continue;

			if (outlineWorld)
				outlineWorld.OutlineEntity(candidate, heat, 1.0, m_fOutlineThickness, true);

			painted++;
			materials += signature.GetMaterialCount();
		}

		// Anyone who was wearing the thermal skin and did not survive this pass - walked out of
		// range, died, despawned - gets their own material back. Without this, one sprint out of
		// range leaves a permanently glowing soldier and no code path left that would ever fix him.
		if (materialising)
		{
			for (int i = m_aMaterialised.Count() - 1; i >= 0; i--)
			{
				IEntity worn = m_aMaterialised[i];

				if (worn && stillHot.Find(worn) != -1)
					continue;

				if (worn)
					SCR_Global.SetMaterial(worn, "", true);

				m_aMaterialised.Remove(i);
			}
		}

		if (!m_bSignatureDiagnostics)
			return;

		float now = System.GetTickCount() / 1000.0;
		if (now < m_fNextSignatureDiag)
			return;

		m_fNextSignatureDiag = now + 1.0;

		// Candidates against painted separates "nobody is out there" from "everybody is out there
		// and none of them carry the signature component". Those are the same picture through the
		// goggles and completely different bugs. Materials is the third: a total equal to the
		// painted count means one material each, which is a naked base body and no clothing.
		string note = "";
		if (m_aSignatureQuery.IsEmpty())
			note = "  <- no characters in range";
		else if (painted == 0)
			note = "  <- characters found but NONE carry ACM_ScopedHighlightComponent (add it to the base character prefab)";
		else if (materials <= painted)
			note = "  <- one material per body: that is the naked base body, the loadout was not reached";
		else if (m_eSignatureMode == ACM_EThermalSignatureMode.MATERIAL_TUNED && GetSignatureMaterial().IsEmpty())
			note = "  <- MATERIAL_TUNED with no Thermal Body Material set: there is nothing with an emission channel to drive, so the sliders do nothing.";
		else if (m_eSignatureMode == ACM_EThermalSignatureMode.SCOPE_HIGHLIGHT && !m_bNoHDRColorisation)
			note = "  <- tinting under an HDR colorisation LUT. A monochrome ramp maps LUMINANCE to one hue and discards the rest, so a red tint reads as a DARK body, not a red one. Tick No HDR Colorisation to see the tint's real colour.";
		else if (m_eSignatureMode == ACM_EThermalSignatureMode.ALBEDO)
			note = "  <- ALBEDO mode: tints multiply, so this can only darken. It cannot make a body read hot.";
		else if (m_eSignatureMode == ACM_EThermalSignatureMode.MATERIAL && m_ThermalBodyMaterial.IsEmpty())
			note = "  <- MATERIAL mode with no Thermal Body Material set. Point it at an .emat that has emission enabled.";

		Print(string.Format("[ACM THERMAL TEST] candidates=%1 painted=%2 materials=%3 strength=%4 (mult=%5 scale=%6)%7",
			m_aSignatureQuery.Count(), painted, materials, m_fEmissiveStrength, mult, scale, note), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected bool FilterSignatureCandidate(IEntity entity)
	{
		// Runs for every entity the broadphase touches, so it stays as cheap as it looks.
		return ChimeraCharacter.Cast(entity) != null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool AddSignatureCandidate(IEntity entity)
	{
		if (entity)
			m_aSignatureQuery.Insert(entity);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void StartPainting()
	{
		if (m_bPainting)
			return;

		m_bPainting = true;
		m_fRepaintTimer = 0;
		m_fNextSignatureDiag = 0;

		// Announced at NORMAL, not DEBUG, and deliberately.
		//
		// The failure this rig is most likely to hit is silence: the device switches on, the picture
		// looks like night vision, and nothing anywhere says whether the paint pass is running, or
		// running and finding nobody, or not running at all. Those need telling apart from the
		// console before anyone starts changing materials.
		string hdr = "phosphor (NVG fallback)";
		if (!m_ThermalHDRMaterial.IsEmpty())
			hdr = m_ThermalHDRMaterial;

		Print("[ACM THERMAL TEST] painting started, hdr=" + hdr, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Everything cools at once. Letting signatures lapse on their own expiry would leave the field
	//! glowing for half a second after the goggles come off, and a body still white-hot with the
	//! device stowed is the most alarming way this could fail.
	protected void StopPainting()
	{
		if (!m_bPainting)
			return;

		m_bPainting = false;
		ACM_ScopedHighlightManager.GetInstance().ExtinguishAll();
		StripAllSkins();

		Print("[ACM THERMAL TEST] painting stopped", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// THE TOGGLE HAS TO APPLY ITS OWN STATE.
	//
	// The parent's OnToggleInput deliberately does NOT set m_bActivated. It only calls
	// ToggleActive(), and lets the gadget system call OnToggleActive back to apply the state - which
	// is correct, and is what stopped one keypress running the effect and the animation twice.
	//
	// It is correct only for the FIRST gadget of a type on an entity, because that is the only one
	// the manager calls back. This component reports the same type as the night vision component it
	// derives from, so on a device carrying both, ToggleActive is a request that is answered by
	// somebody else. The state is never applied, nothing visible happens, and the menu entry looks
	// dead while the code behind it runs perfectly.
	//
	// ACM_ThermalComponentV2 hit this exact wall and solved it by applying its own state before
	// calling ToggleActive. Same wall, same solution - deliberately, so there is one pattern to
	// understand rather than two.
	//------------------------------------------------------------------------------------------------
	override protected void OnToggleInput()
	{
		if (m_bMenuOpen)
			return;

		if (!m_CharacterOwner)
			return;

		if (m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		m_bIsWorn = CheckIsWorn();
		if (!m_bIsWorn)
			return;

		bool nextState = !m_bActivated;

		if (nextState)
		{
			// One camera, one owner. Night vision and this rig both write postprocess slot 16, so
			// leaving the goggles running underneath would mean two components overwriting each
			// other's HDR material every frame - which reads as flicker, not as a conflict, and
			// would be blamed on the thermal shader.
			// Looked up on the DEVICE, not on m_CharacterOwner. The optics components live on the
			// goggles; the character only wears them.
			IEntity device = GetOwner();
			if (device)
			{
				ACM_NightVisionComponent nvg = ACM_NightVisionComponent.Cast(device.FindComponent(ACM_NightVisionComponent));
				if (nvg && nvg != this && nvg.IsNVGActive())
					nvg.ToggleNVG();

				ACM_ThermalComponentV2 thermal = ACM_ThermalComponentV2.Cast(device.FindComponent(ACM_ThermalComponentV2));
				if (thermal && thermal.IsThermalActive())
					thermal.ToggleThermal();
			}
		}

		m_bActivated = nextState;

		Print("[ACM THERMAL TEST] toggled -> " + m_bActivated, LogLevel.NORMAL);

		ApplyVisualEffect(m_bActivated);
		UpdateAnimation();
		ToggleActive(m_bActivated, SCR_EUseContext.CUSTOM);
	}

	//------------------------------------------------------------------------------------------------
	// DRIVEN FROM THE HOST NVG COMPONENT.
	//
	// This component reports the same EGadgetType as ACM_NightVisionComponent, because it IS one.
	// The gadget manager routes mode changes to only the FIRST gadget of a type on an entity, so on
	// a device that carries both, this one receives ModeClear but never ModeSwitch. No ModeSwitch
	// means no ActivateGadgetUpdate, so no Update, so the paint pass never runs and the toggle never
	// reaches us - the component is loaded, constructed, and completely inert.
	//
	// That is the exact failure ACM_NightVisionComponent already documents and already solves for
	// ACM_ThermalComponentV2, so this uses the same solution: the component the manager really
	// drives hands us the events we should have received. On a device carrying only this component
	// the manager drives us directly and none of this is used.
	//------------------------------------------------------------------------------------------------
	void HostUpdate(float timeSlice)
	{
		Update(timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	void HostModeSwitch(EGadgetMode mode, IEntity charOwner)
	{
		ModeSwitch(mode, charOwner);
	}

	//------------------------------------------------------------------------------------------------
	void HostModeClear(EGadgetMode mode)
	{
		ModeClear(mode);
	}

	//------------------------------------------------------------------------------------------------
	//! Every swapped material handed back at once.
	//!
	//! This is the one that matters most. A highlight left on is a cosmetic bug; a character left
	//! wearing a thermal skin after the goggles come off is a soldier who glows to everyone,
	//! permanently, with nothing left running that would ever undo it.
	protected void StripAllSkins()
	{
		foreach (IEntity worn : m_aMaterialised)
		{
			if (worn)
				SCR_Global.SetMaterial(worn, "", true);
		}

		m_aMaterialised.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Last line of defence. If the device is destroyed mid-frame nothing is left to run the
	//! repaint, and without this the bodies it lit stay lit for the rest of the session.
	override void OnDelete(IEntity owner)
	{
		// StopPainting hands every swapped material back, which is exactly what must happen here -
		// the device is being destroyed and nothing else will ever run to undo the skins it applied.
		StopPainting();
		super.OnDelete(owner);
	}
}
