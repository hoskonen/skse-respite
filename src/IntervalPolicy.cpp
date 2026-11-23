#include "IntervalPolicy.h"

#include <unordered_set>

namespace Respite::IntervalPolicy
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using TypeStatus = RE::BSIMusicType::MUSIC_STATUS;

		struct State
		{
			RE::BSIMusicType* pendingType{};
			RE::BSIMusicTrack* pendingTrack{};
			Settings::PolicySettings pendingSettings{};
			RE::BSIMusicType* originType{};
			Clock::time_point cooldownEnd{};
			bool cooldownActive{};
			std::unordered_set<RE::BSIMusicType*> suppressedTypes;
			std::unordered_set<RE::BSIMusicType*> allowedOtherTypes;
			std::unordered_set<RE::BGSMusicPaletteTrack*> blockedPalettes;
		};

		std::mutex g_stateLock;
		std::array<State, ContextIndex(MusicContext::kCount)> g_states;
		std::atomic_bool g_runtimeEnabled{ false };
		std::mt19937 g_random{ static_cast<std::mt19937::result_type>(Clock::now().time_since_epoch().count()) };

		void ClearLocked(MusicContext a_context) noexcept { g_states[ContextIndex(a_context)] = {}; }
		void ClearAllLocked() noexcept { g_states = {}; }

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

		[[nodiscard]] float RemainingSeconds(const State& a_state, Clock::time_point a_now) noexcept
		{
			return (std::max)(0.0F, std::chrono::duration<float>{ a_state.cooldownEnd - a_now }.count());
		}

		[[nodiscard]] float ChooseDuration(const Settings::PolicySettings& a_settings)
		{
			if (a_settings.minimumSilenceSeconds == a_settings.maximumSilenceSeconds) {
				return a_settings.minimumSilenceSeconds;
			}
			return std::uniform_real_distribution<float>{ a_settings.minimumSilenceSeconds,
				a_settings.maximumSilenceSeconds }(g_random);
		}

		void ExpireLocked(MusicContext a_context, Clock::time_point a_now)
		{
			auto& state = g_states[ContextIndex(a_context)];
			if (!state.cooldownActive || a_now < state.cooldownEnd) {
				return;
			}
			logs::info("[interval] cooldown expired context={} origin={}", ContextName(a_context), TypeName(state.originType));
			ClearLocked(a_context);
		}

		void StartCooldownLocked(MusicContext a_context, RE::BSIMusicType* a_type,
			RE::BSIMusicTrack* a_track, const Settings::PolicySettings& a_settings, Clock::time_point a_now)
		{
			auto& state = g_states[ContextIndex(a_context)];
			const auto duration = ChooseDuration(a_settings);
			state = {};
			state.originType = a_type;
			state.cooldownEnd = a_now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>{ duration });
			state.cooldownActive = true;
			logs::info("[interval] cooldown started context={} type={} duration={:.1f}s",
				ContextName(a_context), TypeName(a_type), duration);
			if (Settings::CurrentMusicDiagnosticsEnabled()) {
				logs::info("[interval-detail] owner={} completedTrack={} configuredRange={:.1f}..{:.1f}s",
					static_cast<const void*>(a_type), static_cast<const void*>(a_track),
					a_settings.minimumSilenceSeconds, a_settings.maximumSilenceSeconds);
			}
		}
	}

	void Reset(std::string_view a_reason, bool a_enable) noexcept
	{
		try {
			g_runtimeEnabled.store(false, std::memory_order_relaxed);
			std::scoped_lock lock{ g_stateLock };
			logs::debug("[interval] state reset reason={}", a_reason);
			ClearAllLocked();
			g_runtimeEnabled.store(a_enable, std::memory_order_relaxed);
		} catch (...) { logs::error("Interval policy exception while resetting state"); }
	}

	void ResetContext(MusicContext a_context, std::string_view a_reason) noexcept
	{
		try {
			if (a_context == MusicContext::kUnmanaged || a_context == MusicContext::kCount) return;
			std::scoped_lock lock{ g_stateLock };
			ClearLocked(a_context);
			logs::debug("[interval] context state reset context={} reason={}", ContextName(a_context), a_reason);
		} catch (...) { logs::error("Interval policy exception while resetting context state"); }
	}

	void Update() noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed)) return;
			std::scoped_lock lock{ g_stateLock };
			const auto now = Clock::now();
			for (const auto context : kManagedMusicContexts) ExpireLocked(context, now);
		} catch (...) { logs::error("Interval policy exception while updating cooldowns"); }
	}

	void OnNaturalCompletionCandidate(MusicContext a_context, RE::BSIMusicType* a_owner,
		RE::BSIMusicTrack* a_track, const Settings::PolicySettings& a_settings) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || a_context == MusicContext::kUnmanaged ||
				!a_owner || a_settings.policy != Settings::Policy::kInterval) return;
			std::scoped_lock lock{ g_stateLock };
			auto& state = g_states[ContextIndex(a_context)];
			ExpireLocked(a_context, Clock::now());
			if (!state.cooldownActive) {
				state.pendingType = a_owner;
				state.pendingTrack = a_track;
				state.pendingSettings = a_settings;
			}
		} catch (...) { logs::error("Interval policy exception while recording natural completion"); }
	}

	void OnTypeTransition(MusicContext a_context, RE::BSIMusicType* a_type, std::uint32_t a_before,
		std::uint32_t a_after, const Settings::PolicySettings& a_settings) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || a_context == MusicContext::kUnmanaged) return;
			std::scoped_lock lock{ g_stateLock };
			if (a_settings.policy != Settings::Policy::kInterval) {
				return;
			}
			auto& state = g_states[ContextIndex(a_context)];
			const auto now = Clock::now();
			ExpireLocked(a_context, now);
			if (!state.cooldownActive && state.pendingType == a_type &&
				a_before != static_cast<std::uint32_t>(TypeStatus::kInactive) &&
				a_after == static_cast<std::uint32_t>(TypeStatus::kInactive)) {
				StartCooldownLocked(a_context, a_type, state.pendingTrack, a_settings, now);
			}
		} catch (...) { logs::error("Interval policy exception while observing music type transition"); }
	}

	bool ShouldSuppressPlay(RE::BSIMusicType* a_type, MusicContext a_typeContext,
		const Settings::Values& a_settings, Settings::Policy a_resolvedPolicy) noexcept
	{
		try {
			std::scoped_lock lock{ g_stateLock };
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_settings.enabled) {
				ClearAllLocked();
				return false;
			}
			const auto now = Clock::now();
			for (const auto context : kManagedMusicContexts) {
				auto& state = g_states[ContextIndex(context)];
				if (!a_settings.UsesInterval(context)) {
					ClearLocked(context);
					continue;
				}
				ExpireLocked(context, now);
				if (!state.cooldownActive && state.pendingType == a_type && a_type &&
					a_type->typeStatus == TypeStatus::kInactive) {
					StartCooldownLocked(context, a_type, state.pendingTrack, state.pendingSettings, now);
				}
			}

			if (a_typeContext != MusicContext::kUnmanaged && a_resolvedPolicy == Settings::Policy::kInterval) {
				auto& contextState = g_states[ContextIndex(a_typeContext)];
				if (contextState.cooldownActive) {
					if (contextState.suppressedTypes.insert(a_type).second) {
						logs::info("[interval] suppressed context={} type={} remaining={:.1f}s",
							ContextName(a_typeContext), TypeName(a_type), RemainingSeconds(contextState, now));
					}
					return true;
				}
			}

			for (const auto cooldownContext : kManagedMusicContexts) {
				auto& state = g_states[ContextIndex(cooldownContext)];
				if (!state.cooldownActive || !state.allowedOtherTypes.insert(a_type).second) {
					continue;
				}
				logs::info("[interval] bypass allowed cooldownContext={} type={} typeContext={}",
					ContextName(cooldownContext), TypeName(a_type), ContextName(a_typeContext));
			}
			return false;
		} catch (...) {
			logs::error("Interval policy exception while evaluating playback; allowing native behavior");
			return false;
		}
	}

	bool IsContextCooldownActive(MusicContext a_context) noexcept
	{
		try {
			if (a_context == MusicContext::kUnmanaged || a_context == MusicContext::kCount) {
				return false;
			}
			std::scoped_lock lock{ g_stateLock };
			const auto& state = g_states[ContextIndex(a_context)];
			return g_runtimeEnabled.load(std::memory_order_relaxed) && state.cooldownActive &&
			       Clock::now() < state.cooldownEnd;
		} catch (...) {
			return false;
		}
	}

	bool BlockInactivePaletteUpdate(MusicContext a_context, RE::BSIMusicType* a_owner,
		RE::BGSMusicPaletteTrack* a_palette) noexcept
	{
		try {
			if (!a_owner || !a_palette || a_context == MusicContext::kUnmanaged || a_context == MusicContext::kCount) {
				return false;
			}
			std::scoped_lock lock{ g_stateLock };
			auto& state = g_states[ContextIndex(a_context)];
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !state.cooldownActive ||
			    Clock::now() >= state.cooldownEnd) {
				return false;
			}
			if (state.blockedPalettes.insert(a_palette).second) {
				logs::info("[interval] blocked inactive palette update context={} type={}",
					ContextName(a_context), TypeName(a_owner));
			}
			return true;
		} catch (...) {
			return false;
		}
	}
}
