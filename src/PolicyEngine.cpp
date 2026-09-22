#include "PolicyEngine.h"

#include "IntervalPolicy.h"
#include "MusicClassification.h"
#include "PmfPlaceholder.h"
#include "Settings.h"

namespace Respite::PolicyEngine
{
	namespace
	{
		struct ActivationState
		{
			MusicContext context{ MusicContext::kUnmanaged };
			bool active{};
			bool trackCompleted{};
			bool suppressionLogged{};
		};

		std::mutex g_stateLock;
		std::unordered_map<RE::BSIMusicType*, ActivationState> g_activations;
		std::atomic_bool g_runtimeEnabled{ false };

		void LogActivation(std::string_view a_action, MusicContext a_context, RE::BSIMusicType* a_type)
		{
			if (const auto musicType = a_type ? skyrim_cast<RE::BGSMusicType*>(a_type) : nullptr) {
				logs::info("[activation] {} context={} type={} form={:08X} editorID='{}'", a_action,
					ContextName(a_context), static_cast<const void*>(a_type), musicType->GetFormID(), musicType->GetFormEditorID());
			}
		}

		void SeedActivationLocked(RE::BSIMusicType* a_type)
		{
			if (!a_type || g_activations.contains(a_type)) return;
			const auto classification = MusicClassification::Classify(a_type);
			if (classification.context == MusicContext::kUnmanaged) return;
			g_activations.emplace(a_type, ActivationState{ classification.context, true, false, false });
			LogActivation("begin (lifecycle manager snapshot)", classification.context, a_type);
		}
	}

	void Reset(std::string_view a_reason, bool a_enable) noexcept
	{
		try {
			g_runtimeEnabled.store(false, std::memory_order_relaxed);
			{
				std::scoped_lock lock{ g_stateLock };
				g_activations.clear();
			}
			MusicClassification::Reset();
			IntervalPolicy::Reset(a_reason, a_enable);
			if (a_enable) {
				if (const auto manager = RE::BSMusicManager::GetSingleton()) {
					std::scoped_lock lock{ g_stateLock };
					for (auto* type : manager->musicQueue) SeedActivationLocked(type);
					SeedActivationLocked(manager->current);
				}
			}
			g_runtimeEnabled.store(a_enable, std::memory_order_relaxed);
			logs::debug("[policy] state reset reason={} enabled={}", a_reason, a_enable);
		} catch (...) { logs::error("Policy engine exception while resetting state"); }
	}

	void Update() noexcept { IntervalPolicy::Update(); }

	void OnMusicEvent(const RE::BSMusicEvent* a_event) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_event || !a_event->musicType) return;
			const auto type = a_event->musicType;
			const auto classification = MusicClassification::Classify(type);
			if (classification.context == MusicContext::kUnmanaged) return;

			std::scoped_lock lock{ g_stateLock };
			switch (a_event->msgType.get()) {
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kAdd: {
				auto& activation = g_activations[type];
				if (!activation.active) {
					activation = { classification.context, true, false, false };
					LogActivation("begin", classification.context, type);
				}
				break;
			}
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kRemove:
			case RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kRemoveImmediate:
				if (const auto existing = g_activations.find(type); existing != g_activations.end()) {
					LogActivation("end/reset", existing->second.context, type);
					g_activations.erase(existing);
				}
				break;
			default:
				break;
			}
		} catch (...) { logs::error("Policy engine exception while processing music event"); }
	}

	void OnNaturalCompletionCandidate(RE::BSIMusicType* a_owner, RE::BSIMusicTrack* a_track) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_owner) return;
			const auto classification = MusicClassification::Classify(a_owner);
			if (classification.context == MusicContext::kUnmanaged) return;
			const auto settings = Settings::GetSnapshot();
			const auto& policy = settings.For(classification.context);
			if (!settings.enabled) return;

			IntervalPolicy::OnNaturalCompletionCandidate(classification.context, a_owner, a_track, policy);
			if (classification.context != MusicContext::kDungeon || policy.policy != Settings::Policy::kOncePerActivation) return;

			const auto pmf = PmfPlaceholder::ClassifySelectedSingle(a_owner, a_track);
			if (pmf.placeholder) {
				if (Settings::CurrentMusicDiagnosticsEnabled()) {
					logs::info(
						"[once] completion ignored reason=pmf-placeholder type={} track={} localForm={:06X} asset='{}'",
						static_cast<const void*>(a_owner),
						static_cast<const void*>(a_track),
						pmf.localFormID,
						pmf.assetPath);
				}
				return;
			}

			std::scoped_lock lock{ g_stateLock };
			if (const auto existing = g_activations.find(a_owner);
				existing != g_activations.end() && existing->second.active && !existing->second.trackCompleted) {
				existing->second.trackCompleted = true;
				if (Settings::CurrentMusicDiagnosticsEnabled()) {
					if (a_track && a_track->GetType() == RE::BSIMusicTrack::TrackType::kPalette) {
						logs::info("[once] activation consumed by top-level palette context={} type={} track={}",
							ContextName(classification.context), static_cast<const void*>(a_owner), static_cast<const void*>(a_track));
					} else {
						logs::info("[once] activation consumed by audible track context={} type={} track={}",
							ContextName(classification.context), static_cast<const void*>(a_owner), static_cast<const void*>(a_track));
					}
				}
			} else {
				logs::debug("[once] completion had no active manager activation; suppression not armed type={}", static_cast<const void*>(a_owner));
			}
		} catch (...) { logs::error("Policy engine exception while recording natural completion"); }
	}

	void OnNativeSilenceCompletion(RE::BSIMusicType* a_owner, RE::BSIMusicTrack* a_track) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_owner || !a_track ||
				!Settings::CurrentMusicDiagnosticsEnabled()) return;
			const auto classification = MusicClassification::Classify(a_owner);
			const auto settings = Settings::GetSnapshot();
			if (settings.enabled && classification.context == MusicContext::kDungeon &&
				settings.For(classification.context).policy == Settings::Policy::kOncePerActivation) {
				logs::info("[once] completion ignored reason=native-silence type={} track={}",
					static_cast<const void*>(a_owner), static_cast<const void*>(a_track));
			}
		} catch (...) { logs::error("Policy engine exception while logging native silence completion"); }
	}

	void OnTypeTransition(RE::BSIMusicType* a_type, std::uint32_t a_before, std::uint32_t a_after) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_type) return;
			const auto classification = MusicClassification::Classify(a_type);
			if (classification.context == MusicContext::kUnmanaged) return;
			const auto settings = Settings::GetSnapshot();
			if (!settings.enabled) return;
			IntervalPolicy::OnTypeTransition(classification.context, a_type, a_before, a_after,
				settings.For(classification.context));
		} catch (...) { logs::error("Policy engine exception while observing type transition"); }
	}

	bool ShouldSuppressPlay(RE::BSIMusicType* a_type) noexcept
	{
		try {
			const auto settings = Settings::GetSnapshot();
			if (IntervalPolicy::ShouldSuppressPlay(a_type, settings)) return true;
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !settings.enabled || !a_type) return false;

			const auto classification = MusicClassification::Classify(a_type);
			if (classification.context != MusicContext::kDungeon ||
				settings.For(classification.context).policy != Settings::Policy::kOncePerActivation) return false;

			std::scoped_lock lock{ g_stateLock };
			if (const auto existing = g_activations.find(a_type);
				existing != g_activations.end() && existing->second.active && existing->second.trackCompleted) {
				if (!existing->second.suppressionLogged) {
					if (Settings::CurrentMusicDiagnosticsEnabled()) {
						logs::info("[once] replay suppressed context={} type={}",
							ContextName(classification.context), static_cast<const void*>(a_type));
					}
					existing->second.suppressionLogged = true;
				}
				return true;
			}
			return false;
		} catch (...) {
			logs::error("Policy engine exception while evaluating playback; allowing native behavior");
			return false;
		}
	}
}
