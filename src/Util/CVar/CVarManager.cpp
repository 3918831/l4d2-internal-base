#include "CVarManager.h"
#include "../../SDK/SDK.h"
#include "../Logger/Logger.h"

namespace U {
	bool CVarManager::m_bInitialized = false;

	void CVarManager::Initialize() {
		if (m_bInitialized) {
			return;
		}

		// DEBUG: Log entry to CVarManager::Initialize
		if (I::Cvar) {
			U::LogDebug("===== CVarManager::Initialize() called =====\n");
		}
		else {
			U::LogError("I::Cvar is NULL in CVarManager::Initialize()!\n");
		}

		// Register all ConVars and ConCommands that have been statically created
		ConVar_Register(FCVAR_CLIENTDLL, nullptr);

		m_bInitialized = true;
	}

	void CVarManager::RegisterConCommandBase(ConCommandBase* pCommandBase) {
		if (!pCommandBase)
			return;

		// Register with the game's ICvar interface
		if (I::Cvar) {
			I::Cvar->RegisterConCommand(pCommandBase);
		}
	}
}
