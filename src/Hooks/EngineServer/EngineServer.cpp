#include "EngineServer.h"

#include <iostream>
#include "../../SDK/L4D2/Interfaces/IVEngineServer.h"
#include "../../Portal/L4D2_Portal.h"

using namespace Hooks;

// GetArea Hook implementation
// This function is called by the engine to determine which area a position belongs to
// Areas are used for visibility calculations and portal connectivity
int __fastcall EngineServer::GetArea::Detour(void* ecx, void* edx, const Vector& origin)
{
	// Validate input parameters
	//if (ecx == nullptr)
	//{
	//	std::cerr << "[EngineServer::GetArea] ERROR: ecx (this pointer) is null!" << std::endl;
	//	return -1; // Return invalid area index
	//}

	//// Check for NaN values in origin (invalid coordinates)
	//if (isnan(origin.x) || isnan(origin.y) || isnan(origin.z))
	//{
	//	std::cerr << "[EngineServer::GetArea] WARNING: Origin contains NaN values: ("
	//		<< origin.x << ", " << origin.y << ", " << origin.z << ")" << std::endl;
	//	return -1;
	//}

	// Call original function
	if (G::G_L4D2Portal.g_BluePortal.bIsActive)	{
		Table.Original<FN>(Index)(ecx, edx, G::G_L4D2Portal.g_BluePortal.origin);
	}

	if (G::G_L4D2Portal.g_OrangePortal.bIsActive) {
		Table.Original<FN>(Index)(ecx, edx, G::G_L4D2Portal.g_OrangePortal.origin);
	}
	

	return Table.Original<FN>(Index)(ecx, edx, origin);

	// Optional: Log the area query for debugging
	// Uncomment the following lines to enable debug logging:
	// std::cout << "[EngineServer::GetArea] Origin: (" << origin.x << ", "
	//     << origin.y << ", " << origin.z << ") -> Area: " << nArea << std::endl;
}

void EngineServer::Init()
{
	// Validate that I::EngineServer interface has been initialized
	if (I::EngineServer == nullptr)
	{
		std::cerr << "[EngineServer::Init] ERROR: I::EngineServer interface is null! "
			<< "Make sure SDK initialization completes before Hook initialization." << std::endl;
		return;
	}

	// Initialize the VMT table hook
	if (Table.Init(I::EngineServer) == false)
	{
		std::cerr << "[EngineServer::Init] ERROR: Failed to initialize VMT table for IVEngineServer!" << std::endl;
		return;
	}

	// Hook the GetArea function at index 65
	if (Table.Hook(&GetArea::Detour, GetArea::Index) == false)
	{
		std::cerr << "[EngineServer::Init] ERROR: Failed to hook GetArea function!" << std::endl;
		return;
	}

	std::cout << "[EngineServer::Init] Successfully hooked IVEngineServer::GetArea (Index 65)" << std::endl;
}
