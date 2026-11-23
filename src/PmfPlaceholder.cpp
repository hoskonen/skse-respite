#include "PmfPlaceholder.h"

#include <bcrypt.h>

namespace Respite::PmfPlaceholder
{
	namespace
	{
		constexpr std::string_view kPluginName = "Personal Music Framework - MTD.esp";
		constexpr std::uint32_t kPlaceholderSize = 6756;
		constexpr std::string_view kPlaceholderSHA256 = "4DA0C8BD25069A9F3B55D6ACEB04DF655C64EC8CB0AF81D0266A00A7C66462ED";

		struct ManifestEntry
		{
			RE::FormID localFormID;
			std::string_view category;
			std::string_view targetMusicTypes;
			std::string_view wavPath;
			std::string_view xwmPath;
		};

		enum class SlotNameStyle
		{
			kTwoDigit,
			kParenthesized
		};

		struct SlotFamily
		{
			RE::FormID firstLocalFormID;
			RE::FormID lastLocalFormID;
			std::uint32_t firstSlot;
			std::string_view category;
			std::string_view targetMusicTypes;
			std::string_view directory;
			std::string_view stem;
			SlotNameStyle nameStyle;
		};

		struct ManifestMatch
		{
			std::string_view category;
			std::string_view targetMusicTypes;
			std::string wavPath;
			std::string xwmPath;
		};

		// The wrapper IDs and targets come from PMF's shipped MTD INIs. The paths
		// are the corresponding MUST record ANAM values, with XWM as the effective
		// resource confirmed at runtime. Entries are explicit so gaps remain visible.
		inline constexpr std::array kManifest{
			ManifestEntry{ 0x843, "Apocrypha", "DLC2MUSDungeonApocrypha", "music\\Dungeons\\Apocrypha Music\\Apocrypha01.wav", "music\\Dungeons\\Apocrypha Music\\Apocrypha01.xwm" },
			ManifestEntry{ 0x845, "Apocrypha", "DLC2MUSDungeonApocrypha", "music\\Dungeons\\Apocrypha Music\\Apocrypha02.wav", "music\\Dungeons\\Apocrypha Music\\Apocrypha02.xwm" },
			ManifestEntry{ 0x846, "Apocrypha", "DLC2MUSDungeonApocrypha", "music\\Dungeons\\Apocrypha Music\\Apocrypha03.wav", "music\\Dungeons\\Apocrypha Music\\Apocrypha03.xwm" },
			ManifestEntry{ 0x847, "Apocrypha", "DLC2MUSDungeonApocrypha", "music\\Dungeons\\Apocrypha Music\\Apocrypha04.wav", "music\\Dungeons\\Apocrypha Music\\Apocrypha04.xwm" },
			ManifestEntry{ 0x848, "Blackreach", "PMF_Blackreach", "music\\Dungeons\\Blackreach Music\\Blackreach01.wav", "music\\Dungeons\\Blackreach Music\\Blackreach01.xwm" },
			ManifestEntry{ 0x849, "Blackreach", "PMF_Blackreach", "music\\Dungeons\\Blackreach Music\\Blackreach02.wav", "music\\Dungeons\\Blackreach Music\\Blackreach02.xwm" },
			ManifestEntry{ 0x84A, "Blackreach", "PMF_Blackreach", "music\\Dungeons\\Blackreach Music\\Blackreach03.wav", "music\\Dungeons\\Blackreach Music\\Blackreach03.xwm" },
			ManifestEntry{ 0x84B, "Blackreach", "PMF_Blackreach", "music\\Dungeons\\Blackreach Music\\Blackreach04.wav", "music\\Dungeons\\Blackreach Music\\Blackreach04.xwm" },
			ManifestEntry{ 0x84C, "Blackreach", "PMF_Blackreach", "music\\Dungeons\\Blackreach Music\\Blackreach05.wav", "music\\Dungeons\\Blackreach Music\\Blackreach05.xwm" },
			ManifestEntry{ 0x84D, "Caves", "MUSDungeonCave", "music\\Dungeons\\Caves\\CaveMusic01.wav", "music\\Dungeons\\Caves\\CaveMusic01.xwm" },
			ManifestEntry{ 0x84E, "Caves", "MUSDungeonCave", "music\\Dungeons\\Caves\\CaveMusic02.wav", "music\\Dungeons\\Caves\\CaveMusic02.xwm" },
			ManifestEntry{ 0x84F, "Caves", "MUSDungeonCave", "music\\Dungeons\\Caves\\CaveMusic03.wav", "music\\Dungeons\\Caves\\CaveMusic03.xwm" },
			ManifestEntry{ 0x850, "Caves", "MUSDungeonCave", "music\\Dungeons\\Caves\\CaveMusic04.wav", "music\\Dungeons\\Caves\\CaveMusic04.xwm" },
			ManifestEntry{ 0x851, "Caves", "MUSDungeonCave", "music\\Dungeons\\Caves\\CaveMusic05.wav", "music\\Dungeons\\Caves\\CaveMusic05.xwm" },
			ManifestEntry{ 0x852, "Generic Dungeons", "MUSDungeon", "music\\Dungeons\\Dungeons\\DungeonMusic01.wav", "music\\Dungeons\\Dungeons\\DungeonMusic01.xwm" },
			ManifestEntry{ 0x853, "Generic Dungeons", "MUSDungeon", "music\\Dungeons\\Dungeons\\DungeonMusic02.wav", "music\\Dungeons\\Dungeons\\DungeonMusic02.xwm" },
			ManifestEntry{ 0x854, "Generic Dungeons", "MUSDungeon", "music\\Dungeons\\Dungeons\\DungeonMusic03.wav", "music\\Dungeons\\Dungeons\\DungeonMusic03.xwm" },
			ManifestEntry{ 0x855, "Generic Dungeons", "MUSDungeon", "music\\Dungeons\\Dungeons\\DungeonMusic04.wav", "music\\Dungeons\\Dungeons\\DungeonMusic04.xwm" },
			ManifestEntry{ 0x856, "Generic Dungeons", "MUSDungeon", "music\\Dungeons\\Dungeons\\DungeonMusic05.wav", "music\\Dungeons\\Dungeons\\DungeonMusic05.xwm" },
			ManifestEntry{ 0x857, "Dwemer Ruins", "PMF_DwarvenRuins, MUSDungeon", "music\\Dungeons\\Dwarven Ruins\\Dwemer01.wav", "music\\Dungeons\\Dwarven Ruins\\Dwemer01.xwm" },
			ManifestEntry{ 0x858, "Dwemer Ruins", "PMF_DwarvenRuins, MUSDungeon", "music\\Dungeons\\Dwarven Ruins\\Dwemer02.wav", "music\\Dungeons\\Dwarven Ruins\\Dwemer02.xwm" },
			ManifestEntry{ 0x859, "Dwemer Ruins", "PMF_DwarvenRuins, MUSDungeon", "music\\Dungeons\\Dwarven Ruins\\Dwemer03.wav", "music\\Dungeons\\Dwarven Ruins\\Dwemer03.xwm" },
			ManifestEntry{ 0x85A, "Dwemer Ruins", "PMF_DwarvenRuins, MUSDungeon", "music\\Dungeons\\Dwarven Ruins\\Dwemer04.wav", "music\\Dungeons\\Dwarven Ruins\\Dwemer04.xwm" },
			ManifestEntry{ 0x85B, "Dwemer Ruins", "PMF_DwarvenRuins, MUSDungeon", "music\\Dungeons\\Dwarven Ruins\\Dwemer05.wav", "music\\Dungeons\\Dwarven Ruins\\Dwemer05.xwm" },
			ManifestEntry{ 0x85C, "Forts", "MUSDungeonFort", "music\\Dungeons\\Forts\\FortMusic01.wav", "music\\Dungeons\\Forts\\FortMusic01.xwm" },
			ManifestEntry{ 0x85D, "Forts", "MUSDungeonFort", "music\\Dungeons\\Forts\\FortMusic02.wav", "music\\Dungeons\\Forts\\FortMusic02.xwm" },
			ManifestEntry{ 0x85E, "Forts", "MUSDungeonFort", "music\\Dungeons\\Forts\\FortMusic03.wav", "music\\Dungeons\\Forts\\FortMusic03.xwm" },
			ManifestEntry{ 0x85F, "Forts", "MUSDungeonFort", "music\\Dungeons\\Forts\\FortMusic04.wav", "music\\Dungeons\\Forts\\FortMusic04.xwm" },
			ManifestEntry{ 0x860, "Forts", "MUSDungeonFort", "music\\Dungeons\\Forts\\FortMusic05.wav", "music\\Dungeons\\Forts\\FortMusic05.xwm" },
			ManifestEntry{ 0x861, "Ice Caves", "MUSDungeonIce", "music\\Dungeons\\Ice Caves\\IceCaveMusic01.wav", "music\\Dungeons\\Ice Caves\\IceCaveMusic01.xwm" },
			ManifestEntry{ 0x862, "Ice Caves", "MUSDungeonIce", "music\\Dungeons\\Ice Caves\\IceCaveMusic02.wav", "music\\Dungeons\\Ice Caves\\IceCaveMusic02.xwm" },
			ManifestEntry{ 0x863, "Ice Caves", "MUSDungeonIce", "music\\Dungeons\\Ice Caves\\IceCaveMusic03.wav", "music\\Dungeons\\Ice Caves\\IceCaveMusic03.xwm" },
			ManifestEntry{ 0x864, "Ice Caves", "MUSDungeonIce", "music\\Dungeons\\Ice Caves\\IceCaveMusic04.wav", "music\\Dungeons\\Ice Caves\\IceCaveMusic04.xwm" },
			ManifestEntry{ 0x865, "Ice Caves", "MUSDungeonIce", "music\\Dungeons\\Ice Caves\\IceCaveMusic05.wav", "music\\Dungeons\\Ice Caves\\IceCaveMusic05.xwm" },
			ManifestEntry{ 0x866, "Labyrinthian", "PMF_Labyrinthian, MUSDungeon", "music\\Dungeons\\Labyrinthian\\Labyrinthian01.wav", "music\\Dungeons\\Labyrinthian\\Labyrinthian01.xwm" },
			ManifestEntry{ 0x867, "Labyrinthian", "PMF_Labyrinthian, MUSDungeon", "music\\Dungeons\\Labyrinthian\\Labyrinthian02.wav", "music\\Dungeons\\Labyrinthian\\Labyrinthian02.xwm" },
			ManifestEntry{ 0x868, "Labyrinthian", "PMF_Labyrinthian, MUSDungeon", "music\\Dungeons\\Labyrinthian\\Labyrinthian03.wav", "music\\Dungeons\\Labyrinthian\\Labyrinthian03.xwm" },
			ManifestEntry{ 0x869, "Labyrinthian", "PMF_Labyrinthian, MUSDungeon", "music\\Dungeons\\Labyrinthian\\Labyrinthian04.wav", "music\\Dungeons\\Labyrinthian\\Labyrinthian04.xwm" },
			ManifestEntry{ 0x86A, "Labyrinthian", "PMF_Labyrinthian, MUSDungeon", "music\\Dungeons\\Labyrinthian\\Labyrinthian05.wav", "music\\Dungeons\\Labyrinthian\\Labyrinthian05.xwm" },
			ManifestEntry{ 0x86B, "Riften Ratway", "PMF_Ratway, MUSDungeonFort", "music\\Dungeons\\Riften Ratway\\RiftenRatway01.wav", "music\\Dungeons\\Riften Ratway\\RiftenRatway01.xwm" },
			ManifestEntry{ 0x86C, "Riften Ratway", "PMF_Ratway, MUSDungeonFort", "music\\Dungeons\\Riften Ratway\\RiftenRatway02.wav", "music\\Dungeons\\Riften Ratway\\RiftenRatway02.xwm" },
			ManifestEntry{ 0x86D, "Riften Ratway", "PMF_Ratway, MUSDungeonFort", "music\\Dungeons\\Riften Ratway\\RiftenRatway03.wav", "music\\Dungeons\\Riften Ratway\\RiftenRatway03.xwm" },
			ManifestEntry{ 0x86E, "Riften Ratway", "PMF_Ratway, MUSDungeonFort", "music\\Dungeons\\Riften Ratway\\RiftenRatway04.wav", "music\\Dungeons\\Riften Ratway\\RiftenRatway04.xwm" },
			ManifestEntry{ 0x86F, "Riften Ratway", "PMF_Ratway, MUSDungeonFort", "music\\Dungeons\\Riften Ratway\\RiftenRatway05.wav", "music\\Dungeons\\Riften Ratway\\RiftenRatway05.xwm" },
			ManifestEntry{ 0x870, "Soul Cairn", "MUSExploreDLCSoulCairn", "music\\Dungeons\\Soul Cairn\\SoulCairn01.wav", "music\\Dungeons\\Soul Cairn\\SoulCairn01.xwm" },
			ManifestEntry{ 0x871, "Soul Cairn", "MUSExploreDLCSoulCairn", "music\\Dungeons\\Soul Cairn\\SoulCairn02.wav", "music\\Dungeons\\Soul Cairn\\SoulCairn02.xwm" },
			ManifestEntry{ 0x872, "Soul Cairn", "MUSExploreDLCSoulCairn", "music\\Dungeons\\Soul Cairn\\SoulCairn03.wav", "music\\Dungeons\\Soul Cairn\\SoulCairn03.xwm" },
			ManifestEntry{ 0x873, "Soul Cairn", "MUSExploreDLCSoulCairn", "music\\Dungeons\\Soul Cairn\\SoulCairn04.wav", "music\\Dungeons\\Soul Cairn\\SoulCairn04.xwm" },
			ManifestEntry{ 0x874, "Soul Cairn", "MUSExploreDLCSoulCairn", "music\\Dungeons\\Soul Cairn\\SoulCairn05.wav", "music\\Dungeons\\Soul Cairn\\SoulCairn05.xwm" },
			ManifestEntry{ 0x875, "Vampire Castle", "MUSDungeonDLCVampireCastle", "music\\Dungeons\\Vampire Castle Music\\VampireCastle01.wav", "music\\Dungeons\\Vampire Castle Music\\VampireCastle01.xwm" },
			ManifestEntry{ 0x876, "Vampire Castle", "MUSDungeonDLCVampireCastle", "music\\Dungeons\\Vampire Castle Music\\VampireCastle02.wav", "music\\Dungeons\\Vampire Castle Music\\VampireCastle02.xwm" },
			ManifestEntry{ 0x877, "Vampire Castle", "MUSDungeonDLCVampireCastle", "music\\Dungeons\\Vampire Castle Music\\VampireCastle03.wav", "music\\Dungeons\\Vampire Castle Music\\VampireCastle03.xwm" },
			ManifestEntry{ 0x878, "Vampire Castle", "MUSDungeonDLCVampireCastle", "music\\Dungeons\\Vampire Castle Music\\VampireCastle04.wav", "music\\Dungeons\\Vampire Castle Music\\VampireCastle04.xwm" },
			ManifestEntry{ 0x879, "Vampire Castle", "MUSDungeonDLCVampireCastle", "music\\Dungeons\\Vampire Castle Music\\VampireCastle05.wav", "music\\Dungeons\\Vampire Castle Music\\VampireCastle05.xwm" }
		};

		// These ranges are compact representations of explicit, verified rows in
		// PMF's shipped exploration/location MTD INIs. Splits preserve every gap
		// and non-placeholder slot (notably Mountains 0x8E5 and Winterhold 1-4).
		inline constexpr std::array kExplorationFamilies{
			SlotFamily{ 0x83A, 0x83F, 1, "Solstheim", "DLC2MUSExploreMountain, DLC2MUSExploreSnow, DLC2MUSExploreSolstheimGeneral", "music\\DLC02 - Solstheim\\", "SolstheimExplore", SlotNameStyle::kTwoDigit },
			SlotFamily{ 0x840, 0x842, 8, "Solstheim", "DLC2MUSExploreMountain, DLC2MUSExploreSnow, DLC2MUSExploreSolstheimGeneral", "music\\DLC02 - Solstheim\\", "SolstheimExplore", SlotNameStyle::kTwoDigit },
			SlotFamily{ 0x844, 0x844, 7, "Solstheim", "DLC2MUSExploreMountain, DLC2MUSExploreSnow, DLC2MUSExploreSolstheimGeneral", "music\\DLC02 - Solstheim\\", "SolstheimExplore", SlotNameStyle::kTwoDigit },
			SlotFamily{ 0x87A, 0x883, 1, "All Regions Day", "PMF exploration region types", "music\\Exploration\\All Regions\\Day\\All Weather\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x88A, 0x893, 1, "All Regions Night", "PMF exploration region types", "music\\Exploration\\All Regions\\Night\\All Weather\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x894, 0x896, 1, "All Regions Night Rain", "PMF exploration region types", "music\\Exploration\\All Regions\\Night\\Raining Only\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x89A, 0x89E, 1, "Coast Day", "PMF_Coast", "music\\Exploration\\Coast\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x89F, 0x8A3, 1, "Coast Night", "PMF_Coast", "music\\Exploration\\Coast\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8A4, 0x8A6, 1, "Darklight Tower Day", "PMF_DarklightTower", "music\\Exploration\\Darklight Tower\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8A7, 0x8A9, 1, "Darklight Tower Night", "PMF_DarklightTower", "music\\Exploration\\Darklight Tower\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8AA, 0x8AC, 1, "Deepwood Redoubt Day", "PMF_DeepwoodRedoubt", "music\\Exploration\\Deepwood Redoubt\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8AD, 0x8AF, 1, "Deepwood Redoubt Night", "PMF_DeepwoodRedoubt", "music\\Exploration\\Deepwood Redoubt\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8B0, 0x8B4, 1, "Fall Forest Day", "PMF_ExploreFallForest", "music\\Exploration\\Fall Forest\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8B5, 0x8B9, 1, "Fall Forest Night", "PMF_ExploreFallForest", "music\\Exploration\\Fall Forest\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8BA, 0x8BC, 1, "Fallowstone Day", "PMF_Fallowstone", "music\\Exploration\\Fallowstone\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8BD, 0x8BF, 1, "Fallowstone Night", "PMF_Fallowstone", "music\\Exploration\\Fallowstone\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8C0, 0x8C4, 1, "Falmer Valley Day", "MUSExploreDLCFalmerValley", "music\\Exploration\\Falmer Valley\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8C5, 0x8C9, 1, "Falmer Valley Night", "MUSExploreDLCFalmerValley", "music\\Exploration\\Falmer Valley\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8CA, 0x8CC, 1, "Japhet's Folly Day", "PMF_JaphetsFolly", "music\\Exploration\\Japhets Folly\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8CD, 0x8CF, 1, "Japhet's Folly Night", "PMF_JaphetsFolly", "music\\Exploration\\Japhets Folly\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8D0, 0x8D2, 1, "Karthspire Redoubt Night", "PMF_KarthspireRedoubt", "music\\Exploration\\Karthspire Redoubt\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8D3, 0x8D5, 1, "Karthspire Redoubt Day", "PMF_KarthspireRedoubt", "music\\Exploration\\Karthspire Redoubt\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8D6, 0x8DA, 1, "Marsh Day", "PMF_ExploreMarsh", "music\\Exploration\\Marsh\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8DB, 0x8DF, 1, "Marsh Night", "PMF_ExploreMarsh", "music\\Exploration\\Marsh\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8E0, 0x8E4, 1, "Mountains Day", "PMF_ExploreMountains", "music\\Exploration\\Mountains\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8E6, 0x8E9, 2, "Mountains Night", "PMF_ExploreMountains", "music\\Exploration\\Mountains\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8EA, 0x8EE, 1, "Pine Forest Day", "PMF_ExplorePineForest", "music\\Exploration\\Pine Forest\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8EF, 0x8F3, 1, "Pine Forest Night", "PMF_ExplorePineForest", "music\\Exploration\\Pine Forest\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8F4, 0x8F8, 1, "Reach Day", "PMF_ExploreReach", "music\\Exploration\\Reach\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x8F9, 0x8FD, 1, "Reach Night", "PMF_ExploreReach", "music\\Exploration\\Reach\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x904, 0x908, 1, "Snow Day", "PMF_ExploreSnowRegions", "music\\Exploration\\Snow\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x909, 0x90D, 1, "Snow Night", "PMF_ExploreSnowRegions", "music\\Exploration\\Snow\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x90E, 0x912, 1, "Tundra Day", "PMF_ExploreTundra", "music\\Exploration\\Tundra\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x913, 0x917, 1, "Tundra Night", "PMF_ExploreTundra", "music\\Exploration\\Tundra\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x918, 0x91C, 1, "Volcanic Tundra Day", "PMF_ExploreVolcanicTundra", "music\\Exploration\\Volcanic Tundra\\Day\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x91D, 0x921, 1, "Volcanic Tundra Night", "PMF_ExploreVolcanicTundra", "music\\Exploration\\Volcanic Tundra\\Night\\", "Exploration", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x93C, 0x93E, 3, "Sovngarde", "MUSExploreSovngardeChantExterior, MUSExploreSovngardeChant", "music\\More Locations\\Sovngarde\\", "Sovngarde", SlotNameStyle::kParenthesized },
			SlotFamily{ 0x970, 0x970, 5, "Winterhold", "PMF_Winterhold", "Music\\Towns and Cities\\Winterhold\\", "Winterhold", SlotNameStyle::kParenthesized }
		};

		std::mutex g_cacheLock;
		std::unordered_map<std::string, bool> g_placeholderCache;

		[[nodiscard]] std::optional<ManifestMatch> FindManifestEntry(RE::FormID a_localFormID)
		{
			const auto found = std::ranges::find(kManifest, a_localFormID, &ManifestEntry::localFormID);
			if (found != kManifest.end()) {
				return ManifestMatch{ found->category, found->targetMusicTypes,
					std::string{ found->wavPath }, std::string{ found->xwmPath } };
			}

			for (const auto& family : kExplorationFamilies) {
				if (a_localFormID < family.firstLocalFormID || a_localFormID > family.lastLocalFormID) {
					continue;
				}
				const auto slot = family.firstSlot + (a_localFormID - family.firstLocalFormID);
				const auto base = family.nameStyle == SlotNameStyle::kTwoDigit ?
				                      std::format("{}{}{:02}", family.directory, family.stem, slot) :
				                      std::format("{}{} ({})", family.directory, family.stem, slot);
				return ManifestMatch{ family.category, family.targetMusicTypes, base + ".wav", base + ".xwm" };
			}
			return std::nullopt;
		}

		[[nodiscard]] bool SameResourceID(const RE::BSResource::ID& a_lhs, const RE::BSResource::ID& a_rhs) noexcept
		{
			return a_lhs.file == a_rhs.file && a_lhs.dir == a_rhs.dir &&
			       std::memcmp(a_lhs.ext, a_rhs.ext, sizeof(a_lhs.ext)) == 0;
		}

		[[nodiscard]] std::optional<std::array<std::byte, 32>> SHA256(std::span<const std::byte> a_data)
		{
			BCRYPT_ALG_HANDLE algorithm{};
			BCRYPT_HASH_HANDLE hash{};
			DWORD objectLength{};
			DWORD hashLength{};
			DWORD written{};
			std::optional<std::array<std::byte, 32>> result;

			if (BCryptOpenAlgorithmProvider(std::addressof(algorithm), BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
			    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(std::addressof(objectLength)), sizeof(objectLength), std::addressof(written), 0) < 0 ||
			    BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(std::addressof(hashLength)), sizeof(hashLength), std::addressof(written), 0) < 0 ||
			    hashLength != 32) {
				if (algorithm) {
					BCryptCloseAlgorithmProvider(algorithm, 0);
				}
				return result;
			}

			std::vector<UCHAR> object(objectLength);
			std::array<std::byte, 32> digest{};
			if (BCryptCreateHash(algorithm, std::addressof(hash), object.data(), objectLength, nullptr, 0, 0) >= 0 &&
			    BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(a_data.data())), static_cast<ULONG>(a_data.size()), 0) >= 0 &&
			    BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), 0) >= 0) {
				result = digest;
			}

			if (hash) {
				BCryptDestroyHash(hash);
			}
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return result;
		}

		[[nodiscard]] std::array<std::byte, 32> ExpectedHash()
		{
			std::array<std::byte, 32> result{};
			for (std::size_t i = 0; i < result.size(); ++i) {
				const auto pair = kPlaceholderSHA256.substr(i * 2, 2);
				unsigned int value{};
				std::from_chars(pair.data(), pair.data() + pair.size(), value, 16);
				result[i] = static_cast<std::byte>(value);
			}
			return result;
		}

		[[nodiscard]] bool InspectAsset(std::string_view a_path)
		{
			const std::string cacheKey{ a_path };
			std::scoped_lock lock{ g_cacheLock };
			if (const auto found = g_placeholderCache.find(cacheKey); found != g_placeholderCache.end()) {
				return found->second;
			}

			bool placeholder{};
			RE::BSResourceNiBinaryStream stream{ cacheKey };
			if (stream.good() && stream.stream && stream.stream->totalSize == kPlaceholderSize) {
				std::vector<std::byte> bytes(kPlaceholderSize);
				if (stream.read(bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
					static const auto expected = ExpectedHash();
					if (const auto digest = SHA256(bytes)) {
						placeholder = *digest == expected;
					}
				}
			}

			g_placeholderCache.emplace(cacheKey, placeholder);
			return placeholder;
		}
	}

	Result ClassifySelectedSingle(RE::BSIMusicType* a_owner, RE::BSIMusicTrack* a_completedTrack) noexcept
	{
		Result result{};
		try {
			if (!a_owner || !a_completedTrack || a_owner->currentTrackIndex >= a_owner->tracks.size()) {
				return result;
			}

			const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_owner->tracks[a_owner->currentTrackIndex]);
			if (!wrapper || wrapper->track != a_completedTrack || !skyrim_cast<RE::BGSMusicSingleTrack*>(a_completedTrack)) {
				return result;
			}
			const auto source = wrapper->GetFile(0);
			if (!source || _stricmp(source->fileName, kPluginName.data()) != 0) {
				return result;
			}

			result.localFormID = wrapper->GetLocalFormID();
			const auto manifest = FindManifestEntry(result.localFormID);
			if (!manifest) {
				return result;
			}
			result.recognizedSlot = true;
			result.category = manifest->category;
			result.targetMusicTypes = manifest->targetMusicTypes;
			result.assetPath = manifest->xwmPath;

			// The PMF record names the WAV while Skyrim resolves the paired XWM.
			// Require the record's resource identity to match the expected manifest slot.
			const auto single = static_cast<RE::BGSMusicSingleTrack*>(a_completedTrack);
			RE::BSResource::ID expectedWavID{};
			expectedWavID.GenerateFromPath(manifest->wavPath.c_str());
			if (!SameResourceID(single->trackID, expectedWavID)) {
				return result;
			}

			result.placeholder = InspectAsset(manifest->xwmPath);
		} catch (...) {
			// Fail open for policy purposes: unknown content is audible.
			result.placeholder = false;
		}
		return result;
	}
}
