#pragma once

#include "../Hooks/Hooks.h"

class CGlobal_ModuleEntry
{
public:
	void Load();
	void Run();
	void FuncTest();
};

namespace G { inline CGlobal_ModuleEntry ModuleEntry; }