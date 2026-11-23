#include "MusicClassification.h"

#include "Settings.h"

namespace Respite::MusicClassification
{
	namespace
	{
		struct CachedResult
		{
			Result result;
			bool decisionLogged{};
			bool verboseLogged{};
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
			if (musicType->GetFormID() == 0x001094A9 || editorID == "MUSDUNGEONCHARGEN") {
				return { MusicContext::kUnmanaged, Evidence::kExemptEditorID };
			}
			if (Contains(editorID, "COMBAT") || Contains(editorID, "DISCOVERY") ||
				Contains(editorID, "DEATH") || Contains(editorID, "SPECIAL") ||
				Contains(editorID, "SCRIPTED")) {
				return { MusicContext::kUnmanaged, Evidence::kExemptEditorID };
			}

			// Confirmed native records from the current diagnostic traces.
			if (musicType->GetFormID() == 0x00094BDD) {
				return { MusicContext::kDungeon, Evidence::kKnownForm };
			}
			if (editorID == "DLC2MUSDUNGEONAPOCRYPHA") {
				return { MusicContext::kDungeon, Evidence::kNativeEditorID };
			}

			// Exact music types established by PMF's Towns and Cities MTD INIs.
			// These lack a generic TOWN/CITY token, so resolve them before the
			// distributed-type rules and current-region exploration fallback.
			constexpr std::array kPmfTownTypes{
				std::string_view{ "PMF_FALKREATH" },
				std::string_view{ "PMF_MARKARTH" },
				std::string_view{ "PMF_RIFTEN" },
				std::string_view{ "PMF_SOLITUDE" },
				std::string_view{ "PMF_WHITERUN" },
				std::string_view{ "PMF_WINDHELM" },
				std::string_view{ "PMF_WINTERHOLD" }
			};
			if (std::ranges::find(kPmfTownTypes, editorID) != kPmfTownTypes.end()) {
				return { MusicContext::kTown, Evidence::kDistributedEditorID };
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

		[[nodiscard]] std::string TypeName(RE::BSIMusicType* a_type)
		{
			if (const auto musicType = a_type ? skyrim_cast<RE::BGSMusicType*>(a_type) : nullptr) {
				if (const std::string_view editorID{ musicType->GetFormEditorID() }; !editorID.empty()) {
					return std::string{ editorID };
				}
				return std::format("{:08X}", musicType->GetFormID());
			}
			return a_type ? "__NoMusic" : "<null>";
		}

		void LogDecision(RE::BSIMusicType* a_type, const Result& a_result)
		{
			logs::info("[context] type={} context={} evidence={}", TypeName(a_type),
				ContextName(a_result.context), EvidenceName(a_result.evidence));
		}

		void LogVerbose(RE::BSIMusicType* a_type, const Result& a_result)
		{
			if (const auto musicType = a_type ? skyrim_cast<RE::BGSMusicType*>(a_type) : nullptr) {
				logs::info("[classification-detail] pointer={} form={:08X} editorID='{}' context={} evidence={} priority={} flags={:08X}",
					static_cast<const void*>(a_type), musicType->GetFormID(), musicType->GetFormEditorID(),
					ContextName(a_result.context), EvidenceName(a_result.evidence),
					a_type->priority, a_type->flags.underlying());
			}
		}
	}

	Result Classify(RE::BSIMusicType* a_type) noexcept
	{
		try {
			std::scoped_lock lock{ g_cacheLock };
			auto [entry, inserted] = g_cache.try_emplace(a_type, CachedResult{ Compute(a_type), false, false });
			if (!inserted && entry->second.result.evidence == Evidence::kUnknown) {
				const auto refreshed = Compute(a_type);
				if (refreshed.context != entry->second.result.context || refreshed.evidence != entry->second.result.evidence) {
					entry->second.result = refreshed;
					entry->second.decisionLogged = false;
					entry->second.verboseLogged = false;
				}
			}
			if (!entry->second.decisionLogged) {
				LogDecision(a_type, entry->second.result);
				entry->second.decisionLogged = true;
			}
			if (!entry->second.verboseLogged && Settings::CurrentMusicDiagnosticsEnabled()) {
				LogVerbose(a_type, entry->second.result);
				entry->second.verboseLogged = true;
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
