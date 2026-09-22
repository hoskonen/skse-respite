#include "IntervalPolicy.h"

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
			RE::BSIMusicType* managedType{};
			Clock::time_point cooldownEnd{};
			bool cooldownActive{};
			bool suppressionLogged{};
			RE::BSIMusicType* lastAllowedOtherType{};
		};

		std::mutex g_stateLock;
		std::array<State, ContextIndex(MusicContext::kCount)> g_states;
		std::atomic_bool g_runtimeEnabled{ false };
		std::mt19937 g_random{ static_cast<std::mt19937::result_type>(Clock::now().time_since_epoch().count()) };

		void ClearLocked(MusicContext a_context) noexcept { g_states[ContextIndex(a_context)] = {}; }
		void ClearAllLocked() noexcept { g_states = {}; }

		void LogType(std::string_view a_message, MusicContext a_context, RE::BSIMusicType* a_type)
		{
			if (const auto musicType = a_type ? skyrim_cast<RE::BGSMusicType*>(a_type) : nullptr) {
				logs::info("[interval] {} context={} type={} form={:08X} editorID='{}'", a_message,
					ContextName(a_context), static_cast<const void*>(a_type), musicType->GetFormID(), musicType->GetFormEditorID());
			} else {
				logs::info("[interval] {} context={} type={}", a_message, ContextName(a_context), static_cast<const void*>(a_type));
			}
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
			LogType("cooldown expired", a_context, state.managedType);
			LogType("managed type eligible again", a_context, state.managedType);
			ClearLocked(a_context);
		}

		void StartCooldownLocked(MusicContext a_context, RE::BSIMusicType* a_type,
			RE::BSIMusicTrack* a_track, const Settings::PolicySettings& a_settings, Clock::time_point a_now)
		{
			auto& state = g_states[ContextIndex(a_context)];
			const auto duration = ChooseDuration(a_settings);
			state = {};
			state.managedType = a_type;
			state.cooldownEnd = a_now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>{ duration });
			state.cooldownActive = true;
			LogType("cooldown started", a_context, a_type);
			logs::info("[interval] completion track={} context={}", static_cast<const void*>(a_track), ContextName(a_context));
			logs::info("[interval] chosen duration={:.3f}s context={} configuredRange={:.3f}..{:.3f}s", duration,
				ContextName(a_context), a_settings.minimumSilenceSeconds, a_settings.maximumSilenceSeconds);
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
				ClearLocked(a_context);
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

	bool ShouldSuppressPlay(RE::BSIMusicType* a_type, const Settings::Values& a_settings) noexcept
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
				const auto& policy = a_settings.For(context);
				if (policy.policy != Settings::Policy::kInterval) {
					ClearLocked(context);
					continue;
				}
				ExpireLocked(context, now);
				if (!state.cooldownActive && state.pendingType == a_type && a_type &&
					a_type->typeStatus == TypeStatus::kInactive) {
					StartCooldownLocked(context, a_type, state.pendingTrack, state.pendingSettings, now);
				}
				if (!state.cooldownActive) continue;
				if (a_type == state.managedType) {
					if (!state.suppressionLogged) {
						LogType("managed type replay suppressed", context, a_type);
						state.suppressionLogged = true;
					}
					return true;
				}
				if (a_type && a_type != state.lastAllowedOtherType) {
					LogType("other music type allowed during cooldown", context, a_type);
					state.lastAllowedOtherType = a_type;
				}
			}
			return false;
		} catch (...) {
			logs::error("Interval policy exception while evaluating playback; allowing native behavior");
			return false;
		}
	}
}
