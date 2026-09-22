#pragma once

#include "MusicContext.h"

namespace Respite::MusicClassification
{
	enum class Evidence : std::uint8_t
	{
		kUnknown,
		kKnownForm,
		kNativeEditorID,
		kDistributedEditorID,
		kCurrentRegion,
		kPlaysOnce,
		kExemptEditorID
	};

	struct Result
	{
		MusicContext context{ MusicContext::kUnmanaged };
		Evidence evidence{ Evidence::kUnknown };
	};

	[[nodiscard]] Result Classify(RE::BSIMusicType* a_type) noexcept;
	void Reset() noexcept;
}
