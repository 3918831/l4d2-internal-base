#include "TestCVar.h"
#include "../../SDK/SDK.h"

// Test console args using CON_COMMAND macro
// This args prints all test cvar values to the console
CON_COMMAND(test_print, "Print all test cvar values to console")
{
	if (!I::Cvar) {
		return;
	}

	I::Cvar->ConsolePrintf("=== Test CVar Values ===\n");
	I::Cvar->ConsolePrintf("test_var (string) = %s\n", F::TestCVar::test_var->GetString());
	I::Cvar->ConsolePrintf("test_var (int)    = %d\n", F::TestCVar::test_var->GetInt());
	I::Cvar->ConsolePrintf("test_var_float    = %f\n", F::TestCVar::test_var_float->GetFloat());
	I::Cvar->ConsolePrintf("test_var_bool     = %d\n", F::TestCVar::test_var_bool->GetInt());
	I::Cvar->ConsolePrintf("========================\n");
}

// Test console args that sets test_var to a specified value
CON_COMMAND(test_set, "Set test_var to a specified value - Usage: test_set <value>")
{
	if (!I::Cvar) {
		return;
	}

	if (args.ArgC() < 2) {
		I::Cvar->ConsolePrintf("Usage: test_set <value>\n");
		I::Cvar->ConsolePrintf("Example: test_set 42\n");
		return;
	}

	F::TestCVar::test_var->SetValue(args.Arg(1));
	I::Cvar->ConsolePrintf("test_var set to: %s\n", F::TestCVar::test_var->GetString());
}

// Test console args that demonstrates argument parsing
CON_COMMAND(test_args, "Demonstrate args argument parsing")
{
	if (!I::Cvar) {
		return;
	}

	I::Cvar->ConsolePrintf("=== Command Arguments ===\n");
	I::Cvar->ConsolePrintf("ArgC (argument count): %d\n", args.ArgC());
	for (int i = 0; i < args.ArgC(); i++) {
		I::Cvar->ConsolePrintf("Arg[%d] = %s\n", i, args.Arg(i));
	}
	I::Cvar->ConsolePrintf("ArgS (args after 0th): %s\n", args.ArgS());
}

// Test console args that modifies test_var_float
CON_COMMAND(test_float, "Set test_var_float to a specified value - Usage: test_float <value>")
{
	if (!I::Cvar) {
		return;
	}

	if (args.ArgC() < 2) {
		I::Cvar->ConsolePrintf("Usage: test_float <value>\n");
		I::Cvar->ConsolePrintf("Example: test_float 3.14\n");
		return;
	}

	const char* valueStr = args.Arg(1);
	float value = (float)atof(valueStr);
	F::TestCVar::test_var_float->SetValue(value);
	I::Cvar->ConsolePrintf("test_var_float set to: %f\n", F::TestCVar::test_var_float->GetFloat());
}

// Test console args that toggles test_var_bool
CON_COMMAND(test_toggle, "Toggle test_var_bool between 0 and 1")
{
	if (!I::Cvar) {
		return;
	}

	bool current = F::TestCVar::test_var_bool->GetBool();
	F::TestCVar::test_var_bool->SetValue(current ? 0 : 1);
	I::Cvar->ConsolePrintf("test_var_bool toggled: %d -> %d\n", current ? 1 : 0, F::TestCVar::test_var_bool->GetInt());
}
