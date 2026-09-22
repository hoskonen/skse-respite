#include "MusicClassification.h"

#include "Settings.h"

namespace Respite::MusicClassification
{
	namespace
	{
		struct CachedResult
		{
			Result result;
			bool logged{};
		};

		std::mutex g_cacheLock;
		std::unordered_map<RE::BSIMusicType*, CachedResult> g_cache;

		[[nodiscard]] std::string Upper(std::string_view a_value)
		{
			std::string result{ a_value };
			std::ranges::transform(result, result.begin(), [](unsigned char a_character) {
				return static_cast<char>(std::toupper(a_character));
			});
			return result;
		}

		[[nodiscard]] bool Contains(std::string_view a_value, std::string_view a_token) noexcept
		{
			return a_value.find(a_token) != std::string_view::npos;
		}

		[[nodiscard]] std::string_view EvidenceName(Evidence a_evidence) noexcept
		{
			switch (a_evidence) {
			case Evidence::kKnownForm:
				return "known-form";
			case Evidence::kNativeEditorID:
				return "native-editor-id";
			case Evidence::kDistributedEditorID:
				return "distributed-editor-id";
			case Evidence::kCurrentRegion:
				return "current-region";
			case Evidence::kPlaysOnce:
				return "plays-once-exemption";
			case Evidence::kExemptEditorID:
				return "editor-id-exemption";
			case Evidence::kUnknown:
			default:
				return "unclassified";
			}
		}

		[[nodiscard]] Result Compute(RE::BSIMusicType* a_type) noexcept
		{
			if (!a_type) {
				return {};
			}

			const auto musicType = skyrim_cast<RE::BGSMusicType*>(a_type);
			if (!musicType) {
				return {};
			}

			if (a_type->flags.any(RE::BSIMusicType::MST::kPlaysOnce)) {
				return { MusicContext::kUnmanaged, Evidence::kPlaysOnce };
			}

			const auto editorID = Upper(musicType->GetFormEditorID());
			if (Contains(editorID, "COMBAT") || Contains(editorID, "DISCOVERY") ||
				Contains(editorID, "DEATH") || Contains(editorID, "SPECIAL") ||
				Contains(editorID, "SCRIPTED")) {
				return { MusicContext::kUnmanaged, Evidence::kExemptEditorID };
			}

			// Confirmed native records from the current diagnostic traces.
			if (musicType->GetFormID() == 0x00094BDD) {
				return { MusicContext::kDungeon, Evidence::kKnownForm };
			}

			const bool distributed = editorID.starts_with("PMF_") || editorID.starts_with("MTD_");
			if (distributed) {
				if (Contains(editorID, "TAVERN")) return { MusicContext::kTavern, Evidence::kDistributedEditorID };
				if (Contains(editorID, "DUNGEON")) return { MusicContext::kDungeon, Evidence::kDistributedEditorID };
				if (Contains(editorID, "EXPLORE")) return { MusicContext::kExploration, Evidence::kDistributedEditorID };
				if (Contains(editorID, "TOWN") || Contains(editorID, "CITY")) {
					return { MusicContext::kTown, Evidence::kDistributedEditorID };
				}
			} else {
				if (editorID.starts_with("MUSTAVERN")) return { MusicContext::kTavern, Evidence::kNativeEditorID };
				if (editorID.starts_with("MUSDUNGEON")) return { MusicContext::kDungeon, Evidence::kNativeEditorID };
				if (editorID.starts_with("MUSEXPLORE")) return { MusicContext::kExploration, Evidence::kNativeEditorID };
				if (editorID.starts_with("MUSTOWN") || editorID.starts_with("MUSCITY")) {
					return { MusicContext::kTown, Evidence::kNativeEditorID };
				}
			}

			const auto regionState = RE::PlayerRegionState::GetSingleton();
			if (regionState && regionState->currentMusicType == musicType) {
				return { MusicContext::kExploration, Evidence::kCurrentRegion };
			}
			return {};
		}

		void Log(RE::BSIMusicType* a_type, const Result& a_result)
		{
			if (!Settings::CurrentMusicDiagnosticsEnabled() || !a_type) {
				return;
			}
			if (const auto musicType = skyrim_cast<RE::BGSMusicType*>(a_type)) {
				logs::info(
					"[classification] type={} form={:08X} editorID='{}' context={} evidence={}",
					static_cast<const void*>(a_type),
					musicType->GetFormID(),
					musicType->GetFormEditorID(),
					ContextName(a_result.context),
					EvidenceName(a_result.evidence));
			}
		}
	}

	Result Classify(RE::BSIMusicType* a_type) noexcept
	{
		try {
			std::scoped_lock lock{ g_cacheLock };
			auto [entry, inserted] = g_cache.try_emplace(a_type, CachedResult{ Compute(a_type), false });
			if (!inserted && entry->second.result.evidence == Evidence::kUnknown) {
				const auto refreshed = Compute(a_type);
				if (refreshed.context != entry->second.result.context || refreshed.evidence != entry->second.result.evidence) {
					entry->second.result = refreshed;
					entry->second.logged = false;
				}
			}
			if (!entry->second.logged && Settings::CurrentMusicDiagnosticsEnabled()) {
				Log(a_type, entry->second.result);
				entry->second.logged = true;
			}
			return entry->second.result;
		} catch (...) {
			logs::error("Music classification exception; leaving type unmanaged");
			return {};
		}
	}

	void Reset() noexcept
	{
		try {
			std::scoped_lock lock{ g_cacheLock };
			g_cache.clear();
		} catch (...) {
			logs::error("Music classification exception while resetting cache");
		}
	}
}
