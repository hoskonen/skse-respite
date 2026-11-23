#include <array>
#include <cstdint>
#include <string_view>

#include "Settings.h"

using namespace Respite;

constexpr bool ResolutionMatrix()
{
	for (unsigned parent = 0; parent <= static_cast<unsigned>(Settings::Policy::kDisabled); ++parent) {
		for (unsigned override = 0; override <= static_cast<unsigned>(Settings::PolicyOverride::kDisabled); ++override) {
		for (const auto period : { MusicSubcontext::kExplorationDay, MusicSubcontext::kExplorationNight }) {
			Settings::Values values;
			values.contexts[ContextIndex(MusicContext::kExploration)] = {
				static_cast<Settings::Policy>(parent), 12.0F, 34.0F
			};
			values.overrides[static_cast<std::size_t>(period)] = static_cast<Settings::PolicyOverride>(override);
			const auto otherPeriod = period == MusicSubcontext::kExplorationDay ? MusicSubcontext::kExplorationNight : MusicSubcontext::kExplorationDay;
			const auto day = values.Resolve(MusicContext::kExploration, otherPeriod);
			const auto night = values.Resolve(MusicContext::kExploration, period);
			const auto unknown = values.Resolve(MusicContext::kExploration, MusicSubcontext::kNone);
			const auto expectedParent = parent == static_cast<unsigned>(Settings::Policy::kOncePerActivation) ? 0U : parent;
			const bool inherited = override == 0 || override == static_cast<unsigned>(Settings::PolicyOverride::kOncePerActivation);
			const auto expected = inherited ? expectedParent : override - 1;
			if (static_cast<unsigned>(night.settings.policy) != expected || night.fromOverride != !inherited) return false;
			if (night.settings.policy == Settings::Policy::kOncePerActivation) return false;
			if (day.settings.policy != static_cast<Settings::Policy>(expectedParent) || day.fromOverride) return false;
			if (unknown.settings.policy != day.settings.policy || unknown.fromOverride) return false;
			if (night.settings.minimumSilenceSeconds != 12.0F || night.settings.maximumSilenceSeconds != 34.0F) return false;
			if (values.UsesInterval(MusicContext::kExploration) !=
				(expectedParent == static_cast<unsigned>(Settings::Policy::kInterval) || expected == static_cast<unsigned>(Settings::Policy::kInterval))) return false;
			for (const auto context : { MusicContext::kTown, MusicContext::kTavern, MusicContext::kDungeon }) {
				const auto resolved = values.Resolve(context, MusicSubcontext::kExplorationNight);
				if (resolved.settings.policy != values.contexts[ContextIndex(context)].policy || resolved.fromOverride) return false;
			}
		}
		}
	}
	return true;
}

static_assert(ResolutionMatrix(), "Override precedence, inherited timing, and context isolation must hold");
static_assert(Settings::Values{}.Resolve(MusicContext::kDungeon, MusicSubcontext::kNone).settings.policy == Settings::Policy::kOncePerActivation);
