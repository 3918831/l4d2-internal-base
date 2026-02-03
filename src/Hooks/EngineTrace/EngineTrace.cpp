#include "EngineTrace.h"

#include <iostream>
#include "../../SDK/L4D2/Interfaces/EngineTrace.h"
#include "../../Portal/L4D2_Portal.h"

using namespace Hooks;

// GetLeafContainingPoint Hook implementation
// This function is called by the engine to determine which leaf (BSP tree node) contains a point
// Leaves are used for visibility determination and portal rendering
int __fastcall EngineTrace::GetLeafContainingPoint::Detour(void* ecx, void* edx, const Vector& ptTest)
{
	// Call original function
	int nLeaf = Table.Original<FN>(Index)(ecx, edx, ptTest);
	return nLeaf;
}

void EngineTrace::Init()
{
	// Validate that I::EngineTrace interface has been initialized
	if (I::EngineTrace == nullptr)
	{
		std::cerr << "[EngineTrace::Init] ERROR: I::EngineTrace interface is null! "
			<< "Make sure SDK initialization completes before Hook initialization." << std::endl;
		return;
	}

	// Initialize the VMT table hook
	if (Table.Init(I::EngineTrace) == false)
	{
		std::cerr << "[EngineTrace::Init] ERROR: Failed to initialize VMT table for IEngineTrace!" << std::endl;
		return;
	}

	// Hook the GetLeafContainingPoint function at index 18
	if (Table.Hook(&GetLeafContainingPoint::Detour, GetLeafContainingPoint::Index) == false)
	{
		std::cerr << "[EngineTrace::Init] ERROR: Failed to hook GetLeafContainingPoint function!" << std::endl;
		return;
	}

	std::cout << "[EngineTrace::Init] Successfully hooked IEngineTrace::GetLeafContainingPoint (Index 18)" << std::endl;
}
