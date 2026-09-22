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
		bool currentMusicDiagnostics{ true };

		[[nodiscard]] PolicySettings& For(MusicContext a_context) noexcept
		{
			return contexts[ContextIndex(a_context)];
		}

		[[nodiscard]] const PolicySettings& For(MusicContext a_context) const noexcept
		{
			return contexts[ContextIndex(a_context)];
		}
	};

	inline constexpr float kMinimumSilenceSeconds = 0.0F;
	inline constexpr float kMaximumSilenceSeconds = 3600.0F;

	[[nodiscard]] Values GetSnapshot() noexcept;
	void Update(Values a_values) noexcept;

	void Load();
	[[nodiscard]] bool Save();

	[[nodiscard]] bool CurrentMusicDiagnosticsEnabled() noexcept;
	[[nodiscard]] std::string_view PolicyName(Policy a_policy) noexcept;
}
