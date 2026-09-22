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
			logs::info("SKSE lifecycle: PostLoad");
			Respite::Menu::Register();
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			logs::info("SKSE lifecycle: PostPostLoad");
			break;
		case SKSE::MessagingInterface::kInputLoaded:
			logs::info("SKSE lifecycle: InputLoaded");
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			logs::info("SKSE lifecycle: DataLoaded");
			Respite::PolicyEngine::Reset("kDataLoaded", true);
			Respite::Observability::OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			logs::info("SKSE lifecycle: PreLoadGame");
			Respite::PolicyEngine::Reset("kPreLoadGame", false);
			Respite::Observability::OnPreLoadGame();
			break;
		case SKSE::MessagingInterface::kPostLoadGame: {
			const bool succeeded = a_message->data != nullptr;
			logs::info("SKSE lifecycle: PostLoadGame success={}", succeeded);
			Respite::PolicyEngine::Reset(
				succeeded ? "kPostLoadGame(success)" : "kPostLoadGame(failure)",
				succeeded);
			Respite::Observability::OnPostLoadGame(succeeded);
			break;
		}
		case SKSE::MessagingInterface::kSaveGame:
			logs::info("SKSE lifecycle: SaveGame");
			break;
		case SKSE::MessagingInterface::kDeleteGame:
			logs::info("SKSE lifecycle: DeleteGame");
			break;
		case SKSE::MessagingInterface::kNewGame:
			logs::info("SKSE lifecycle: NewGame");
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

	logs::info("Respite diagnostic observability is loading");

	try {
		Respite::Settings::Load();
		Respite::Observability::Install();

		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
			logs::critical("Failed to register the SKSE messaging listener");
			return false;
		}
	} catch (const std::exception& e) {
		logs::critical("Failed to install diagnostic observability: {}", e.what());
		return false;
	} catch (...) {
		logs::critical("Failed to install diagnostic observability: unknown exception");
		return false;
	}

	logs::info("Respite loaded; ambient contexts use independent policies and exempt music retains native behavior");

	return true;
}
