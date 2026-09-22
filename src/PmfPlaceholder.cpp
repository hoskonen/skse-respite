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
			std::string_view wavPath;
			std::string_view xwmPath;
		};

		// These are the PMF cave and generic-dungeon slots established by its shipped MTD manifests.
		// Unlisted PMF forms intentionally fall through as audible/unknown.
		inline constexpr std::array kManifest{
			ManifestEntry{ 0x84D, "music\\Dungeons\\Caves\\CaveMusic01.wav", "music\\Dungeons\\Caves\\CaveMusic01.xwm" },
			ManifestEntry{ 0x84E, "music\\Dungeons\\Caves\\CaveMusic02.wav", "music\\Dungeons\\Caves\\CaveMusic02.xwm" },
			ManifestEntry{ 0x84F, "music\\Dungeons\\Caves\\CaveMusic03.wav", "music\\Dungeons\\Caves\\CaveMusic03.xwm" },
			ManifestEntry{ 0x850, "music\\Dungeons\\Caves\\CaveMusic04.wav", "music\\Dungeons\\Caves\\CaveMusic04.xwm" },
			ManifestEntry{ 0x851, "music\\Dungeons\\Caves\\CaveMusic05.wav", "music\\Dungeons\\Caves\\CaveMusic05.xwm" },
			ManifestEntry{ 0x852, "music\\Dungeons\\Dungeons\\DungeonMusic01.wav", "music\\Dungeons\\Dungeons\\DungeonMusic01.xwm" },
			ManifestEntry{ 0x853, "music\\Dungeons\\Dungeons\\DungeonMusic02.wav", "music\\Dungeons\\Dungeons\\DungeonMusic02.xwm" },
			ManifestEntry{ 0x854, "music\\Dungeons\\Dungeons\\DungeonMusic03.wav", "music\\Dungeons\\Dungeons\\DungeonMusic03.xwm" },
			ManifestEntry{ 0x855, "music\\Dungeons\\Dungeons\\DungeonMusic04.wav", "music\\Dungeons\\Dungeons\\DungeonMusic04.xwm" },
			ManifestEntry{ 0x856, "music\\Dungeons\\Dungeons\\DungeonMusic05.wav", "music\\Dungeons\\Dungeons\\DungeonMusic05.xwm" }
		};

		std::mutex g_cacheLock;
		std::unordered_map<std::string, bool> g_placeholderCache;

		[[nodiscard]] const ManifestEntry* FindManifestEntry(RE::FormID a_localFormID) noexcept
		{
			const auto found = std::ranges::find(kManifest, a_localFormID, &ManifestEntry::localFormID);
			return found == kManifest.end() ? nullptr : std::addressof(*found);
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
			result.assetPath = manifest->xwmPath;

			// The PMF record names the WAV while Skyrim resolves the paired XWM.
			// Require the record's resource identity to match the expected manifest slot.
			const auto single = static_cast<RE::BGSMusicSingleTrack*>(a_completedTrack);
			RE::BSResource::ID expectedWavID{};
			expectedWavID.GenerateFromPath(manifest->wavPath.data());
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
