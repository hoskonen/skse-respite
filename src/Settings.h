#pragma once

#include "MusicContext.h"

namespace Respite::Settings
{
	enum class Policy : std::uint8_t
	{
		kNormal,
		kInterval,
		kOncePerActivation,
		kDisabled
	};

	struct PolicySettings
	{
		Policy policy{ Policy::kNormal };
		float minimumSilenceSeconds{ 15.0F };
		float maximumSilenceSeconds{ 15.0F };
	};

	enum class PolicyOverride : std::uint8_t
	{
		kInherit,
		kNormal,
		kInterval,
		kOncePerActivation,
		kDisabled
	};

	struct ResolvedPolicy
	{
		PolicySettings settings;
		bool fromOverride{};
	};

	inline constexpr std::array kExplorationPolicies{ Policy::kNormal, Policy::kInterval, Policy::kDisabled };
	inline constexpr std::array kExplorationOverrides{ PolicyOverride::kInherit, PolicyOverride::kNormal, PolicyOverride::kInterval, PolicyOverride::kDisabled };

	[[nodiscard]] constexpr Policy NormalizePolicy(MusicContext a_context, Policy a_policy) noexcept
	{
		return a_policy > Policy::kDisabled ||
			(a_context == MusicContext::kExploration && a_policy == Policy::kOncePerActivation) ? Policy::kNormal : a_policy;
	}

	[[nodiscard]] constexpr PolicyOverride NormalizeExplorationOverride(PolicyOverride a_override) noexcept
	{
		return a_override > PolicyOverride::kDisabled || a_override == PolicyOverride::kOncePerActivation ? PolicyOverride::kInherit : a_override;
	}

	[[nodiscard]] constexpr std::string_view PolicyName(Policy a_policy) noexcept
	{
		switch (a_policy) {
		case Policy::kInterval: return "Interval";
		case Policy::kOncePerActivation: return "OncePerActivation";
		case Policy::kDisabled: return "Disabled";
		default: return "Normal";
		}
	}

	[[nodiscard]] constexpr Policy ParseContextPolicy(MusicContext a_context, std::string_view a_value, bool& a_recognized) noexcept
	{
		for (const auto policy : { Policy::kNormal, Policy::kInterval, Policy::kOncePerActivation, Policy::kDisabled }) {
			if (a_value == PolicyName(policy) && NormalizePolicy(a_context, policy) == policy) {
				a_recognized = true;
				return policy;
			}
		}
		a_recognized = false;
		return Policy::kNormal;
	}

	[[nodiscard]] constexpr PolicyOverride ParseExplorationOverride(std::string_view a_value, bool& a_recognized) noexcept
	{
		if (a_value == "Inherit") {
			a_recognized = true;
			return PolicyOverride::kInherit;
		}
		const auto policy = ParseContextPolicy(MusicContext::kExploration, a_value, a_recognized);
		return a_recognized ? static_cast<PolicyOverride>(static_cast<unsigned>(policy) + 1) : PolicyOverride::kInherit;
	}

	struct Values
	{
		bool enabled{ true };
		std::array<PolicySettings, ContextIndex(MusicContext::kCount)> contexts{
			PolicySettings{ Policy::kInterval, 180.0F, 420.0F },
			PolicySettings{ Policy::kNormal, 15.0F, 15.0F },
			PolicySettings{ Policy::kNormal, 15.0F, 15.0F },
			PolicySettings{ Policy::kOncePerActivation, 15.0F, 15.0F }
		};

		bool debugLogging{ false };
		bool currentMusicDiagnostics{ false };
		std::array<PolicyOverride, static_cast<std::size_t>(MusicSubcontext::kCount)> overrides{};
		// Runtime uses revisions to notice even a switch away and back between updates.
		std::array<std::uint64_t, ContextIndex(MusicContext::kCount)> policyRevisions{};

		[[nodiscard]] PolicySettings& For(MusicContext a_context) noexcept
		{
			return contexts[ContextIndex(a_context)];
		}

		[[nodiscard]] const PolicySettings& For(MusicContext a_context) const noexcept
		{
			return contexts[ContextIndex(a_context)];
		}

		[[nodiscard]] constexpr ResolvedPolicy Resolve(MusicContext a_context, MusicSubcontext a_subcontext) const noexcept
		{
			ResolvedPolicy result{ contexts[ContextIndex(a_context)], false };
			result.settings.policy = NormalizePolicy(a_context, result.settings.policy);
			if (a_context == MusicContext::kExploration &&
				(a_subcontext == MusicSubcontext::kExplorationDay || a_subcontext == MusicSubcontext::kExplorationNight)) {
				const auto override = NormalizeExplorationOverride(overrides[static_cast<std::size_t>(a_subcontext)]);
				if (override != PolicyOverride::kInherit) {
					result.settings.policy = static_cast<Policy>(static_cast<unsigned>(override) - 1);
					result.fromOverride = true;
				}
			}
			return result;
		}

		[[nodiscard]] constexpr bool UsesInterval(MusicContext a_context) const noexcept
		{
			return Resolve(a_context, MusicSubcontext::kNone).settings.policy == Policy::kInterval ||
				(a_context == MusicContext::kExploration &&
					(Resolve(a_context, MusicSubcontext::kExplorationDay).settings.policy == Policy::kInterval ||
					 Resolve(a_context, MusicSubcontext::kExplorationNight).settings.policy == Policy::kInterval));
		}
	};

	inline constexpr float kMinimumSilenceSeconds = 0.0F;
	inline constexpr float kMaximumSilenceSeconds = 3600.0F;

	[[nodiscard]] Values GetSnapshot() noexcept;
	void Update(Values a_values) noexcept;

	void Load();
	[[nodiscard]] bool Save();

	[[nodiscard]] bool CurrentMusicDiagnosticsEnabled() noexcept;
	[[nodiscard]] std::string_view OverrideName(PolicyOverride a_override) noexcept;
}
