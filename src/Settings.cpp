#include "Settings.h"

#include <SimpleIni.h>

namespace Respite::Settings
{
	namespace
	{
		constexpr auto kSettingsPath = "Data/SKSE/Plugins/Respite.ini";
		constexpr auto kGeneralSection = "General";
		constexpr auto kDiagnosticsSection = "Diagnostics";

		std::mutex g_settingsLock;
		Values g_values;
		std::atomic_bool g_currentMusicDiagnostics{ false };

		void Configure(CSimpleIniA& a_ini)
		{
			a_ini.SetUnicode();
			a_ini.SetQuotes();
		}

		[[nodiscard]] bool Validate(PolicySettings& a_settings) noexcept
		{
			const auto original = a_settings;
			if (a_settings.policy < Policy::kNormal || a_settings.policy > Policy::kDisabled) {
				a_settings.policy = Policy::kNormal;
			}

			a_settings.minimumSilenceSeconds = std::clamp(
				a_settings.minimumSilenceSeconds,
				kMinimumSilenceSeconds,
				kMaximumSilenceSeconds);
			a_settings.maximumSilenceSeconds = std::clamp(
				a_settings.maximumSilenceSeconds,
				kMinimumSilenceSeconds,
				kMaximumSilenceSeconds);
			if (a_settings.maximumSilenceSeconds < a_settings.minimumSilenceSeconds) {
				a_settings.maximumSilenceSeconds = a_settings.minimumSilenceSeconds;
			}

			return original.policy != a_settings.policy ||
			       original.minimumSilenceSeconds != a_settings.minimumSilenceSeconds ||
			       original.maximumSilenceSeconds != a_settings.maximumSilenceSeconds;
		}

		[[nodiscard]] bool Validate(Values& a_values) noexcept
		{
			bool adjusted{};
			for (const auto context : kManagedMusicContexts) {
				auto& settings = a_values.For(context);
				adjusted |= Validate(settings);
				const auto policy = NormalizePolicy(context, settings.policy);
				adjusted |= policy != settings.policy;
				settings.policy = policy;
			}
			for (auto& override : a_values.overrides) {
				const auto normalized = NormalizeExplorationOverride(override);
				adjusted |= normalized != override;
				override = normalized;
			}
			return adjusted;
		}

		void ApplyLoggingLevel(const Values& a_values) noexcept
		{
			const auto level = a_values.debugLogging ? spdlog::level::debug : spdlog::level::info;
			spdlog::set_level(level);
			spdlog::flush_on(level);
		}

		void LoadContext(CSimpleIniA& a_ini, MusicContext a_context, Values& a_values, bool& a_recognizedPolicies)
		{
			auto& settings = a_values.For(a_context);
			const auto section = ContextName(a_context);
			bool recognized = true;
			settings.policy = ParseContextPolicy(a_context,
				a_ini.GetValue(section.data(), "Policy", PolicyName(settings.policy).data()),
				recognized);
			a_recognizedPolicies &= recognized;
			settings.minimumSilenceSeconds = static_cast<float>(a_ini.GetDoubleValue(
				section.data(),
				"MinimumSilenceSeconds",
				settings.minimumSilenceSeconds));
			settings.maximumSilenceSeconds = static_cast<float>(a_ini.GetDoubleValue(
				section.data(),
				"MaximumSilenceSeconds",
				settings.maximumSilenceSeconds));
		}
	}

	Values GetSnapshot() noexcept
	{
		std::scoped_lock lock{ g_settingsLock };
		return g_values;
	}

	void Update(Values a_values) noexcept
	{
		const auto adjusted = Validate(a_values);
		(void) adjusted;
		{
			std::scoped_lock lock{ g_settingsLock };
			for (const auto context : kManagedMusicContexts) {
				const auto index = ContextIndex(context);
				const bool changed = g_values.For(context).policy != a_values.For(context).policy ||
					(context == MusicContext::kExploration && g_values.overrides != a_values.overrides);
				a_values.policyRevisions[index] = g_values.policyRevisions[index] + (changed ? 1 : 0);
			}
			g_values = a_values;
		}
		g_currentMusicDiagnostics.store(a_values.currentMusicDiagnostics, std::memory_order_relaxed);
		ApplyLoggingLevel(a_values);
	}

	void Load()
	{
		Values loaded;
		bool recognizedPolicies = true;

		try {
			if (std::filesystem::exists(kSettingsPath)) {
				CSimpleIniA ini;
				Configure(ini);
				const auto result = ini.LoadFile(kSettingsPath);
				if (result < 0) {
					logs::error("Failed to load settings from {} (error {}); using defaults", kSettingsPath, result);
				} else {
					loaded.enabled = ini.GetBoolValue(kGeneralSection, "Enabled", loaded.enabled);
					for (const auto context : kManagedMusicContexts) {
						LoadContext(ini, context, loaded, recognizedPolicies);
					}
					for (const auto subcontext : { MusicSubcontext::kExplorationDay, MusicSubcontext::kExplorationNight }) {
						const auto value = std::string_view{ ini.GetValue("Exploration.Overrides", SubcontextName(subcontext).data(), "Inherit") };
						auto& override = loaded.overrides[static_cast<std::size_t>(subcontext)];
						bool recognized{};
						override = ParseExplorationOverride(value, recognized);
						if (!recognized) logs::warn("Unsupported or unknown Exploration {} override '{}'; using Inherit", SubcontextName(subcontext), value);
					}

					loaded.debugLogging = ini.GetBoolValue(kDiagnosticsSection, "DebugLogging", loaded.debugLogging);
					loaded.currentMusicDiagnostics = ini.GetBoolValue(
						kDiagnosticsSection,
						"CurrentMusicDiagnostics",
						loaded.currentMusicDiagnostics);
				}
			} else {
				logs::info("Settings file not found; using defaults");
			}
		} catch (const std::exception& e) {
			loaded = {};
			logs::error("Failed to load settings from {}: {}; using defaults", kSettingsPath, e.what());
		}

		const bool adjusted = Validate(loaded);
		Update(loaded);
		if (!recognizedPolicies) {
			logs::warn("Unsupported or unknown policy in {}; affected contexts use Normal", kSettingsPath);
		}
		if (adjusted) {
			logs::warn("Adjusted invalid settings loaded from {}", kSettingsPath);
		}
		logs::info(
			"[settings] enabled={} Exploration={} Town={} Tavern={} Dungeon={} verbose={}",
			loaded.enabled,
			PolicyName(loaded.For(MusicContext::kExploration).policy),
			PolicyName(loaded.For(MusicContext::kTown).policy),
			PolicyName(loaded.For(MusicContext::kTavern).policy),
			PolicyName(loaded.For(MusicContext::kDungeon).policy),
			loaded.currentMusicDiagnostics);
		if (loaded.debugLogging) {
			for (const auto context : kManagedMusicContexts) {
				const auto& settings = loaded.For(context);
				logs::debug("[settings] context={} policy={} interval={:.1f}..{:.1f}s",
					ContextName(context), PolicyName(settings.policy),
					settings.minimumSilenceSeconds, settings.maximumSilenceSeconds);
			}
		}
	}

	bool Save()
	{
		const auto snapshot = GetSnapshot();

		try {
			CSimpleIniA ini;
			Configure(ini);
			if (std::filesystem::exists(kSettingsPath)) {
				const auto loadResult = ini.LoadFile(kSettingsPath);
				if (loadResult < 0) {
					logs::error("Failed to read {} before saving settings (error {})", kSettingsPath, loadResult);
					return false;
				}
			}

			ini.SetBoolValue(kGeneralSection, "Enabled", snapshot.enabled);
			for (const auto context : kManagedMusicContexts) {
				const auto section = ContextName(context);
				const auto& settings = snapshot.For(context);
				ini.SetValue(section.data(), "Policy", PolicyName(settings.policy).data());
				ini.SetDoubleValue(section.data(), "MinimumSilenceSeconds", settings.minimumSilenceSeconds);
				ini.SetDoubleValue(section.data(), "MaximumSilenceSeconds", settings.maximumSilenceSeconds);
			}
			ini.SetBoolValue(kDiagnosticsSection, "DebugLogging", snapshot.debugLogging);
			for (const auto subcontext : { MusicSubcontext::kExplorationDay, MusicSubcontext::kExplorationNight }) {
				ini.SetValue("Exploration.Overrides", SubcontextName(subcontext).data(),
					OverrideName(snapshot.overrides[static_cast<std::size_t>(subcontext)]).data());
			}
			ini.SetBoolValue(kDiagnosticsSection, "CurrentMusicDiagnostics", snapshot.currentMusicDiagnostics);

			const auto result = ini.SaveFile(kSettingsPath);
			if (result < 0) {
				logs::error("Failed to save settings to {} (error {})", kSettingsPath, result);
				return false;
			}

			logs::debug("Saved settings to {}", kSettingsPath);
			return true;
		} catch (const std::exception& e) {
			logs::error("Failed to save settings to {}: {}", kSettingsPath, e.what());
			return false;
		}
	}

	bool CurrentMusicDiagnosticsEnabled() noexcept
	{
		return g_currentMusicDiagnostics.load(std::memory_order_relaxed);
	}

	std::string_view OverrideName(PolicyOverride a_override) noexcept
	{
		if (a_override == PolicyOverride::kInherit) return "Inherit";
		return PolicyName(static_cast<Policy>(static_cast<unsigned>(a_override) - 1));
	}
}
