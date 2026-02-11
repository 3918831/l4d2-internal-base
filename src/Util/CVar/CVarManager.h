#pragma once

#include "../../SDK/L4D2/Includes/convar.h"

namespace U {
	class CVarManager {
	public:
		// Initialize the cvar system - registers all ConVars/ConCommands
		static void Initialize();

		// Register a single ConCommandBase with the game's ICvar
		static void RegisterConCommandBase(ConCommandBase* pCommandBase);

	private:
		static bool m_bInitialized;
	};
}
