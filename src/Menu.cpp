#include "Menu.h"

#include "Settings.h"

#include <SKSEMenuFramework.h>

namespace Respite::Menu
{
	namespace
	{
		std::atomic_bool g_dirty{ false };
		SKSEMenuFramework::Model::Event* g_menuEvent{};

		void RenderContext(Settings::Values& a_values, MusicContext a_context, bool& a_changed)
		{
			auto& context = a_values.For(a_context);
			ImGuiMCP::Spacing();
			ImGuiMCP::SeparatorText(ContextName(a_context).data());

			int policy = static_cast<int>(context.policy);
			constexpr const char* policies[]{ "Normal", "Interval", "OncePerActivation", "Disabled" };
			constexpr const char* explorationPolicies[]{ "Normal", "Interval", "Disabled" };
			const bool exploration = a_context == MusicContext::kExploration;
			if (exploration) {
				policy = 0;
				for (std::size_t i = 0; i < Settings::kExplorationPolicies.size(); ++i) {
					if (Settings::kExplorationPolicies[i] == context.policy) policy = static_cast<int>(i);
				}
			}
			const auto policyLabel = std::format("Policy##{}", ContextName(a_context));
			if (ImGuiMCP::Combo(policyLabel.c_str(), &policy, exploration ? explorationPolicies : policies,
				exploration ? static_cast<int>(std::size(explorationPolicies)) : static_cast<int>(std::size(policies)))) {
				context.policy = exploration ? Settings::kExplorationPolicies[policy] : static_cast<Settings::Policy>(policy);
				a_changed = true;
			}

			if (a_values.UsesInterval(a_context)) {
				const auto minimumLabel = std::format("Minimum silence seconds##{}", ContextName(a_context));
				if (ImGuiMCP::SliderFloat(minimumLabel.c_str(), &context.minimumSilenceSeconds,
						Settings::kMinimumSilenceSeconds, Settings::kMaximumSilenceSeconds, "%.0f s")) {
					context.maximumSilenceSeconds = (std::max)(context.maximumSilenceSeconds, context.minimumSilenceSeconds);
					a_changed = true;
				}
				const auto maximumLabel = std::format("Maximum silence seconds##{}", ContextName(a_context));
				if (ImGuiMCP::SliderFloat(maximumLabel.c_str(), &context.maximumSilenceSeconds,
						Settings::kMinimumSilenceSeconds, Settings::kMaximumSilenceSeconds, "%.0f s")) {
					context.minimumSilenceSeconds = (std::min)(context.minimumSilenceSeconds, context.maximumSilenceSeconds);
					a_changed = true;
				}
			}
			if (a_context == MusicContext::kExploration && ImGuiMCP::CollapsingHeader("Advanced overrides##Exploration")) {
				ImGuiMCP::TextWrapped("Inherit uses Exploration's policy. Interval overrides use the Exploration silence range. Unresolved Day/Night evidence uses the parent policy.");
				constexpr const char* overrides[]{ "Inherit", "Normal", "Interval", "Disabled" };
				for (const auto subcontext : { MusicSubcontext::kExplorationDay, MusicSubcontext::kExplorationNight }) {
					auto& value = a_values.overrides[static_cast<std::size_t>(subcontext)];
					int selected = 0;
					for (std::size_t i = 0; i < Settings::kExplorationOverrides.size(); ++i) {
						if (Settings::kExplorationOverrides[i] == value) selected = static_cast<int>(i);
					}
					if (ImGuiMCP::Combo(SubcontextName(subcontext).data(), &selected, overrides, static_cast<int>(std::size(overrides)))) {
						value = Settings::kExplorationOverrides[selected];
						a_changed = true;
					}
				}
			}
		}

		void __stdcall RenderSettings()
		{
			auto settings = Settings::GetSnapshot();
			bool changed{};

			ImGuiMCP::SeparatorText("General");
			changed |= ImGuiMCP::Checkbox("Enabled", &settings.enabled);
			for (const auto context : kManagedMusicContexts) {
				RenderContext(settings, context, changed);
			}

			ImGuiMCP::Spacing();
			ImGuiMCP::SeparatorText("Diagnostics");
			changed |= ImGuiMCP::Checkbox("Internal debug logging", &settings.debugLogging);
			changed |= ImGuiMCP::Checkbox("Verbose/current-music diagnostics", &settings.currentMusicDiagnostics);

			if (changed) {
				Settings::Update(settings);
				g_dirty.store(true, std::memory_order_relaxed);
			}
		}

		void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_event)
		{
			if (a_event != SKSEMenuFramework::Model::EventType::kCloseMenu ||
				!g_dirty.exchange(false, std::memory_order_relaxed)) {
				return;
			}
			if (!Settings::Save()) {
				logs::error("Failed to persist settings when SKSE Menu Framework closed");
				g_dirty.store(true, std::memory_order_relaxed);
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logs::info("SKSE Menu Framework is not installed; Respite settings UI is disabled");
			return;
		}
		const auto apiVersion = SKSEMenuFramework::GetMenuFrameworkAPIVersion();
		if (apiVersion == 0) {
			logs::warn("SKSE Menu Framework was found but its API is unavailable; Respite settings UI is disabled");
			return;
		}

		SKSEMenuFramework::SetSection("Respite");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		g_menuEvent = SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0F);
		logs::info("Registered Respite settings with SKSE Menu Framework API {}; settings save on menu close", apiVersion);
	}
}
