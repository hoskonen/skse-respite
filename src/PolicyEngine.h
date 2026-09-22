#pragma once

namespace Respite::PolicyEngine
{
	void Reset(std::string_view a_reason, bool a_enable) noexcept;
	void Update() noexcept;
	void OnMusicEvent(const RE::BSMusicEvent* a_event) noexcept;
	void OnNaturalCompletionCandidate(RE::BSIMusicType* a_owner, RE::BSIMusicTrack* a_track) noexcept;
	void OnNativeSilenceCompletion(RE::BSIMusicType* a_owner, RE::BSIMusicTrack* a_track) noexcept;
	void OnTypeTransition(RE::BSIMusicType* a_type, std::uint32_t a_before, std::uint32_t a_after) noexcept;
	[[nodiscard]] bool ShouldSuppressPlay(RE::BSIMusicType* a_type) noexcept;
}
