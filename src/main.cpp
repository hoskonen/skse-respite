#include "Menu.h"
#include "Observability.h"
#include "PolicyEngine.h"
#include "Settings.h"

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			logs::debug("[lifecycle] PostLoad");
			Respite::Menu::Register();
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			logs::debug("[lifecycle] PostPostLoad");
			break;
		case SKSE::MessagingInterface::kInputLoaded:
			logs::debug("[lifecycle] InputLoaded");
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			logs::info("[lifecycle] DataLoaded; policy state reset");
			Respite::PolicyEngine::Reset("kDataLoaded", true);
			Respite::Observability::OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			logs::info("[lifecycle] PreLoadGame; policy state suspended");
			Respite::PolicyEngine::Reset("kPreLoadGame", false);
			Respite::Observability::OnPreLoadGame();
			break;
		case SKSE::MessagingInterface::kPostLoadGame: {
			const bool succeeded = a_message->data != nullptr;
			logs::info("[lifecycle] PostLoadGame success={}; policy state reset", succeeded);
			Respite::PolicyEngine::Reset(
				succeeded ? "kPostLoadGame(success)" : "kPostLoadGame(failure)",
				succeeded);
			Respite::Observability::OnPostLoadGame(succeeded);
			break;
		}
		case SKSE::MessagingInterface::kSaveGame:
			logs::debug("[lifecycle] SaveGame");
			break;
		case SKSE::MessagingInterface::kDeleteGame:
			logs::debug("[lifecycle] DeleteGame");
			break;
		case SKSE::MessagingInterface::kNewGame:
			logs::info("[lifecycle] NewGame; policy state reset");
			Respite::PolicyEngine::Reset("kNewGame", true);
			Respite::Observability::OnNewGame();
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	logs::info("Respite is loading");

	try {
		Respite::Settings::Load();
		Respite::Observability::Install();

		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
			logs::critical("Failed to register the SKSE messaging listener");
			return false;
		}
	} catch (const std::exception& e) {
		logs::critical("Failed to initialize Respite: {}", e.what());
		return false;
	} catch (...) {
		logs::critical("Failed to initialize Respite: unknown exception");
		return false;
	}

	logs::info("Respite loaded; ambient contexts use independent policies and exempt music retains native behavior");

	return true;
}
