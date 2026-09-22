#pragma once

namespace Respite::PmfPlaceholder
{
	struct Result
	{
		bool recognizedSlot{};
		bool placeholder{};
		RE::FormID localFormID{};
		std::string assetPath;
	};

	// Unknown, unreadable, mismatched, or modified content is deliberately reported
	// as non-placeholder so policy accounting treats it as audible.
	[[nodiscard]] Result ClassifySelectedSingle(
		RE::BSIMusicType* a_owner,
		RE::BSIMusicTrack* a_completedTrack) noexcept;
}
