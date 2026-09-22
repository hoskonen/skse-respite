#pragma once

#include "MusicContext.h"
#include "Settings.h"

namespace Respite::IntervalPolicy
{
	void Reset(std::string_view a_reason, bool a_enable) noexcept;
	void Update() noexcept;
	void OnNaturalCompletionCandidate(MusicContext a_context, RE::BSIMusicType* a_owner,
		RE::BSIMusicTrack* a_track, const Settings::PolicySettings& a_settings) noexcept;
	void OnTypeTransition(MusicContext a_context, RE::BSIMusicType* a_type,
		std::uint32_t a_before, std::uint32_t a_after, const Settings::PolicySettings& a_settings) noexcept;
	[[nodiscard]] bool ShouldSuppressPlay(RE::BSIMusicType* a_type, const Settings::Values& a_settings) noexcept;
}
