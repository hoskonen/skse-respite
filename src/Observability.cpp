#include "Observability.h"

#include "PolicyEngine.h"
#include "Settings.h"

namespace Respite::Observability
{
	namespace
	{
		using TypeStatus = RE::BSIMusicType::MUSIC_STATUS;
		using TrackStatus = RE::BSIMusicTrack::MUSIC_STATUS;

		struct TypeSnapshot
		{
			std::uint32_t status{};
			std::uint32_t currentTrackIndex{};
			RE::BSIMusicTrack* selectedTrack{};
		};

		struct TrackSnapshot
		{
			std::uint32_t status{};
			bool handleValid{};
			bool handlePlaying{};
			std::uint32_t playbackPosition{};
			float duration{};
		};

		struct TrackObservation
		{
			TrackSnapshot snapshot{};
			RE::BSIMusicType* owner{};
			bool initialized{};
			bool finishRequested{};
		};

		struct ManagerSnapshot
		{
			RE::BSIMusicType* current{};
			std::size_t queueFingerprint{};
			std::uint32_t queueSize{};
			bool initialized{};
		};

		std::atomic_bool g_observationEnabled{ false };
		std::mutex g_stateLock;
		std::unordered_map<RE::BSIMusicType*, TypeSnapshot> g_typeStates;
		std::unordered_map<RE::BSIMusicTrack*, TrackObservation> g_trackStates;
		ManagerSnapshot g_managerState;

		thread_local RE::BSIMusicType* g_updatingType{};

		[[nodiscard]] std::string_view TypeStatusName(std::uint32_t a_status) noexcept
		{
			switch (static_cast<TypeStatus>(a_status)) {
			case TypeStatus::kInactive:
				return "inactive";
			case TypeStatus::kPlaying:
				return "playing";
			case TypeStatus::kPaused:
				return "paused";
			case TypeStatus::kFinishing:
				return "finishing";
			case TypeStatus::kFinished:
				return "finished";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] std::string_view TrackStatusName(std::uint32_t a_status) noexcept
		{
			switch (static_cast<TrackStatus>(a_status)) {
			case TrackStatus::kInactive:
				return "inactive";
			case TrackStatus::kPlaying:
				return "playing";
			case TrackStatus::kPaused:
				return "paused";
			case TrackStatus::kFinishing:
				return "finishing";
			case TrackStatus::kFinished:
				return "finished";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] std::string_view TrackTypeName(RE::BSIMusicTrack::TrackType a_type) noexcept
		{
			switch (a_type) {
			case RE::BSIMusicTrack::TrackType::kSilentTrack:
				return "silent";
			case RE::BSIMusicTrack::TrackType::kSingleTrack:
				return "single";
			case RE::BSIMusicTrack::TrackType::kPalette:
				return "palette";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] std::string_view MessageName(RE::BSMusicEvent::MUSIC_MESSAGE_TYPE a_type) noexcept
		{
			switch (a_type) {
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kAdd:
				return "add";
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kRemove:
				return "remove";
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kRemoveImmediate:
				return "remove-immediate";
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kPause:
				return "pause";
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kUnpause:
				return "unpause";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] bool IsNoMusic(const RE::BSIMusicType* a_type) noexcept
		{
			if (!a_type) {
				return false;
			}

			static REL::Relocation<std::uintptr_t> noMusicVTable{ RE::VTABLE___NoMusic[0] };
			return *reinterpret_cast<const std::uintptr_t*>(a_type) == noMusicVTable.address();
		}

		[[nodiscard]] RE::BGSMusicType* AsMusicType(RE::BSIMusicType* a_type) noexcept
		{
			if (!a_type || IsNoMusic(a_type)) {
				return nullptr;
			}

			return skyrim_cast<RE::BGSMusicType*>(a_type);
		}

		void LogType(std::string_view a_prefix, RE::BSIMusicType* a_type)
		{
			if (!a_type) {
				logs::info("{} type=<null>", a_prefix);
				return;
			}

			if (IsNoMusic(a_type)) {
				logs::info("{} type={} class=__NoMusic", a_prefix, static_cast<const void*>(a_type));
				return;
			}

			if (const auto musicType = AsMusicType(a_type)) {
				logs::info(
					"{} type={} form={:08X} editorID='{}' priority={} flags={:08X} playsOnce={} status={} trackIndex={} tracks={}",
					a_prefix,
					static_cast<const void*>(a_type),
					musicType->GetFormID(),
					musicType->GetFormEditorID(),
					a_type->priority,
					a_type->flags.underlying(),
					a_type->flags.any(RE::BSIMusicType::MST::kPlaysOnce),
					TypeStatusName(a_type->typeStatus.underlying()),
					a_type->currentTrackIndex,
					a_type->tracks.size());
				return;
			}

			logs::info(
				"{} type={} class=<unknown BSIMusicType> priority={} flags={:08X} playsOnce={} status={} trackIndex={} tracks={}",
				a_prefix,
				static_cast<const void*>(a_type),
				a_type->priority,
				a_type->flags.underlying(),
				a_type->flags.any(RE::BSIMusicType::MST::kPlaysOnce),
				TypeStatusName(a_type->typeStatus.underlying()),
				a_type->currentTrackIndex,
				a_type->tracks.size());
		}

		void LogTrack(std::string_view a_prefix, RE::BSIMusicTrack* a_track, RE::BSIMusicType* a_owner)
		{
			if (!a_track) {
				logs::info("{} track=<null> owner={}", a_prefix, static_cast<const void*>(a_owner));
				return;
			}

			const auto type = a_track->GetType();
			const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_track);
			logs::info(
				"{} track={} owner={} kind={}({:08X}) status={} wrapperForm={:08X} wrappedTrack={}",
				a_prefix,
				static_cast<const void*>(a_track),
				static_cast<const void*>(a_owner),
				TrackTypeName(type),
				static_cast<std::uint32_t>(type),
				TrackStatusName(a_track->trackStatus.underlying()),
				wrapper ? wrapper->GetFormID() : 0,
				wrapper ? static_cast<const void*>(wrapper->track) : nullptr);
		}

		[[nodiscard]] TypeSnapshot CaptureType(RE::BSIMusicType* a_type) noexcept
		{
			TypeSnapshot result{};
			if (!a_type) {
				return result;
			}

			result.status = a_type->typeStatus.underlying();
			result.currentTrackIndex = a_type->currentTrackIndex;
			if (result.currentTrackIndex < a_type->tracks.size()) {
				result.selectedTrack = a_type->tracks[result.currentTrackIndex];
			}
			return result;
		}

		[[nodiscard]] RE::BSIMusicTrack* SelectedTopLevelTrack(RE::BSIMusicType* a_owner) noexcept
		{
			if (!a_owner || a_owner->currentTrackIndex >= a_owner->tracks.size()) {
				return nullptr;
			}
			auto* selected = a_owner->tracks[a_owner->currentTrackIndex];
			if (const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(selected)) {
				selected = wrapper->track;
			}
			return selected;
		}

		[[nodiscard]] bool IsSelectedTopLevelCompletion(
			RE::BSIMusicType* a_owner,
			RE::BSIMusicTrack* a_completedTrack) noexcept
		{
			return a_completedTrack && SelectedTopLevelTrack(a_owner) == a_completedTrack;
		}

		template <class T>
		[[nodiscard]] TrackSnapshot CaptureTrack(T* a_track) noexcept
		{
			TrackSnapshot result{};
			if (a_track) {
				result.status = a_track->trackStatus.underlying();
			}
			return result;
		}

		template <>
		[[nodiscard]] TrackSnapshot CaptureTrack(RE::BGSMusicSingleTrack* a_track) noexcept
		{
			TrackSnapshot result{};
			if (!a_track) {
				return result;
			}

			result.status = a_track->trackStatus.underlying();
			result.playbackPosition = a_track->lastKnownPlaybackPosition;
			result.handleValid = a_track->trackHandle.soundID != RE::BSSoundHandle::kInvalidID;
			result.handlePlaying = a_track->trackHandle.state == RE::BSSoundHandle::AssumedState::kPlaying;
			return result;
		}

		template <>
		[[nodiscard]] TrackSnapshot CaptureTrack(RE::BGSMusicPaletteTrack* a_track) noexcept
		{
			TrackSnapshot result{};
			if (a_track) {
				result.status = a_track->trackStatus.underlying();
				result.duration = a_track->duration;
			}
			return result;
		}

		template <>
		[[nodiscard]] TrackSnapshot CaptureTrack(RE::BGSMusicSilenceTrack* a_track) noexcept
		{
			TrackSnapshot result{};
			if (a_track) {
				result.status = a_track->trackStatus.underlying();
				result.duration = a_track->duration;
			}
			return result;
		}

		[[nodiscard]] std::size_t QueueFingerprint(const RE::BSMusicManager* a_manager) noexcept
		{
			if (!a_manager) {
				return 0;
			}

			std::size_t result = a_manager->musicQueue.size();
			for (const auto type : a_manager->musicQueue) {
				const auto value = reinterpret_cast<std::uintptr_t>(type);
				result ^= value + 0x9E3779B97F4A7C15ull + (result << 6) + (result >> 2);
			}
			return result;
		}

		void ObserveManager(std::string_view a_reason)
		{
			if (!g_observationEnabled.load(std::memory_order_relaxed) || !Settings::CurrentMusicDiagnosticsEnabled()) {
				return;
			}

			const auto manager = RE::BSMusicManager::GetSingleton();
			if (!manager) {
				return;
			}

			const auto fingerprint = QueueFingerprint(manager);
			bool currentChanged{};
			bool queueChanged{};
			RE::BSIMusicType* previousCurrent{};
			{
				std::scoped_lock lock{ g_stateLock };
				previousCurrent = g_managerState.current;
				currentChanged = !g_managerState.initialized || previousCurrent != manager->current;
				queueChanged = !g_managerState.initialized || g_managerState.queueFingerprint != fingerprint || g_managerState.queueSize != manager->musicQueue.size();
				g_managerState = { manager->current, fingerprint, manager->musicQueue.size(), true };
			}

			if (currentChanged) {
				logs::info(
					"[manager-current] reason={} previous={} current={}",
					a_reason,
					static_cast<const void*>(previousCurrent),
					static_cast<const void*>(manager->current));
				LogType("[manager-current-detail]", manager->current);
			}

			if (queueChanged) {
				logs::info("[manager-queue] reason={} size={}", a_reason, manager->musicQueue.size());
				for (std::uint32_t i = 0; i < manager->musicQueue.size(); ++i) {
					const auto type = manager->musicQueue[i];
					logs::info(
						"[manager-queue-entry] index={} type={} priority={} status={} noMusic={}",
						i,
						static_cast<const void*>(type),
						type ? type->priority : 0,
						type ? TypeStatusName(type->typeStatus.underlying()) : "null",
						IsNoMusic(type));
				}
			}
		}

		void ObserveType(RE::BSIMusicType* a_type, TypeSnapshot a_before, std::string_view a_reason)
		{
			if (!g_observationEnabled.load(std::memory_order_relaxed) || !Settings::CurrentMusicDiagnosticsEnabled() || !a_type) {
				return;
			}

			const auto after = CaptureType(a_type);
			bool firstObservation{};
			auto previous = a_before;
			{
				std::scoped_lock lock{ g_stateLock };
				const auto existing = g_typeStates.find(a_type);
				firstObservation = existing == g_typeStates.end();
				if (!firstObservation) {
					previous = existing->second;
				}
				g_typeStates[a_type] = after;
			}

			if (firstObservation || previous.status != after.status) {
				logs::info(
					"[type-status] reason={} type={} {} -> {}",
					a_reason,
					static_cast<const void*>(a_type),
					TypeStatusName(previous.status),
					TypeStatusName(after.status));
				LogType("[type-status-detail]", a_type);
			}

			if (firstObservation || previous.currentTrackIndex != after.currentTrackIndex || previous.selectedTrack != after.selectedTrack) {
				logs::info(
					"[track-selection] reason={} type={} index={} -> {} track={} -> {}",
					a_reason,
					static_cast<const void*>(a_type),
					previous.currentTrackIndex,
					after.currentTrackIndex,
					static_cast<const void*>(previous.selectedTrack),
					static_cast<const void*>(after.selectedTrack));
				LogTrack("[track-selection-detail]", after.selectedTrack, a_type);
			}
		}

		template <class T>
		void ObserveTrack(T* a_track, TrackSnapshot a_before, std::string_view a_kind, std::string_view a_reason)
		{
			if (!g_observationEnabled.load(std::memory_order_relaxed) || !Settings::CurrentMusicDiagnosticsEnabled() || !a_track) {
				return;
			}

			const auto after = CaptureTrack(a_track);
			bool firstObservation{};
			bool finishRequested{};
			RE::BSIMusicType* owner{};
			auto previous = a_before;
			{
				std::scoped_lock lock{ g_stateLock };
				auto& observation = g_trackStates[a_track];
				firstObservation = !observation.initialized;
				if (!firstObservation) {
					previous = observation.snapshot;
				}
				finishRequested = observation.finishRequested;
				if (g_updatingType) {
					observation.owner = g_updatingType;
				}
				owner = observation.owner;
				observation.snapshot = after;
				observation.initialized = true;
			}

			if (firstObservation || previous.status != after.status) {
				logs::info(
					"[track-status] reason={} kind={} track={} owner={} {} -> {} handleValid={} handlePlaying={} position={} duration={}",
					a_reason,
					a_kind,
					static_cast<const void*>(a_track),
					static_cast<const void*>(owner),
					TrackStatusName(previous.status),
					TrackStatusName(after.status),
					after.handleValid,
					after.handlePlaying,
					after.playbackPosition,
					after.duration);
			}

			if (a_reason == "DoUpdate" && a_before.status != static_cast<std::uint32_t>(TrackStatus::kFinished) && after.status == static_cast<std::uint32_t>(TrackStatus::kFinished)) {
				logs::info(
					"[track-completion] kind={} track={} owner={} classification={}",
					a_kind,
					static_cast<const void*>(a_track),
					static_cast<const void*>(owner),
					finishRequested ? "finished-after-observed-DoFinish" : "natural-completion-candidate");
			}
		}

		void Reset(std::string_view a_reason, bool a_enable)
		{
			g_observationEnabled.store(false, std::memory_order_relaxed);
			{
				std::scoped_lock lock{ g_stateLock };
				g_typeStates.clear();
				g_trackStates.clear();
				g_managerState = {};
			}
			logs::info("[lifecycle] {}: diagnostic state reset", a_reason);
			g_observationEnabled.store(a_enable, std::memory_order_relaxed);
			if (a_enable) {
				ObserveManager(a_reason);
			}
		}

		struct UpdatingTypeScope
		{
			explicit UpdatingTypeScope(RE::BSIMusicType* a_type) noexcept :
				previous(g_updatingType)
			{
				g_updatingType = a_type;
			}

			~UpdatingTypeScope() { g_updatingType = previous; }

			RE::BSIMusicType* previous;
		};

		struct MusicManagerProcessEventHook
		{
			static RE::BSEventNotifyControl Thunk(
				RE::BSTEventSink<RE::BSMusicEvent>* a_sink,
				const RE::BSMusicEvent* a_event,
				RE::BSTEventSource<RE::BSMusicEvent>* a_source)
			{
				const auto result = func(a_sink, a_event, a_source);
				PolicyEngine::OnMusicEvent(a_event);
				if (g_observationEnabled.load(std::memory_order_relaxed) && Settings::CurrentMusicDiagnosticsEnabled()) {
					try {
						if (a_event) {
							logs::info(
								"[music-event] message={} type={}",
								MessageName(a_event->msgType.get()),
								static_cast<const void*>(a_event->musicType));
							LogType("[music-event-detail]", a_event->musicType);
						}
						ObserveManager("BSMusicManager::ProcessEvent");
					} catch (...) {
						logs::error("Diagnostic exception after BSMusicManager::ProcessEvent");
					}
				}
				return result;
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};

		struct MusicTypeUpdateHook
		{
			static void Thunk(RE::BSIMusicType* a_type)
			{
				const auto before = CaptureType(a_type);
				PolicyEngine::Update();
				{
					UpdatingTypeScope scope{ a_type };
					func(a_type);
				}
				const auto after = CaptureType(a_type);
				if (before.status != after.status) {
					PolicyEngine::OnTypeTransition(a_type, before.status, after.status);
				}
				try {
					ObserveType(a_type, before, "DoUpdate");
					ObserveManager("BGSMusicType::DoUpdate");
				} catch (...) {
					logs::error("Diagnostic exception after BGSMusicType::DoUpdate");
				}
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};

		struct MusicTypePlayHook
		{
			static void Thunk(RE::BSIMusicType* a_type)
			{
				if (PolicyEngine::ShouldSuppressPlay(a_type)) {
					return;
				}

				const auto before = CaptureType(a_type);
				{
					UpdatingTypeScope scope{ a_type };
					func(a_type);
				}
				try {
					ObserveType(a_type, before, "DoPlay");
				} catch (...) {
					logs::error("Diagnostic exception after BGSMusicType::DoPlay");
				}
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};

		struct MusicTypeFinishHook
		{
			static void Thunk(RE::BSIMusicType* a_type, bool a_immediate)
			{
				const auto before = CaptureType(a_type);
				{
					UpdatingTypeScope scope{ a_type };
					func(a_type, a_immediate);
				}
				const auto after = CaptureType(a_type);
				if (before.status != after.status) {
					PolicyEngine::OnTypeTransition(a_type, before.status, after.status);
				}
				try {
					if (g_observationEnabled.load(std::memory_order_relaxed) && Settings::CurrentMusicDiagnosticsEnabled()) {
						logs::info("[type-finish-request] type={} immediate={}", static_cast<const void*>(a_type), a_immediate);
					}
					ObserveType(a_type, before, "DoFinish");
				} catch (...) {
					logs::error("Diagnostic exception after BGSMusicType::DoFinish");
				}
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};

		template <class T, const char* KindName>
		struct TrackHooks
		{
			static void Update(T* a_track)
			{
				const auto before = CaptureTrack(a_track);
				const auto owner = g_updatingType;
				update(a_track);
				const auto after = CaptureTrack(a_track);
				bool finishRequested{};
				{
					std::scoped_lock lock{ g_stateLock };
					if (const auto existing = g_trackStates.find(a_track); existing != g_trackStates.end()) {
						finishRequested = existing->second.finishRequested;
					}
				}
				const auto reachedFinished = before.status != static_cast<std::uint32_t>(TrackStatus::kFinished) &&
				                           after.status == static_cast<std::uint32_t>(TrackStatus::kFinished);
				const auto naturalCandidate = reachedFinished && !finishRequested;
				const auto selectedTopLevel = naturalCandidate && IsSelectedTopLevelCompletion(owner, a_track);
				if constexpr (std::same_as<T, RE::BGSMusicSilenceTrack>) {
					if (selectedTopLevel) {
						PolicyEngine::OnNativeSilenceCompletion(owner, a_track);
					}
				} else {
					if (selectedTopLevel) {
						PolicyEngine::OnNaturalCompletionCandidate(owner, a_track);
					}
				}
				try {
					ObserveTrack(a_track, before, KindName, "DoUpdate");
				} catch (...) {
					logs::error("Diagnostic exception after {}::DoUpdate", KindName);
				}
			}

			static void Play(T* a_track)
			{
				const auto before = CaptureTrack(a_track);
				play(a_track);
				try {
					{
						std::scoped_lock lock{ g_stateLock };
						g_trackStates[a_track].finishRequested = false;
					}
					ObserveTrack(a_track, before, KindName, "DoPlay");
				} catch (...) {
					logs::error("Diagnostic exception after {}::DoPlay", KindName);
				}
			}

			static void Finish(T* a_track, bool a_immediate, float a_fadeTime)
			{
				const auto before = CaptureTrack(a_track);
				finish(a_track, a_immediate, a_fadeTime);
				try {
					{
						std::scoped_lock lock{ g_stateLock };
						g_trackStates[a_track].finishRequested = true;
					}
					if (g_observationEnabled.load(std::memory_order_relaxed) && Settings::CurrentMusicDiagnosticsEnabled()) {
						logs::info(
							"[track-finish-request] kind={} track={} owner={} immediate={} fadeTime={}",
							KindName,
							static_cast<const void*>(a_track),
							static_cast<const void*>(g_updatingType),
							a_immediate,
							a_fadeTime);
					}
					ObserveTrack(a_track, before, KindName, "DoFinish");
				} catch (...) {
					logs::error("Diagnostic exception after {}::DoFinish", KindName);
				}
			}

			static inline REL::Relocation<decltype(Update)> update;
			static inline REL::Relocation<decltype(Play)> play;
			static inline REL::Relocation<decltype(Finish)> finish;
		};

		inline constexpr char kSingleTrackName[] = "BGSMusicSingleTrack";
		inline constexpr char kPaletteTrackName[] = "BGSMusicPaletteTrack";
		inline constexpr char kSilenceTrackName[] = "BGSMusicSilenceTrack";

		using SingleTrackHooks = TrackHooks<RE::BGSMusicSingleTrack, kSingleTrackName>;
		using PaletteTrackHooks = TrackHooks<RE::BGSMusicPaletteTrack, kPaletteTrackName>;
		using SilenceTrackHooks = TrackHooks<RE::BGSMusicSilenceTrack, kSilenceTrackName>;

		template <class Hooks>
		void InstallTrackHooks(REL::VariantID a_vtable)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_vtable };
			Hooks::update = vtable.write_vfunc(1, Hooks::Update);
			Hooks::play = vtable.write_vfunc(2, Hooks::Play);
			Hooks::finish = vtable.write_vfunc(4, Hooks::Finish);
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> managerVTable{ RE::VTABLE_BSMusicManager[0] };
		MusicManagerProcessEventHook::func = managerVTable.write_vfunc(1, MusicManagerProcessEventHook::Thunk);

		REL::Relocation<std::uintptr_t> musicTypeVTable{ RE::VTABLE_BGSMusicType[1] };
		MusicTypeUpdateHook::func = musicTypeVTable.write_vfunc(0, MusicTypeUpdateHook::Thunk);
		MusicTypePlayHook::func = musicTypeVTable.write_vfunc(1, MusicTypePlayHook::Thunk);
		MusicTypeFinishHook::func = musicTypeVTable.write_vfunc(3, MusicTypeFinishHook::Thunk);

		InstallTrackHooks<SingleTrackHooks>(RE::VTABLE_BGSMusicSingleTrack[0]);
		InstallTrackHooks<PaletteTrackHooks>(RE::VTABLE_BGSMusicPaletteTrack[0]);
		InstallTrackHooks<SilenceTrackHooks>(RE::VTABLE_BGSMusicSilenceTrack[0]);

		logs::info("Installed music observability hooks and optional interval replay gate");
		logs::info("  BSMusicManager::ProcessEvent -> VTABLE_BSMusicManager[0], slot 1");
		logs::info("  BGSMusicType::DoUpdate/DoPlay/DoFinish -> VTABLE_BGSMusicType[1], slots 0/1/3");
		logs::info("  Concrete track DoUpdate/DoPlay/DoFinish -> primary vtables, slots 1/2/4");
		logs::info("  __NoMusic -> identity observation only; no hook installed");
	}

	void OnDataLoaded()
	{
		Reset("kDataLoaded", true);
	}

	void OnPreLoadGame()
	{
		Reset("kPreLoadGame", false);
	}

	void OnPostLoadGame(bool a_succeeded)
	{
		Reset(a_succeeded ? "kPostLoadGame(success)" : "kPostLoadGame(failure)", a_succeeded);
	}

	void OnNewGame()
	{
		Reset("kNewGame", true);
	}
}
