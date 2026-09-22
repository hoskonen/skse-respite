#pragma once

namespace Respite
{
	enum class MusicContext : std::uint8_t
	{
		kExploration,
		kTown,
		kTavern,
		kDungeon,
		kCount,
		kUnmanaged = 0xFF
	};

	inline constexpr std::array kManagedMusicContexts{
		MusicContext::kExploration,
		MusicContext::kTown,
		MusicContext::kTavern,
		MusicContext::kDungeon
	};

	[[nodiscard]] constexpr std::size_t ContextIndex(MusicContext a_context) noexcept
	{
		return static_cast<std::size_t>(a_context);
	}

	[[nodiscard]] constexpr std::string_view ContextName(MusicContext a_context) noexcept
	{
		switch (a_context) {
		case MusicContext::kExploration:
			return "Exploration";
		case MusicContext::kTown:
			return "Town";
		case MusicContext::kTavern:
			return "Tavern";
		case MusicContext::kDungeon:
			return "Dungeon";
		case MusicContext::kUnmanaged:
		default:
			return "Unmanaged";
		}
	}
}
