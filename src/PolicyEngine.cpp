#include "PolicyEngine.h"

#include "IntervalPolicy.h"
#include "MusicClassification.h"
#include "PmfPlaceholder.h"
#include "Settings.h"

#include <unordered_set>

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
		std::mutex g_policySyncLock;
		std::unordered_map<RE::BSIMusicType*, ActivationState> g_activations;
		std::array<std::unordered_set<RE::BSIMusicType*>, ContextIndex(MusicContext::kCount)> g_disabledSuppressions;
		std::array<std::unordered_set<RE::BGSMusicPaletteTrack*>, ContextIndex(MusicContext::kCount)> g_disabledPalettes;
		std::array<Settings::Policy, ContextIndex(MusicContext::kCount)> g_observedPolicies{};
		bool g_policiesInitialized{};
		std::array<std::uint64_t, ContextIndex(MusicContext::kCount)> g_observedRevisions{};
		std::array<std::optional<Settings::Policy>, ContextIndex(MusicContext::kCount)> g_effectivePolicies;
		struct BlockedStart { RE::BSIMusicTrack* track; MusicContext context; };
		std::unordered_map<RE::BSIMusicType*, BlockedStart> g_blockedStarts;
		struct ResolutionLog
		{
			MusicSubcontext subcontext;
			Settings::Policy policy;
			bool fromOverride;
			RE::BSIMusicTrack* entry;
		};
		std::unordered_map<RE::BSIMusicType*, ResolutionLog> g_resolutionLogs;
		std::atomic_bool g_runtimeEnabled{ false };

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

		[[nodiscard]] std::string TrackName(RE::BSIMusicType* a_owner, RE::BSIMusicTrack* a_track)
		{
			if (a_owner && a_owner->currentTrackIndex < a_owner->tracks.size()) {
				if (const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_owner->tracks[a_owner->currentTrackIndex]);
					wrapper && wrapper->track == a_track) {
					return std::format("{:08X}", wrapper->GetFormID());
				}
			}
			if (!a_track) return "<null>";
			switch (a_track->GetType()) {
			case RE::BSIMusicTrack::TrackType::kPalette:
				return "palette";
			case RE::BSIMusicTrack::TrackType::kSilentTrack:
				return "silence";
			case RE::BSIMusicTrack::TrackType::kSingleTrack:
				return "single";
			default:
				return "unknown";
			}
		}

		void LogActivation(std::string_view a_action, MusicContext a_context, RE::BSIMusicType* a_type)
		{
			const auto settings = Settings::GetSnapshot();
			const auto policy = settings.For(a_context).policy;
			const auto tag = policy == Settings::Policy::kOncePerActivation ? "once" : "activation";
			logs::info("[{}] activation {} context={} type={} policy={}", tag, a_action,
				ContextName(a_context), TypeName(a_type), Settings::PolicyName(policy));
		}

		void SeedActivationLocked(RE::BSIMusicType* a_type)
		{
			if (!a_type || g_activations.contains(a_type)) return;
			const auto classification = MusicClassification::Classify(a_type);
			if (classification.context == MusicContext::kUnmanaged) return;
			g_activations.emplace(a_type, ActivationState{ classification.context, true, false, false });
			LogActivation("begin (lifecycle manager snapshot)", classification.context, a_type);
		}

		void StorePoliciesLocked(const Settings::Values& a_settings) noexcept
		{
			for (const auto context : kManagedMusicContexts) {
				g_observedPolicies[ContextIndex(context)] = a_settings.For(context).policy;
				g_observedRevisions[ContextIndex(context)] = a_settings.policyRevisions[ContextIndex(context)];
			}
			g_policiesInitialized = true;
		}

		void SynchronizePolicies(const Settings::Values& a_settings)
		{
			std::scoped_lock policyLock{ g_policySyncLock };
			std::array<bool, ContextIndex(MusicContext::kCount)> changed{};
			std::array<Settings::Policy, ContextIndex(MusicContext::kCount)> previous{};
			{
				std::scoped_lock stateLock{ g_stateLock };
				if (!g_policiesInitialized) {
					StorePoliciesLocked(a_settings);
					return;
				}

				for (const auto context : kManagedMusicContexts) {
					const auto index = ContextIndex(context);
					const auto next = a_settings.For(context).policy;
					if (g_observedPolicies[index] == next && g_observedRevisions[index] == a_settings.policyRevisions[index]) continue;

					previous[index] = g_observedPolicies[index];
					g_observedPolicies[index] = next;
					g_observedRevisions[index] = a_settings.policyRevisions[index];
					g_effectivePolicies[index].reset();
					std::erase_if(g_blockedStarts, [context](const auto& entry) { return entry.second.context == context; });
					g_resolutionLogs.clear();
					changed[index] = true;
					g_disabledSuppressions[index].clear();
					g_disabledPalettes[index].clear();
					for (auto& [type, activation] : g_activations) {
						(void) type;
						if (activation.context == context) {
							activation.trackCompleted = false;
							activation.suppressionLogged = false;
						}
					}
				}
			}

			for (const auto context : kManagedMusicContexts) {
				const auto index = ContextIndex(context);
				if (!changed[index]) continue;
				IntervalPolicy::ResetContext(context, "policy changed");
				if (context == MusicContext::kExploration) {
					logs::info("[policy] configuration changed context={} from={} to={} Day={} Night={}; context state reset",
						ContextName(context), Settings::PolicyName(previous[index]), Settings::PolicyName(a_settings.For(context).policy),
						Settings::OverrideName(a_settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationDay)]),
						Settings::OverrideName(a_settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationNight)]));
				} else {
					logs::info("[policy] changed context={} from={} to={}; context state reset",
						ContextName(context), Settings::PolicyName(previous[index]),
						Settings::PolicyName(a_settings.For(context).policy));
				}
			}
		}

		Settings::PolicySettings ResolveForType(const Settings::Values& a_settings,
			RE::BSIMusicType* a_type, MusicContext a_context, RE::BSIMusicTrack* a_entry = nullptr)
		{
			const bool inspectSubcontext = a_settings.currentMusicDiagnostics ||
				a_settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationDay)] != Settings::PolicyOverride::kInherit ||
				a_settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationNight)] != Settings::PolicyOverride::kInherit;
			if (!a_entry && a_type && a_type->currentTrackIndex < a_type->tracks.size()) {
				a_entry = a_type->tracks[a_type->currentTrackIndex];
			}
			const auto subcontext = inspectSubcontext ? MusicClassification::ClassifySelectedSubcontext(a_entry, a_context) : MusicSubcontext::kNone;
			const auto resolved = a_settings.Resolve(a_context, subcontext);
			std::scoped_lock policyLock{ g_policySyncLock };
			bool changed{};
			{
				std::scoped_lock stateLock{ g_stateLock };
				const auto index = ContextIndex(a_context);
				changed = g_effectivePolicies[index] && *g_effectivePolicies[index] != resolved.settings.policy;
				g_effectivePolicies[index] = resolved.settings.policy;
				if (changed) {
					std::erase_if(g_blockedStarts, [a_context](const auto& entry) { return entry.second.context == a_context; });
					g_disabledSuppressions[index].clear();
					g_disabledPalettes[index].clear();
					for (auto& [type, activation] : g_activations) {
						(void) type;
						if (activation.context == a_context) {
							activation.trackCompleted = false;
							activation.suppressionLogged = false;
						}
					}
				}
				if (Settings::CurrentMusicDiagnosticsEnabled()) {
					const auto found = g_resolutionLogs.find(a_type);
					if (found == g_resolutionLogs.end() || found->second.subcontext != subcontext ||
						found->second.policy != resolved.settings.policy || found->second.fromOverride != resolved.fromOverride || found->second.entry != a_entry) {
						const auto wrapper = a_entry ? skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_entry) : nullptr;
						logs::info("[policy] resolved type={} context={} subcontext={} policy={} source={} evidence={} wrapper={:08X}",
							TypeName(a_type), ContextName(a_context), SubcontextName(subcontext),
							Settings::PolicyName(resolved.settings.policy), resolved.fromOverride ? "override" : "parent",
							subcontext == MusicSubcontext::kNone ? "selected-track-unknown-or-mixed" :
								(wrapper && skyrim_cast<RE::BGSMusicSingleTrack*>(wrapper->track) ? "pmf-selected-wrapper-resource" : "pmf-selected-palette-resources"),
							wrapper ? wrapper->GetFormID() : 0);
						g_resolutionLogs[a_type] = { subcontext, resolved.settings.policy, resolved.fromOverride, a_entry };
					}
				}
			}
			if (changed) {
				IntervalPolicy::ResetContext(a_context, "effective policy changed");
				logs::info("[policy] effective policy changed context={} subcontext={} policy={}; context state reset",
					ContextName(a_context), SubcontextName(subcontext), Settings::PolicyName(resolved.settings.policy));
			}
			return resolved.settings;
		}
	}

	void Reset(std::string_view a_reason, bool a_enable) noexcept
	{
		try {
			g_runtimeEnabled.store(false, std::memory_order_relaxed);
			{
				std::scoped_lock policyLock{ g_policySyncLock };
				std::scoped_lock lock{ g_stateLock };
				g_activations.clear();
				g_disabledSuppressions = {};
				g_disabledPalettes = {};
				g_effectivePolicies = {};
				g_resolutionLogs.clear();
				g_blockedStarts.clear();
				StorePoliciesLocked(Settings::GetSnapshot());
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

	void Update() noexcept
	{
		try {
			if (g_runtimeEnabled.load(std::memory_order_relaxed)) {
				SynchronizePolicies(Settings::GetSnapshot());
			}
			IntervalPolicy::Update();
		} catch (...) { logs::error("Policy engine exception while synchronizing settings"); }
	}

	void OnMusicEvent(const RE::BSMusicEvent* a_event) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_event || !a_event->musicType) return;
			SynchronizePolicies(Settings::GetSnapshot());
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
				g_blockedStarts.erase(type);
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
			SynchronizePolicies(settings);
			{
				std::scoped_lock lock{ g_stateLock };
				if (const auto blocked = g_blockedStarts.find(a_owner);
					blocked != g_blockedStarts.end() && blocked->second.track == a_track) return;
			}
			const auto policy = ResolveForType(settings, a_owner, classification.context);
			if (!settings.enabled) return;

			const bool completionAffectsPolicy = policy.policy == Settings::Policy::kInterval ||
			                                     (classification.context == MusicContext::kDungeon &&
			                                         policy.policy == Settings::Policy::kOncePerActivation);
			if (completionAffectsPolicy) {
				const auto pmf = PmfPlaceholder::ClassifySelectedSingle(a_owner, a_track);
				if (pmf.placeholder) {
					const auto policyTag = policy.policy == Settings::Policy::kInterval ? "interval" : "once";
					logs::info("[{}] completion ignored reason=pmf-placeholder context={} type={} track={}",
						policyTag, ContextName(classification.context), TypeName(a_owner), TrackName(a_owner, a_track));
					if (Settings::CurrentMusicDiagnosticsEnabled()) {
						logs::info("[pmf-placeholder-detail] owner={} track={} category='{}' targets='{}' localForm={:06X} asset='{}'",
							static_cast<const void*>(a_owner), static_cast<const void*>(a_track), pmf.category,
							pmf.targetMusicTypes, pmf.localFormID, pmf.assetPath);
					}
					return;
				}
			}

			IntervalPolicy::OnNaturalCompletionCandidate(classification.context, a_owner, a_track, policy);
			if (classification.context != MusicContext::kDungeon || policy.policy != Settings::Policy::kOncePerActivation) return;

			std::scoped_lock lock{ g_stateLock };
			if (const auto existing = g_activations.find(a_owner);
				existing != g_activations.end() && existing->second.active && !existing->second.trackCompleted) {
				existing->second.trackCompleted = true;
				logs::info("[once] consumed context={} type={} track={}", ContextName(classification.context),
					TypeName(a_owner), TrackName(a_owner, a_track));
			} else {
				logs::debug("[once] completion had no active manager activation; suppression not armed type={}", TypeName(a_owner));
			}
		} catch (...) { logs::error("Policy engine exception while recording natural completion"); }
	}

	void OnNativeSilenceCompletion(RE::BSIMusicType* a_owner, RE::BSIMusicTrack* a_track) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_owner || !a_track) return;
			const auto classification = MusicClassification::Classify(a_owner);
			const auto settings = Settings::GetSnapshot();
			SynchronizePolicies(settings);
			if (settings.enabled && classification.context == MusicContext::kDungeon &&
				settings.For(classification.context).policy == Settings::Policy::kOncePerActivation) {
				logs::info("[once] completion ignored reason=native-silence context={} type={} track={}",
					ContextName(classification.context), TypeName(a_owner), TrackName(a_owner, a_track));
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
			SynchronizePolicies(settings);
			if (!settings.enabled) return;
			IntervalPolicy::OnTypeTransition(classification.context, a_type, a_before, a_after,
				ResolveForType(settings, a_type, classification.context));
		} catch (...) { logs::error("Policy engine exception while observing type transition"); }
	}

	bool ShouldSuppressPlay(RE::BSIMusicType* a_type, RE::BSIMusicTrack* a_selectedEntry) noexcept
	{
		try {
			const auto settings = Settings::GetSnapshot();
			const auto runtimeEnabled = g_runtimeEnabled.load(std::memory_order_relaxed);
			if (runtimeEnabled) SynchronizePolicies(settings);
			const auto classification = runtimeEnabled && settings.enabled && a_type ?
			                                MusicClassification::Classify(a_type) :
			                                MusicClassification::Result{};
			if (!runtimeEnabled || !a_type) return false;
			if (!settings.enabled) {
				(void) IntervalPolicy::ShouldSuppressPlay(a_type, classification.context, settings);
				return false;
			}

			// With overrides, the pre-selection type entry cannot decide from a
			// previous index or the entire candidate pool. Gate the native selected
			// top-level entry before its DoPlay instead.
			if (!a_selectedEntry && classification.context == MusicContext::kExploration &&
				(settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationDay)] != Settings::PolicyOverride::kInherit ||
				 settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationNight)] != Settings::PolicyOverride::kInherit)) return false;
			const auto policy = classification.context == MusicContext::kUnmanaged ? Settings::PolicySettings{} :
				ResolveForType(settings, a_type, classification.context, a_selectedEntry);
			if (classification.context != MusicContext::kUnmanaged && policy.policy == Settings::Policy::kDisabled) {
				std::scoped_lock lock{ g_stateLock };
				if (g_disabledSuppressions[ContextIndex(classification.context)].insert(a_type).second) {
					logs::info("[disabled] suppressed context={} type={}",
						ContextName(classification.context), TypeName(a_type));
				}
				return true;
			}

			if (IntervalPolicy::ShouldSuppressPlay(a_type, classification.context, settings, policy.policy)) return true;

			if (classification.context != MusicContext::kDungeon ||
				policy.policy != Settings::Policy::kOncePerActivation) return false;

			std::scoped_lock lock{ g_stateLock };
			if (const auto existing = g_activations.find(a_type);
				existing != g_activations.end() && existing->second.active && existing->second.trackCompleted) {
				if (!existing->second.suppressionLogged) {
					logs::info("[once] replay suppressed context={} type={}",
						ContextName(classification.context), TypeName(a_type));
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

	bool ShouldSuppressSelectedPlay(RE::BSIMusicType* a_type, RE::BSIMusicTrack* a_entry) noexcept
	{
		try {
			if (!a_type || !a_entry || !g_runtimeEnabled.load(std::memory_order_relaxed)) return false;
			const auto settings = Settings::GetSnapshot();
			if (!settings.enabled || MusicClassification::Classify(a_type).context != MusicContext::kExploration ||
				(settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationDay)] == Settings::PolicyOverride::kInherit &&
				 settings.overrides[static_cast<std::size_t>(MusicSubcontext::kExplorationNight)] == Settings::PolicyOverride::kInherit)) {
				std::scoped_lock lock{ g_stateLock };
				g_blockedStarts.erase(a_type);
				return false;
			}
			const bool blocked = ShouldSuppressPlay(a_type, a_entry);
			auto* concrete = a_entry;
			if (const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(a_entry)) concrete = wrapper->track;
			std::scoped_lock lock{ g_stateLock };
			if (blocked) g_blockedStarts[a_type] = { concrete, MusicContext::kExploration };
			else g_blockedStarts.erase(a_type);
			return blocked;
		} catch (...) {
			logs::error("Policy engine exception at selected playback gate; allowing native behavior");
			return false;
		}
	}

	bool ShouldBlockPaletteUpdate(RE::BSIMusicType* a_owner, RE::BGSMusicPaletteTrack* a_palette) noexcept
	{
		try {
			if (!g_runtimeEnabled.load(std::memory_order_relaxed) || !a_owner || !a_palette ||
			    a_palette->trackStatus != RE::BSIMusicTrack::MUSIC_STATUS::kInactive ||
			    a_owner->currentTrackIndex >= a_owner->tracks.size()) {
				return false;
			}

			auto* selected = a_owner->tracks[a_owner->currentTrackIndex];
			if (const auto wrapper = skyrim_cast<RE::BGSMusicTrackFormWrapper*>(selected)) {
				selected = wrapper->track;
			}
			if (selected != a_palette) {
				return false;
			}

			const auto settings = Settings::GetSnapshot();
			if (!settings.enabled) {
				return false;
			}
			SynchronizePolicies(settings);
			if (a_owner->typeStatus != RE::BSIMusicType::MUSIC_STATUS::kInactive) {
				// Native DoPlay may mark the owner playing even when its selected
				// entry was declined. Only that exact never-started palette qualifies.
				std::scoped_lock lock{ g_stateLock };
				const auto blocked = g_blockedStarts.find(a_owner);
				if (blocked == g_blockedStarts.end() || blocked->second.track != a_palette ||
					a_owner->typeStatus != RE::BSIMusicType::MUSIC_STATUS::kPlaying) return false;
			}
			const auto classification = MusicClassification::Classify(a_owner);
			if (classification.context == MusicContext::kUnmanaged) {
				return false;
			}

			const auto policy = ResolveForType(settings, a_owner, classification.context).policy;
			if (policy == Settings::Policy::kInterval) {
				if (IntervalPolicy::BlockInactivePaletteUpdate(classification.context, a_owner, a_palette)) return true;
			}
			if (policy == Settings::Policy::kDisabled) {
				std::scoped_lock lock{ g_stateLock };
				if (g_disabledPalettes[ContextIndex(classification.context)].insert(a_palette).second) {
					logs::info("[disabled] blocked inactive palette update context={} type={}",
						ContextName(classification.context), TypeName(a_owner));
				}
				return true;
			}
			{
				// An allowed palette update can legitimately start its children; it
				// must no longer carry a declined-start completion marker.
				std::scoped_lock lock{ g_stateLock };
				g_blockedStarts.erase(a_owner);
			}
			return false;
		} catch (...) {
			logs::error("Policy engine exception while evaluating inactive palette update; allowing native behavior");
			return false;
		}
	}
}
