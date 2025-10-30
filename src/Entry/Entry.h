#pragma once

#include "../Hooks/Hooks.h"

class CGlobal_ModuleEntry
{
public:
	void Load();
	void Run();
	void Func_TraceRay_Test();
	void Func_IPhysicsEnvironment_Test();
};

namespace G { inline CGlobal_ModuleEntry ModuleEntry; }