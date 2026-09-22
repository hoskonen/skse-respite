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
		std::atomic_bool g_currentMusicDiagnostics{ true };

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
			for (auto& context : a_values.contexts) {
				adjusted |= Validate(context);
			}
			return adjusted;
		}

		void ApplyLoggingLevel(const Values& a_values) noexcept
		{
			const auto level = a_values.debugLogging ? spdlog::level::debug : spdlog::level::info;
			spdlog::set_level(level);
			spdlog::flush_on(level);
		}

		[[nodiscard]] Policy ParsePolicy(std::string_view a_value, bool& a_recognized) noexcept
		{
			for (const auto policy : { Policy::kNormal, Policy::kInterval, Policy::kOncePerActivation, Policy::kDisabled }) {
				if (a_value == PolicyName(policy)) {
					a_recognized = true;
					return policy;
				}
			}

			a_recognized = false;
			return Policy::kNormal;
		}

		void LoadContext(CSimpleIniA& a_ini, MusicContext a_context, Values& a_values, bool& a_recognizedPolicies)
		{
			auto& settings = a_values.For(a_context);
			const auto section = ContextName(a_context);
			bool recognized = true;
			settings.policy = ParsePolicy(
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
			logs::warn("Unknown policy in {}; affected contexts use Normal", kSettingsPath);
		}
		if (adjusted) {
			logs::warn("Adjusted invalid settings loaded from {}", kSettingsPath);
		}
		logs::info("Settings active: enabled={} debugLogging={} currentMusicDiagnostics={}", loaded.enabled, loaded.debugLogging, loaded.currentMusicDiagnostics);
		for (const auto context : kManagedMusicContexts) {
			const auto& settings = loaded.For(context);
			logs::info(
				"  {} policy={} interval={}..{}s",
				ContextName(context),
				PolicyName(settings.policy),
				settings.minimumSilenceSeconds,
				settings.maximumSilenceSeconds);
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

	std::string_view PolicyName(Policy a_policy) noexcept
	{
		switch (a_policy) {
		case Policy::kInterval:
			return "Interval";
		case Policy::kOncePerActivation:
			return "OncePerActivation";
		case Policy::kDisabled:
			return "Disabled";
		case Policy::kNormal:
		default:
			return "Normal";
		}
	}
}
