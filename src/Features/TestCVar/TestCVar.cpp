#include "TestCVar.h"
#include "../../SDK/SDK.h"
#include <stdio.h>

// Define the actual ConVar instances in an anonymous namespace
// They're defined outside the F namespace to avoid name conflicts
namespace {
	ConVar _test_var("test_var", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
		"Generic test variable for cvar system - can be used for testing");

	ConVar _test_var_float("test_var_float", "1.5", FCVAR_CLIENTDLL,
		"Float test variable for cvar system - default value is 1.5");

	ConVar _test_var_bool("test_var_bool", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
		"Boolean test variable for cvar system - 0 = false, 1 = true");
}

namespace F {
	// Initialize static pointers to point to the actual ConVar instances
	ConVar* TestCVar::test_var = &_test_var;
	ConVar* TestCVar::test_var_float = &_test_var_float;
	ConVar* TestCVar::test_var_bool = &_test_var_bool;

	void TestCVar::Initialize() {
		// CVars are automatically registered through static initialization
		// This function is for any additional initialization if needed

		// DEBUG: Always print with printf to verify instances exist
		printf("[CVar DEBUG] TestCVar::Initialize() called\n");
		printf("[CVar DEBUG] test_var pointer: %p (value: %s)\n", test_var, test_var->GetString());
		printf("[CVar DEBUG] test_var_float pointer: %p (value: %f)\n", test_var_float, test_var_float->GetFloat());
		printf("[CVar DEBUG] test_var_bool pointer: %p (value: %d)\n", test_var_bool, test_var_bool->GetInt());

		// Print cvar values to console on init for verification
		if (I::Cvar) {
			I::Cvar->ConsolePrintf("[CVar] Test cvars initialized:\n");
			I::Cvar->ConsolePrintf("  test_var = %s (default: %s)\n",
				test_var->GetString(), test_var->GetDefault());
			I::Cvar->ConsolePrintf("  test_var_float = %f (default: %s)\n",
				test_var_float->GetFloat(), test_var_float->GetDefault());
			I::Cvar->ConsolePrintf("  test_var_bool = %d (default: %s)\n",
				test_var_bool->GetInt(), test_var_bool->GetDefault());
		}
	}
}
