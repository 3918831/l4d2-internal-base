#pragma once

#include "../../SDK/L4D2/Includes/convar.h"

namespace F {
	class TestCVar {
	public:
		// Initialize the test cvars (called during DLL initialization)
		static void Initialize();

		// Test cvar pointers - can be accessed from anywhere in the code
		static ConVar* test_var;           // Generic test variable (int/string)
		static ConVar* test_var_float;     // Float test variable
		static ConVar* test_var_bool;      // Boolean test variable
	};
}
