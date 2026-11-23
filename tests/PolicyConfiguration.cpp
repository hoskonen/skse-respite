#include <array>
#include <cstdint>
#include <string_view>

#include "Settings.h"

using namespace Respite;

constexpr bool ConfigurationCases()
{
	bool recognized{};
	for (const auto value : { "OncePerActivation", "invalid", "" }) {
		if (Settings::ParseContextPolicy(MusicContext::kExploration, value, recognized) != Settings::Policy::kNormal || recognized) return false;
		if (Settings::ParseExplorationOverride(value, recognized) != Settings::PolicyOverride::kInherit || recognized) return false;
	}
	for (const auto policy : Settings::kExplorationPolicies) {
		if (policy == Settings::Policy::kOncePerActivation) return false;
		if (Settings::ParseContextPolicy(MusicContext::kExploration, Settings::PolicyName(policy), recognized) != policy || !recognized) return false;
		const auto override = Settings::ParseExplorationOverride(Settings::PolicyName(policy), recognized);
		if (!recognized || override != static_cast<Settings::PolicyOverride>(static_cast<unsigned>(policy) + 1)) return false;
	}
	if (Settings::ParseExplorationOverride("Inherit", recognized) != Settings::PolicyOverride::kInherit || !recognized) return false;
	for (const auto override : Settings::kExplorationOverrides) {
		if (override == Settings::PolicyOverride::kOncePerActivation) return false;
	}
	for (const auto context : { MusicContext::kTown, MusicContext::kTavern, MusicContext::kDungeon }) {
		if (Settings::ParseContextPolicy(context, "OncePerActivation", recognized) != Settings::Policy::kOncePerActivation || !recognized) return false;
		if (Settings::NormalizePolicy(context, Settings::Policy::kOncePerActivation) != Settings::Policy::kOncePerActivation) return false;
	}
	if (Settings::NormalizePolicy(MusicContext::kExploration, static_cast<Settings::Policy>(255)) != Settings::Policy::kNormal) return false;
	if (Settings::NormalizeExplorationOverride(static_cast<Settings::PolicyOverride>(255)) != Settings::PolicyOverride::kInherit) return false;
	Settings::Values legacy;
	legacy.contexts[ContextIndex(MusicContext::kExploration)].policy = Settings::Policy::kOncePerActivation;
	for (auto& override : legacy.overrides) override = Settings::PolicyOverride::kOncePerActivation;
	for (const auto period : { MusicSubcontext::kNone, MusicSubcontext::kExplorationDay, MusicSubcontext::kExplorationNight }) {
		const auto resolved = legacy.Resolve(MusicContext::kExploration, period);
		if (resolved.settings.policy != Settings::Policy::kNormal || resolved.fromOverride) return false;
		if (legacy.Resolve(MusicContext::kDungeon, period).settings.policy != Settings::Policy::kOncePerActivation) return false;
	}
	return true;
}

static_assert(Settings::kExplorationPolicies.size() == 3);
static_assert(Settings::kExplorationOverrides.size() == 4);
static_assert(ConfigurationCases(), "Legacy Exploration settings must degrade safely without changing other contexts");
