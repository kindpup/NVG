//------------------------------------------------------------------------------------------------
// ACM_RemoteFeedProfile.c
// Authored link characteristics for one remote camera source.
//
// These are TUNING VALUES, not rules. Nothing in the evaluator or the renderer branches on what
// kind of source this is - a UAV and a K9 differ only by the numbers below, which is what makes a
// new camera source a new profile rather than a new renderer branch.
//
// Deliberately separate from the sensor profile that Phase 4.2 will introduce. Video link quality
// and tactical detection are different capabilities on the same airframe, and a drone may hold a
// contact perfectly well on a picture too poor to watch.
//------------------------------------------------------------------------------------------------

[BaseContainerProps(configRoot: true)]
class ACM_RemoteFeedProfile
{
	//------------------------------------------------------------------------------------------------
	// Range
	//------------------------------------------------------------------------------------------------
	[Attribute("600", UIWidgets.Slider, desc: "Distance in metres within which the link is at full strength. Distance costs nothing at all below this.", params: "10 3000 10")]
	float optimalRange;

	[Attribute("1500", UIWidgets.Slider, desc: "Distance in metres beyond which the link is unusable regardless of everything else. Between optimal and this, quality falls on a curve rather than a cliff.", params: "20 6000 10")]
	float maxRange;

	[Attribute("2.0", UIWidgets.Slider, desc: "Shape of the falloff between optimal and maximum range. 1 is linear; higher values hold quality longer and then drop off sharply, which is what a real link does.", params: "1 4 0.1")]
	float distanceFalloffPower;

	[Attribute("0.25", UIWidgets.Slider, desc: "How much quality is lost by the time the source reaches optimal range. Without this the link sits at a flat 100% for the whole of normal engagement distance and the readout never moves - a link should visibly weaken as the source gets further away, not only once it is nearly out of reach.", params: "0 0.6 0.01")]
	float nearFalloff;

	[Attribute("1.4", UIWidgets.Slider, desc: "Shape of that near loss. Below 1 it bites immediately; above 1 it stays generous up close and steepens toward optimal range.", params: "0.5 3 0.1")]
	float nearFalloffPower;

	[Attribute("1.0", UIWidgets.Slider, desc: "Overall sensitivity. Scales every loss term at once - distance, obstruction and equipment - so a whole scenario can be made harsher or more forgiving without re-tuning each source.", params: "0.25 3 0.05")]
	float sensitivity;

	//------------------------------------------------------------------------------------------------
	// Obstruction
	//------------------------------------------------------------------------------------------------
	[Attribute("0.45", UIWidgets.Slider, desc: "How much obstruction costs this link. The single most important difference between an airborne source and a ground one: a UAV usually has sky between it and the viewer, while a dog is at ankle height with walls in the way, so the same obstruction hurts it far more.", params: "0 1 0.05")]
	float obstructionWeight;

	[Attribute("1", UIWidgets.CheckBox, desc: "Whether altitude improves this link. Meaningless for a ground source. Note that height already improves the obstruction result on its own, so this is a small explicit bonus rather than the main effect.")]
	bool altitudeHelps;

	[Attribute("120", UIWidgets.Slider, desc: "Height above the viewer, in metres, at which the altitude bonus is fully earned. Ignored when altitude does not help this source.", params: "10 500 10")]
	float altitudeFullBonusMetres;

	[Attribute("0.15", UIWidgets.Slider, desc: "How much a fully earned altitude bonus adds back.", params: "0 0.5 0.01")]
	float altitudeBonus;

	//------------------------------------------------------------------------------------------------
	// Equipment
	//------------------------------------------------------------------------------------------------
	[Attribute("1.0", UIWidgets.Slider, desc: "Multiplier for this source's transmitter. 1.0 is a purpose-built airframe; a harness-mounted camera is worse.", params: "0.1 1.5 0.05")]
	float antennaGain;

	[Attribute("1", UIWidgets.CheckBox, desc: "Whether this source needs power to transmit. When set and the source reports itself unpowered, quality is zero before any raycast is spent.")]
	bool requiresPower;
}

//------------------------------------------------------------------------------------------------
// Built-in defaults, so a source with no authored profile still behaves sensibly rather than
// falling back to zeroes and reading as a permanently dead link.
//------------------------------------------------------------------------------------------------
class ACM_RemoteFeedProfileDefaults
{
	//------------------------------------------------------------------------------------------------
	static ACM_RemoteFeedProfile For(ACM_ERemoteFeedSourceType type)
	{
		ACM_RemoteFeedProfile profile = new ACM_RemoteFeedProfile();

		// Attribute defaults only apply to AUTHORED instances. One built in script starts at zero
		// for every field, and a zero falloff power or sensitivity produces nonsense rather than a
		// gentle default - so the shared values are set explicitly here.
		profile.distanceFalloffPower = 2.0;
		profile.nearFalloff = 0.25;
		profile.nearFalloffPower = 1.4;
		profile.sensitivity = 1.0;
		profile.altitudeFullBonusMetres = 120;
		profile.altitudeBonus = 0.15;
		profile.requiresPower = true;

		if (type == ACM_ERemoteFeedSourceType.K9)
		{
			// Short range, and obstruction hurts badly: the transmitter is at ankle height with
			// walls between it and the handler more often than not.
			profile.optimalRange = 120;
			profile.maxRange = 300;
			profile.obstructionWeight = 0.85;
			profile.altitudeHelps = false;
			profile.antennaGain = 0.7;
			return profile;
		}

		if (type == ACM_ERemoteFeedSourceType.SOLDIER)
		{
			// IVAS to IVAS. Both ends are man-portable, both are at head height, and both are
			// usually indoors or behind something - so this degrades harder than a dog, whose
			// handler at least tends to be looking at it. Short optimal range on purpose: two
			// soldiers a building apart should not have a clean picture of each other.
			profile.optimalRange = 150;
			profile.maxRange = 500;
			profile.obstructionWeight = 0.80;
			profile.altitudeHelps = false;
			profile.antennaGain = 0.85;
			profile.nearFalloff = 0.30;
			return profile;
		}

		if (type == ACM_ERemoteFeedSourceType.STATIC || type == ACM_ERemoteFeedSourceType.MAST)
		{
			// A fixed installation is usually wired or on a good mast - long reach, and it does not
			// climb.
			profile.optimalRange = 900;
			profile.maxRange = 2000;
			profile.obstructionWeight = 0.55;
			profile.altitudeHelps = false;
			profile.antennaGain = 1.1;
			return profile;
		}

		if (type == ACM_ERemoteFeedSourceType.VEHICLE)
		{
			profile.optimalRange = 500;
			profile.maxRange = 1200;
			profile.obstructionWeight = 0.7;
			profile.altitudeHelps = false;
			profile.antennaGain = 1.0;
			return profile;
		}

		// UAV and GENERIC. Airborne assumptions: long reach, sky between the ends most of the time.
		profile.optimalRange = 600;
		profile.maxRange = 1500;
		profile.obstructionWeight = 0.45;
		profile.altitudeHelps = true;
		profile.antennaGain = 1.0;
		return profile;
	}
}
