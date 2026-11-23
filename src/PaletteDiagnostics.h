#pragma once

namespace Respite::PaletteDiagnostics
{
	void Reset() noexcept;

	void BeforeTypePlay(RE::BSIMusicType* a_owner) noexcept;
	void TypePlaySuppressed(RE::BSIMusicType* a_owner) noexcept;
	void AfterTypePlay(RE::BSIMusicType* a_owner) noexcept;

	void BeforeTypeUpdate(RE::BSIMusicType* a_owner) noexcept;
	void AfterTypeUpdate(RE::BSIMusicType* a_owner) noexcept;

	void BeforePalettePlay(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept;
	void AfterPalettePlay(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept;
	void BeforePaletteUpdate(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept;
	void AfterPaletteUpdate(RE::BGSMusicPaletteTrack* a_palette, RE::BSIMusicType* a_owner) noexcept;

	void BeforeChildPlay(RE::BGSMusicSingleTrack* a_child, RE::BGSMusicPaletteTrack* a_palette,
		RE::BSIMusicType* a_owner) noexcept;
	void AfterChildPlay(RE::BGSMusicSingleTrack* a_child, RE::BGSMusicPaletteTrack* a_palette,
		RE::BSIMusicType* a_owner) noexcept;
}
