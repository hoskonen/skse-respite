#include "MusicClassification.h"

namespace Respite::MusicClassification
{
	namespace
	{
		struct SlotFamily
		{
			RE::FormID first;
			RE::FormID last;
			unsigned firstSlot;
			std::string_view directory;
			std::string_view stem;
			MusicSubcontext subcontext;
		};

		// Explicit PMF INI wrapper rows and their record paths. This metadata is
		// independent of placeholder content; replacement audio keeps its category.
		constexpr std::array kSlots{
			SlotFamily{ 0x87A, 0x883, 1, "music\\Exploration\\All Regions\\Day\\All Weather\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x884, 0x886, 1, "music\\Exploration\\All Regions\\Day\\Raining Only\\", "Exploration", MusicSubcontext::kExplorationDay },
			// PMF explicitly swaps the first two Day/Snowing slots.
			SlotFamily{ 0x887, 0x887, 2, "music\\Exploration\\All Regions\\Day\\Snowing Only\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x888, 0x888, 1, "music\\Exploration\\All Regions\\Day\\Snowing Only\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x889, 0x889, 3, "music\\Exploration\\All Regions\\Day\\Snowing Only\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x88A, 0x893, 1, "music\\Exploration\\All Regions\\Night\\All Weather\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x894, 0x896, 1, "music\\Exploration\\All Regions\\Night\\Raining Only\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x897, 0x899, 1, "music\\Exploration\\All Regions\\Night\\Snowing Only\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x89A, 0x89E, 1, "music\\Exploration\\Coast\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x89F, 0x8A3, 1, "music\\Exploration\\Coast\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8A4, 0x8A6, 1, "music\\Exploration\\Darklight Tower\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8A7, 0x8A9, 1, "music\\Exploration\\Darklight Tower\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8AA, 0x8AC, 1, "music\\Exploration\\Deepwood Redoubt\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8AD, 0x8AF, 1, "music\\Exploration\\Deepwood Redoubt\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8B0, 0x8B4, 1, "music\\Exploration\\Fall Forest\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8B5, 0x8B9, 1, "music\\Exploration\\Fall Forest\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8BA, 0x8BC, 1, "music\\Exploration\\Fallowstone\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8BD, 0x8BF, 1, "music\\Exploration\\Fallowstone\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8C0, 0x8C4, 1, "music\\Exploration\\Falmer Valley\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8C5, 0x8C9, 1, "music\\Exploration\\Falmer Valley\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8CA, 0x8CC, 1, "music\\Exploration\\Japhets Folly\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8CD, 0x8CF, 1, "music\\Exploration\\Japhets Folly\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8D0, 0x8D2, 1, "music\\Exploration\\Karthspire Redoubt\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8D3, 0x8D5, 1, "music\\Exploration\\Karthspire Redoubt\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8D6, 0x8DA, 1, "music\\Exploration\\Marsh\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8DB, 0x8DF, 1, "music\\Exploration\\Marsh\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8E0, 0x8E4, 1, "music\\Exploration\\Mountains\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8E5, 0x8E9, 1, "music\\Exploration\\Mountains\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8EA, 0x8EE, 1, "music\\Exploration\\Pine Forest\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8EF, 0x8F3, 1, "music\\Exploration\\Pine Forest\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x8F4, 0x8F8, 1, "music\\Exploration\\Reach\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x8F9, 0x8FD, 1, "music\\Exploration\\Reach\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x904, 0x908, 1, "music\\Exploration\\Snow\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x909, 0x90D, 1, "music\\Exploration\\Snow\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x90E, 0x912, 1, "music\\Exploration\\Tundra\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x913, 0x917, 1, "music\\Exploration\\Tundra\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
			SlotFamily{ 0x918, 0x91C, 1, "music\\Exploration\\Volcanic Tundra\\Day\\", "Exploration", MusicSubcontext::kExplorationDay },
			SlotFamily{ 0x91D, 0x921, 1, "music\\Exploration\\Volcanic Tundra\\Night\\", "Exploration", MusicSubcontext::kExplorationNight },
		};

		MusicSubcontext Identify(RE::BSIMusicTrack* a_entry)
		{
			const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_entry);
			if (!wrapper) return MusicSubcontext::kNone;
			const auto source = wrapper->GetFile(0);
			if (!source || _stricmp(source->fileName, "Personal Music Framework - MTD.esp") != 0) return MusicSubcontext::kNone;
			const auto single = skyrim_cast<RE::BGSMusicSingleTrack*>(wrapper->track);
			if (!single) return MusicSubcontext::kNone;
			const auto id = wrapper->GetLocalFormID();
			struct SlotIdentity
			{
				RE::BSResource::ID resource;
				MusicSubcontext subcontext;
			};
			static const auto identities = [] {
				std::unordered_map<RE::FormID, SlotIdentity> result;
				for (const auto& slot : kSlots) {
					for (auto local = slot.first; local <= slot.last; ++local) {
						const auto path = std::format("{}{} ({}).wav", slot.directory, slot.stem, slot.firstSlot + local - slot.first);
						SlotIdentity identity{ {}, slot.subcontext };
						identity.resource.GenerateFromPath(path.c_str());
						result.emplace(local, identity);
					}
				}
				return result;
			}();
			if (const auto found = identities.find(id); found != identities.end()) {
				const auto& expected = found->second.resource;
				if (single->trackID.file == expected.file && single->trackID.dir == expected.dir &&
					std::memcmp(single->trackID.ext, expected.ext, sizeof(expected.ext)) == 0) return found->second.subcontext;
			}
			return MusicSubcontext::kNone;
		}

		MusicSubcontext IdentifySelected(RE::BSIMusicTrack* a_entry, unsigned a_depth)
		{
			if (!a_entry || a_depth > 8) return MusicSubcontext::kNone;
			if (const auto identified = Identify(a_entry); identified != MusicSubcontext::kNone) return identified;
			if (const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_entry)) a_entry = wrapper->track;
			const auto palette = a_entry ? skyrim_cast<RE::BGSMusicPaletteTrack*>(a_entry) : nullptr;
			if (!palette) return MusicSubcontext::kNone;
			MusicSubcontext result{ MusicSubcontext::kNone };
			// A palette is only classified when ALL possible children agree. One
			// selected/audible child cannot establish the identity of the whole palette.
			for (const auto& layer : palette->layers) {
				for (auto* child : layer) {
					const auto period = IdentifySelected(child, a_depth + 1);
					if (period == MusicSubcontext::kNone || (result != MusicSubcontext::kNone && result != period)) return MusicSubcontext::kNone;
					result = period;
				}
			}
			return result;
		}
	}

	MusicSubcontext ClassifySelectedSubcontext(RE::BSIMusicTrack* a_entry, MusicContext a_context) noexcept
	{
		try {
			return a_context == MusicContext::kExploration ? IdentifySelected(a_entry, 0) : MusicSubcontext::kNone;
		} catch (...) { return MusicSubcontext::kNone; }
	}

}

