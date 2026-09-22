#pragma once

namespace Respite::Observability
{
	void Install();

	void OnDataLoaded();
	void OnPreLoadGame();
	void OnPostLoadGame(bool a_succeeded);
	void OnNewGame();
}
