#include "PaletteDiagnostics.h"

#include "IntervalPolicy.h"
#include "MusicClassification.h"
#include "Settings.h"

#include <unordered_set>

namespace Respite::PaletteDiagnostics
{
	namespace
	{
		struct ChildState
		{
			std::int32_t index{ -1 };
			RE::BSIMusicTrack* entry{};
			RE::BGSMusicTrackFormWrapper* wrapper{};
			RE::BSIMusicTrack* concrete{};
			std::uint32_t status{};
			std::uint32_t soundID{ RE::BSSoundHandle::kInvalidID };
			bool handleValid{};
			bool handlePlaying{};
		};

		struct Snapshot
		{
			bool valid{};
			RE::BSIMusicType* owner{};
			RE::FormID ownerForm{};
			std::string ownerEditorID;
			std::uint32_t ownerStatus{};
			MusicContext context{ MusicContext::kUnmanaged };
			bool intervalBlocked{};
			RE::BSIMusicTrack* selectedEntry{};
			RE::BGSMusicTrackFormWrapper* selectedWrapper{};
			RE::BGSMusicPaletteTrack* palette{};
			std::uint32_t paletteStatus{};
			float paletteDuration{};
			std::array<ChildState, 3> children{};
		};

		std::atomic_uint64_t g_sequence{};
		std::mutex g_logStateLock;
		std::unordered_set<RE::BSIMusicType*> g_suppressedTypePlayLogged;

		thread_local Snapshot g_typePlayBefore;
		thread_local Snapshot g_typeUpdateBefore;
		thread_local Snapshot g_palettePlayBefore;
		thread_local Snapshot g_paletteUpdateBefore;

		[[nodiscard]] bool Enabled() noexcept
		{
			return Settings::CurrentMusicDiagnosticsEnabled();
		}

		[[nodiscard]] RE::BSIMusicTrack* Unwrap(RE::BSIMusicTrack* a_track) noexcept
		{
			if (const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_track)) {
				return wrapper->track;
			}
			return a_track;
		}

		[[nodiscard]] std::string_view TypeStatusName(std::uint32_t a_status) noexcept
		{
			switch (static_cast<RE::BSIMusicType::MUSIC_STATUS>(a_status)) {
			case RE::BSIMusicType::MUSIC_STATUS::kInactive:
				return "inactive";
			case RE::BSIMusicType::MUSIC_STATUS::kPlaying:
				return "playing";
			case RE::BSIMusicType::MUSIC_STATUS::kPaused:
				return "paused";
			case RE::BSIMusicType::MUSIC_STATUS::kFinishing:
				return "finishing";
			case RE::BSIMusicType::MUSIC_STATUS::kFinished:
				return "finished";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] std::string_view TrackStatusName(std::uint32_t a_status) noexcept
		{
			switch (static_cast<RE::BSIMusicTrack::MUSIC_STATUS>(a_status)) {
			case RE::BSIMusicTrack::MUSIC_STATUS::kInactive:
				return "inactive";
			case RE::BSIMusicTrack::MUSIC_STATUS::kPlaying:
				return "playing";
			case RE::BSIMusicTrack::MUSIC_STATUS::kPaused:
				return "paused";
			case RE::BSIMusicTrack::MUSIC_STATUS::kFinishing:
				return "finishing";
			case RE::BSIMusicTrack::MUSIC_STATUS::kFinished:
				return "finished";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] std::string_view TrackKind(RE::BSIMusicTrack* a_track) noexcept
		{
			if (!a_track) {
				return "null";
			}
			switch (a_track->GetType()) {
			case RE::BSIMusicTrack::TrackType::kSingleTrack:
				return "single";
			case RE::BSIMusicTrack::TrackType::kSilentTrack:
				return "silence";
			case RE::BSIMusicTrack::TrackType::kPalette:
				return "palette";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] Snapshot Capture(RE::BSIMusicType* a_owner, RE::BGSMusicPaletteTrack* a_palette = nullptr)
		{
			Snapshot result{};
			if (!Enabled() || !a_owner || a_owner->currentTrackIndex >= a_owner->tracks.size()) {
				return result;
			}

			const auto classification = MusicClassification::Classify(a_owner);
			if (classification.context != MusicContext::kExploration) {
				return result;
			}

			result.selectedEntry = a_owner->tracks[a_owner->currentTrackIndex];
			result.selectedWrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(result.selectedEntry);
			const auto selectedConcrete = Unwrap(result.selectedEntry);
			const auto selectedPalette = skyrim_cast<RE::BGSMusicPaletteTrack*>(selectedConcrete);
			if (!selectedPalette || (a_palette && selectedPalette != a_palette)) {
				return result;
			}

			result.valid = true;
			result.owner = a_owner;
			result.ownerStatus = a_owner->typeStatus.underlying();
			result.context = classification.context;
			result.intervalBlocked = IntervalPolicy::IsContextCooldownActive(classification.context);
			result.palette = selectedPalette;
			result.paletteStatus = selectedPalette->trackStatus.underlying();
			result.paletteDuration = selectedPalette->duration;
			if (const auto musicType = skyrim_cast<RE::BGSMusicType*>(a_owner)) {
				result.ownerForm = musicType->GetFormID();
				result.ownerEditorID = musicType->GetFormEditorID();
			}

			for (std::size_t layer = 0; layer < result.children.size(); ++layer) {
				auto& child = result.children[layer];
				child.index = selectedPalette->playingIndexes[layer];
				if (child.index < 0 || static_cast<std::uint32_t>(child.index) >= selectedPalette->layers[layer].size()) {
					continue;
				}
				child.entry = selectedPalette->layers[layer][child.index];
				child.wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(child.entry);
				child.concrete = Unwrap(child.entry);
				if (!child.concrete) {
					continue;
				}
				child.status = child.concrete->trackStatus.underlying();
				if (const auto single = skyrim_cast<RE::BGSMusicSingleTrack*>(child.concrete)) {
					child.soundID = single->trackHandle.soundID;
					child.handleValid = child.soundID != RE::BSSoundHandle::kInvalidID;
					child.handlePlaying = single->trackHandle.state == RE::BSSoundHandle::AssumedState::kPlaying;
				}
			}
			return result;
		}

		[[nodiscard]] bool Same(const Snapshot& a_lhs, const Snapshot& a_rhs) noexcept
		{
			if (a_lhs.valid != a_rhs.valid || a_lhs.ownerStatus != a_rhs.ownerStatus ||
			    a_lhs.intervalBlocked != a_rhs.intervalBlocked || a_lhs.selectedEntry != a_rhs.selectedEntry ||
			    a_lhs.palette != a_rhs.palette || a_lhs.paletteStatus != a_rhs.paletteStatus) {
				return false;
			}
			for (std::size_t layer = 0; layer < a_lhs.children.size(); ++layer) {
				const auto& lhs = a_lhs.children[layer];
				const auto& rhs = a_rhs.children[layer];
				if (lhs.index != rhs.index || lhs.entry != rhs.entry || lhs.concrete != rhs.concrete ||
				    lhs.status != rhs.status || lhs.soundID != rhs.soundID || lhs.handleValid != rhs.handleValid ||
				    lhs.handlePlaying != rhs.handlePlaying) {
					return false;
				}
			}
			return true;
		}

		void LogSnapshot(std::string_view a_event, const Snapshot& a_snapshot)
		{
			if (!a_snapshot.valid) {
				return;
			}
			const auto sequence = g_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
			logs::info(
				"[palette-focus] seq={} event={} owner={} form={:08X} editorID='{}' ownerStatus={} context={} intervalBlocked={} selectedEntry={} selectedWrapper={} wrapperForm={:08X} palette={} paletteStatus={} duration={} indexes=[{},{},{}]",
				sequence,
				a_event,
				static_cast<const void*>(a_snapshot.owner),
				a_snapshot.ownerForm,
				a_snapshot.ownerEditorID,
				TypeStatusName(a_snapshot.ownerStatus),
				ContextName(a_snapshot.context),
				a_snapshot.intervalBlocked,
				static_cast<const void*>(a_snapshot.selectedEntry),
				static_cast<const void*>(a_snapshot.selectedWrapper),
				a_snapshot.selectedWrapper ? a_snapshot.selectedWrapper->GetFormID() : 0,
				static_cast<const void*>(a_snapshot.palette),
				TrackStatusName(a_snapshot.paletteStatus),
				a_snapshot.paletteDuration,
				a_snapshot.children[0].index,
				a_snapshot.children[1].index,
				a_snapshot.children[2].index);
			for (std::size_t layer = 0; layer < a_snapshot.children.size(); ++layer) {
				const auto& child = a_snapshot.children[layer];
				logs::info(
					"[palette-focus-child] seq={} event={} layer={} index={} entry={} wrapper={} wrapperForm={:08X} concrete={} kind={} status={} soundID={:08X} handleValid={} handlePlaying={}",
					sequence,
					a_event,
					layer,
					child.index,
					static_cast<const void*>(child.entry),
					static_cast<const void*>(child.wrapper),
					child.wrapper ? child.wrapper->GetFormID() : 0,
					static_cast<const void*>(child.concrete),
					TrackKind(child.concrete),
					TrackStatusName(child.status),
					child.soundID,
					child.handleValid,
					child.handlePlaying);
			}
		}

		void LogChangedPair(std::string_view a_beforeEvent, const Snapshot& a_before,
			std::string_view a_afterEvent, const Snapshot& a_after)
		{
			if ((a_before.valid || a_after.valid) && !Same(a_before, a_after)) {
				LogSnapshot(a_beforeEvent, a_before);
				LogSnapshot(a_afterEvent, a_after);
			}
		}

		void LogTriggerChild(std::string_view a_event, RE::BGSMusicSingleTrack* a_child,
			RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner)
		{
			const auto snapshot = Capture(a_owner, a_palette);
			if (!snapshot.valid || !a_child) {
				return;
			}
			std::int32_t memberLayer{ -1 };
			std::int32_t memberIndex{ -1 };
			for (std::size_t layer = 0; layer < std::size(a_palette->layers) && memberLayer < 0; ++layer) {
				for (std::uint32_t index = 0; index < a_palette->layers[layer].size(); ++index) {
					if (Unwrap(a_palette->layers[layer][index]) == a_child) {
						memberLayer = static_cast<std::int32_t>(layer);
						memberIndex = static_cast<std::int32_t>(index);
						break;
					}
				}
			}
			const auto valid = a_child->trackHandle.soundID != RE::BSSoundHandle::kInvalidID;
			logs::info(
				"[palette-child-play] event={} owner={} ownerStatus={} palette={} paletteStatus={} intervalBlocked={} child={} memberLayer={} memberIndex={} childStatus={} soundID={:08X} handleValid={} handlePlaying={}",
				a_event,
				static_cast<const void*>(a_owner),
				TypeStatusName(snapshot.ownerStatus),
				static_cast<const void*>(a_palette),
				TrackStatusName(snapshot.paletteStatus),
				snapshot.intervalBlocked,
				static_cast<const void*>(a_child),
				memberLayer,
				memberIndex,
				TrackStatusName(a_child->trackStatus.underlying()),
				a_child->trackHandle.soundID,
				valid,
				a_child->trackHandle.state == RE::BSSoundHandle::AssumedState::kPlaying);
			LogSnapshot(a_event, snapshot);
		}
	}

	void Reset() noexcept
	{
		try {
			std::scoped_lock lock{ g_logStateLock };
			g_suppressedTypePlayLogged.clear();
			g_sequence.store(0, std::memory_order_relaxed);
		} catch (...) {}
	}

	void BeforeTypePlay(RE::BSIMusicType* a_owner) noexcept
	{
		try { g_typePlayBefore = Capture(a_owner); } catch (...) { g_typePlayBefore = {}; }
	}

	void TypePlaySuppressed(RE::BSIMusicType* a_owner) noexcept
	{
		try {
			if (!g_typePlayBefore.valid) return;
			bool first{};
			{
				std::scoped_lock lock{ g_logStateLock };
				first = g_suppressedTypePlayLogged.insert(a_owner).second;
			}
			if (first) {
				LogSnapshot("BGSMusicType::DoPlay.before", g_typePlayBefore);
				LogSnapshot("BGSMusicType::DoPlay.suppressed-original-not-called", Capture(a_owner));
			}
		} catch (...) {}
	}

	void AfterTypePlay(RE::BSIMusicType* a_owner) noexcept
	{
		try {
			{
				std::scoped_lock lock{ g_logStateLock };
				g_suppressedTypePlayLogged.erase(a_owner);
			}
			LogSnapshot("BGSMusicType::DoPlay.before", g_typePlayBefore);
			LogSnapshot("BGSMusicType::DoPlay.after", Capture(a_owner));
		} catch (...) {}
	}

	void BeforeTypeUpdate(RE::BSIMusicType* a_owner) noexcept
	{
		try { g_typeUpdateBefore = Capture(a_owner); } catch (...) { g_typeUpdateBefore = {}; }
	}

	void AfterTypeUpdate(RE::BSIMusicType* a_owner) noexcept
	{
		try { LogChangedPair("BGSMusicType::DoUpdate.before", g_typeUpdateBefore,
			"BGSMusicType::DoUpdate.after", Capture(a_owner)); } catch (...) {}
	}

	void BeforePalettePlay(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept
	{
		try { g_palettePlayBefore = Capture(a_owner, a_palette); LogSnapshot("BGSMusicPaletteTrack::DoPlay.before", g_palettePlayBefore); } catch (...) { g_palettePlayBefore = {}; }
	}

	void AfterPalettePlay(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept
	{
		try { LogSnapshot("BGSMusicPaletteTrack::DoPlay.after", Capture(a_owner, a_palette)); } catch (...) {}
	}

	void BeforePaletteUpdate(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept
	{
		try { g_paletteUpdateBefore = Capture(a_owner, a_palette); } catch (...) { g_paletteUpdateBefore = {}; }
	}

	void AfterPaletteUpdate(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept
	{
		try { LogChangedPair("BGSMusicPaletteTrack::DoUpdate.before", g_paletteUpdateBefore,
			"BGSMusicPaletteTrack::DoUpdate.after", Capture(a_owner, a_palette)); } catch (...) {}
	}

	void BeforeChildPlay(RE::BGSMusicSingleTrack* a_child, RE::BGSMusicPaletteTrack* a_palette,
		RE::BSIMusicType* a_owner) noexcept
	{
		try { LogTriggerChild("BGSMusicSingleTrack::DoPlay.before", a_child, a_palette, a_owner); } catch (...) {}
	}

	void AfterChildPlay(RE::BGSMusicSingleTrack* a_child, RE::BGSMusicPaletteTrack* a_palette,
		RE::BSIMusicType* a_owner) noexcept
	{
		try { LogTriggerChild("BGSMusicSingleTrack::DoPlay.after", a_child, a_palette, a_owner); } catch (...) {}
	}
}
