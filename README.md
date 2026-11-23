# Respite

Respite is an SKSE plugin for Skyrim Special Edition that provides context-aware control over music pacing while preserving Skyrim's native music arbitration.

Implemented policies currently include configurable Interval pacing for exploration and OncePerActivation for dungeon music. Exploration cooldowns are context-wide, while combat, discovery, death, scripted, and otherwise unmanaged music retain native behavior.

Exploration exposes Normal, Interval, and Disabled. OncePerActivation is a location-activation policy used by Dungeon: an audible top-level completion consumes the activation until its manager activation resets. Disabled prevents managed ambient music in that context from starting without terminating music that is already playing; event and special music remain exempt. Town and Tavern settings are unchanged.

Exploration has a collapsed Advanced overrides section for Day and Night, each offering Inherit, Normal, Interval, and Disabled and defaulting to Inherit. A specific override takes precedence over the parent policy; Interval timing always uses the Exploration silence range. INI values are stored as `Day` and `Night` under `[Exploration.Overrides]`. Old INIs without this section inherit automatically. Legacy `Policy=OncePerActivation` under `[Exploration]` (or an unknown policy) falls back to Normal with a warning. Legacy/unknown Day or Night overrides fall back to Inherit with a warning. These normalized values are persisted on the next settings save; loading does not rewrite the INI. Dungeon's OncePerActivation configuration and behavior are unchanged.

Day/Night evidence uses the native selected top-level entry's verified PMF wrapper ID and WAV resource identity. With overrides configured, the playback decision is deferred from the pre-selection type gate to the selected wrapper/concrete `DoPlay`, before audio starts. Native selection is left intact; Respite does not edit the selected index or history. A palette is classified only when all possible children have matching verified period identities. Unknown, mixed, or unmapped selected entries use the parent policy; no clock or EditorID guesses are made. Replaced PMF audio retains its slot category.

The selected-wrapper hook uses `BGSMusicTrackFormWrapper`'s `BSIMusicTrack` secondary vtable `[1]`, `DoPlay` slot 2 (SE relocation 236546 / AE relocation 191038). Concrete playback hooks cover direct top-level calls; palette children are not independently gated. A declined selected palette remains protected from inactive updates even if native type playback has already marked its owner playing. Playing/finishing palettes are not blocked, and declined starts cannot consume completion accounting. This ordering needs in-game validation on both supported runtimes.

Parent policy or override edits reset the affected context's policy state before future native decisions. An effective policy change between recognized periods also resets that context. Exploration retains one context-wide Interval cooldown; Day and Night do not create separate stored cooldowns. Neither settings edits nor policy changes start or stop audio directly.

Diagnostics have two practical layers:

- The normal log records lifecycle and meaningful classification/policy transitions.
- Verbose/current-music diagnostics add manager queues, track transitions, palette layers, sound handles, and other low-level runtime details.

Possible future work includes:

- Different pacing rules for exploration, dungeons, towns, and other music types
- Improved handling of music transitions around combat
- Repeat prevention and additional soundtrack pacing controls
