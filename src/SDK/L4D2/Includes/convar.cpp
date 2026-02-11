#include "convar.h"
#include "../Interfaces/ICvar.h"
#include <cstring>
#include <cstdlib>
#include <stdio.h>

// Forward declaration of I:: namespace
namespace I { extern ICvar* Cvar; }

// Static member initialization
ConCommandBase* ConCommandBase::s_pConCommandBases = nullptr;
IConCommandBaseAccessor* ConCommandBase::s_pAccessor = nullptr;

//-----------------------------------------------------------------------------
// ConCommandBase implementation
//-----------------------------------------------------------------------------

ConCommandBase::ConCommandBase(void) : m_pNext(nullptr), m_bRegistered(false), m_pszName(""), m_pszHelpString(""), m_nFlags(0)
{
}

ConCommandBase::ConCommandBase(const char* pName, const char* pHelpString, int flags) :
	m_pNext(nullptr), m_bRegistered(false), m_pszName(pName), m_pszHelpString(pHelpString), m_nFlags(flags)
{
}

ConCommandBase::~ConCommandBase(void)
{
}

bool ConCommandBase::IsCommand(void) const
{
	return true;
}

bool ConCommandBase::IsFlagSet(int flag) const
{
	return (m_nFlags & flag) != 0;
}

void ConCommandBase::AddFlags(int flags)
{
	m_nFlags |= flags;
}

void ConCommandBase::RemoveFlags(int flags)
{
	m_nFlags &= ~flags;
}

int ConCommandBase::GetFlags() const
{
	return m_nFlags;
}

const char* ConCommandBase::GetName(void) const
{
	return m_pszName;
}

const char* ConCommandBase::GetHelpText(void) const
{
	return m_pszHelpString;
}

const ConCommandBase* ConCommandBase::GetNext(void) const
{
	return m_pNext;
}

ConCommandBase* ConCommandBase::GetNext(void)
{
	return m_pNext;
}

bool ConCommandBase::IsRegistered(void) const
{
	return m_bRegistered;
}

CVarDLLIdentifier_t ConCommandBase::GetDLLIdentifier() const
{
	return 0;
}

void ConCommandBase::Create(const char* pName, const char* pHelpString, int flags)
{
	m_pszName = pName;
	m_pszHelpString = pHelpString;
	m_nFlags = flags;
}

void ConCommandBase::Init()
{
	// DEBUG: Only log for our test cvars to reduce noise
	bool is_test_cvar = (strcmp(m_pszName, "test_var") == 0 ||
	                     strcmp(m_pszName, "test_var_float") == 0 ||
	                     strcmp(m_pszName, "test_var_bool") == 0 ||
	                     strncmp(m_pszName, "test_", 5) == 0);

	if (is_test_cvar) {
		if (I::Cvar) {
			I::Cvar->ConsolePrintf("[CVar DEBUG] Init() called for: %s (this=%p)\n", m_pszName, this);
		} else {
			printf("[CVar DEBUG] WARNING: I::Cvar is NULL during Init() for %s\n", m_pszName);
			return;
		}
	}

	// Don't add to linked list here - constructors already do that
	// The constructor has already added this instance to s_pConCommandBases

	// Register with the accessor if available
	if (s_pAccessor)
	{
		if (is_test_cvar && I::Cvar) {
			I::Cvar->ConsolePrintf("[CVar DEBUG] Registering with accessor: %s\n", m_pszName);
		}
		s_pAccessor->RegisterConCommandBase(this);
	}

	// Register with the game's ICvar interface if available
	// This is critical for the cvars to be accessible in the game console
	if (is_test_cvar && I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] Attempting to register '%s' with game ICvar (flags=0x%X)...\n", m_pszName, m_nFlags);
	}
	I::Cvar->RegisterConCommand(this);
	if (is_test_cvar && I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] RegisterConCommand call completed for '%s'\n", m_pszName);
	}

	// Mark as registered
	m_bRegistered = true;
}

void ConCommandBase::Shutdown()
{
	// TODO: Implement shutdown logic
}

char* ConCommandBase::CopyString(const char* from)
{
	if (!from)
		return nullptr;

	size_t len = strlen(from) + 1;
	char* result = new char[len];
	strcpy_s(result, len, from);
	return result;
}

//-----------------------------------------------------------------------------
// ConCommand implementation
//-----------------------------------------------------------------------------

ConCommand::ConCommand(const char* pName, FnCommandCallbackV1_t callback, const char* pHelpString, int flags, FnCommandCompletionCallback completionFunc) :
	ConCommandBase(pName, pHelpString, flags),
	m_fnCommandCallbackV1(callback),
	m_fnCompletionCallback(completionFunc),
	m_bHasCompletionCallback(completionFunc != nullptr),
	m_bUsingNewCommandCallback(false),
	m_bUsingCommandCallbackInterface(false)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConCommand constructor (V1 callback) called: %s\n", pName);
	}

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConCommand %s added to linked list (next=%p)\n", pName, m_pNext);
	}
}

ConCommand::ConCommand(const char* pName, FnCommandCallback_t callback, const char* pHelpString, int flags, FnCommandCompletionCallback completionFunc) :
	ConCommandBase(pName, pHelpString, flags),
	m_fnCommandCallback(callback),
	m_fnCompletionCallback(completionFunc),
	m_bHasCompletionCallback(completionFunc != nullptr),
	m_bUsingNewCommandCallback(true),
	m_bUsingCommandCallbackInterface(false)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConCommand constructor (callback) called: %s\n", pName);
	}

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConCommand %s added to linked list (next=%p)\n", pName, m_pNext);
	}
}

ConCommand::ConCommand(const char* pName, ICommandCallback* pCallback, const char* pHelpString, int flags, ICommandCompletionCallback* pCommandCompletionCallback) :
	ConCommandBase(pName, pHelpString, flags),
	m_pCommandCallback(pCallback),
	m_pCommandCompletionCallback(pCommandCompletionCallback),
	m_bHasCompletionCallback(pCommandCompletionCallback != nullptr),
	m_bUsingNewCommandCallback(false),
	m_bUsingCommandCallbackInterface(true)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConCommand constructor (interface callback) called: %s\n", pName);
	}

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConCommand %s added to linked list (next=%p)\n", pName, m_pNext);
	}
}

ConCommand::~ConCommand(void)
{
}

bool ConCommand::IsCommand(void) const
{
	return true;
}

int ConCommand::AutoCompleteSuggest(const char* partial, CUtlVector<CUtlString>& commands)
{
	if (!m_bHasCompletionCallback || !m_fnCompletionCallback)
		return 0;

	// Note: This implementation is simplified - the completion callback expects
	// a char array format, not CUtlVector. For full compatibility, more work is needed.
	return 0;
}

bool ConCommand::CanAutoComplete(void)
{
	return m_bHasCompletionCallback;
}

void ConCommand::Dispatch(const CCommand& command)
{
	if (m_bUsingCommandCallbackInterface)
	{
		if (m_pCommandCallback)
			m_pCommandCallback->CommandCallback(command);
	}
	else if (m_bUsingNewCommandCallback)
	{
		if (m_fnCommandCallback)
			m_fnCommandCallback(command);
	}
	else
	{
		if (m_fnCommandCallbackV1)
			m_fnCommandCallbackV1();
	}
}

//-----------------------------------------------------------------------------
// ConVar implementation
//-----------------------------------------------------------------------------

ConVar::ConVar(const char* pName, const char* pDefaultValue, int flags) :
	ConCommandBase(pName, "Not Defined", flags),
	m_pParent(this),
	m_pszDefaultValue(pDefaultValue),
	m_pszString(nullptr),
	m_StringLength(0),
	m_fValue(0.0f),
	m_nValue(0),
	m_fnChangeCallback(nullptr)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar constructor called: %s\n", pName);
	}

	// Initialize string value
	m_StringLength = (int)strlen(pDefaultValue) + 1;
	m_pszString = new char[m_StringLength];
	strcpy_s(const_cast<char*>(m_pszString), m_StringLength, pDefaultValue);

	// Parse initial values
	m_fValue = (float)atof(pDefaultValue);
	m_nValue = atoi(pDefaultValue);

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar %s added to linked list\n", pName);
	}
}

ConVar::ConVar(const char* pName, const char* pDefaultValue, int flags, const char* pHelpString) :
	ConCommandBase(pName, pHelpString, flags),
	m_pParent(this),
	m_pszDefaultValue(pDefaultValue),
	m_pszString(nullptr),
	m_StringLength(0),
	m_fValue(0.0f),
	m_nValue(0),
	m_fnChangeCallback(nullptr)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar constructor (with help) called: %s = %s\n", pName, pDefaultValue);
	}

	// Initialize string value
	m_StringLength = (int)strlen(pDefaultValue) + 1;
	m_pszString = new char[m_StringLength];
	strcpy_s(const_cast<char*>(m_pszString), m_StringLength, pDefaultValue);

	// Parse initial values
	m_fValue = (float)atof(pDefaultValue);
	m_nValue = atoi(pDefaultValue);

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar %s added to linked list (next=%p)\n", pName, m_pNext);
	}
}

ConVar::ConVar(const char* pName, const char* pDefaultValue, int flags, const char* pHelpString, bool bMin, float fMin, bool bMax, float fMax) :
	ConCommandBase(pName, pHelpString, flags),
	m_pParent(this),
	m_pszDefaultValue(pDefaultValue),
	m_pszString(nullptr),
	m_StringLength(0),
	m_fValue(0.0f),
	m_nValue(0),
	m_fnChangeCallback(nullptr)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar constructor (with min/max) called: %s = %s\n", pName, pDefaultValue);
	}

	// Initialize string value
	m_StringLength = (int)strlen(pDefaultValue) + 1;
	m_pszString = new char[m_StringLength];
	strcpy_s(const_cast<char*>(m_pszString), m_StringLength, pDefaultValue);

	// Parse initial values
	m_fValue = (float)atof(pDefaultValue);
	m_nValue = atoi(pDefaultValue);

	// Set min/max if specified
	if (bMin)
	{
		m_fMinVal = fMin;
	}
	if (bMax)
	{
		m_fMaxVal = fMax;
	}

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar %s added to linked list (next=%p)\n", pName, m_pNext);
	}
}

ConVar::ConVar(const char* pName, const char* pDefaultValue, int flags, const char* pHelpString, FnChangeCallback_t callback) :
	ConCommandBase(pName, pHelpString, flags),
	m_pParent(this),
	m_pszDefaultValue(pDefaultValue),
	m_pszString(nullptr),
	m_StringLength(0),
	m_fValue(0.0f),
	m_nValue(0),
	m_fnChangeCallback(callback)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar constructor (with callback) called: %s = %s\n", pName, pDefaultValue);
	}

	// Initialize string value
	m_StringLength = (int)strlen(pDefaultValue) + 1;
	m_pszString = new char[m_StringLength];
	strcpy_s(const_cast<char*>(m_pszString), m_StringLength, pDefaultValue);

	// Parse initial values
	m_fValue = (float)atof(pDefaultValue);
	m_nValue = atoi(pDefaultValue);

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar %s added to linked list (next=%p)\n", pName, m_pNext);
	}
}

ConVar::ConVar(const char* pName, const char* pDefaultValue, int flags, const char* pHelpString, bool bMin, float fMin, bool bMax, float fMax, FnChangeCallback_t callback) :
	ConCommandBase(pName, pHelpString, flags),
	m_pParent(this),
	m_pszDefaultValue(pDefaultValue),
	m_pszString(nullptr),
	m_StringLength(0),
	m_fValue(0.0f),
	m_nValue(0),
	m_fnChangeCallback(callback)
{
	// DEBUG: Log constructor call
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar constructor (with min/max + callback) called: %s = %s\n", pName, pDefaultValue);
	}

	// Initialize string value
	m_StringLength = (int)strlen(pDefaultValue) + 1;
	m_pszString = new char[m_StringLength];
	strcpy_s(const_cast<char*>(m_pszString), m_StringLength, pDefaultValue);

	// Parse initial values
	m_fValue = (float)atof(pDefaultValue);
	m_nValue = atoi(pDefaultValue);

	// Set min/max if specified
	if (bMin)
	{
		m_fMinVal = fMin;
	}
	if (bMax)
	{
		m_fMaxVal = fMax;
	}

	// Add to the static linked list of all cvars/commands
	m_pNext = s_pConCommandBases;
	s_pConCommandBases = this;

	// DEBUG: Log after adding to list
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ConVar %s added to linked list (next=%p)\n", pName, m_pNext);
	}
}

ConVar::~ConVar(void)
{
	delete[] m_pszString;
}

bool ConVar::IsFlagSet(int flag) const
{
	return (m_nFlags & flag) != 0;
}

void ConVar::AddFlags(int flags)
{
	m_nFlags |= flags;
}

int ConVar::GetFlags(void) const
{
	return m_nFlags;
}

const char* ConVar::GetName(void) const
{
	return m_pszName;
}

const char* ConVar::GetHelpText(void) const
{
	return m_pszHelpString;
}

bool ConVar::IsRegistered(void) const
{
	return m_bRegistered;
}

bool ConVar::IsCommand(void) const
{
	return false;
}

void ConVar::InstallChangeCallback(FnChangeCallback_t callback)
{
	m_fnChangeCallback = callback;
}

void ConVar::SetValue(const char* value)
{
	InternalSetValue(value);
}

void ConVar::SetValue(float value)
{
	InternalSetFloatValue(value);
}

void ConVar::SetValue(int value)
{
	InternalSetIntValue(value);
}

void ConVar::InternalSetValue(const char* value)
{
	float flOldValue = m_fValue;
	const char* pszOldValue = m_pszString;

	// Update string value
	delete[] m_pszString;
	m_StringLength = (int)strlen(value) + 1;
	m_pszString = new char[m_StringLength];
	strcpy_s(const_cast<char*>(m_pszString), m_StringLength, value);

	// Parse new values
	m_fValue = (float)atof(value);
	m_nValue = atoi(value);

	// Call change callback
	if (m_fnChangeCallback)
	{
		m_fnChangeCallback(this, pszOldValue, flOldValue);
	}
}

void ConVar::InternalSetFloatValue(float fNewValue)
{
	ClampValue(fNewValue);
	char val[32];
	sprintf_s(val, "%f", fNewValue);
	InternalSetValue(val);
}

void ConVar::InternalSetIntValue(int nValue)
{
	char val[32];
	sprintf_s(val, "%d", nValue);
	InternalSetValue(val);
}

bool ConVar::ClampValue(float& value)
{
	// Note: This implementation assumes min/max are set
	// In a full implementation, you'd check m_bHasMin/m_bHasMax
	// For simplicity, we're skipping that check here
	return true;
}

void ConVar::ChangeStringValue(const char* tempVal, float flOldValue)
{
	InternalSetValue(tempVal);
}

void ConVar::SetValue(Color value)
{
	InternalSetColorValue(value);
}

void ConVar::InternalSetColorValue(Color value)
{
	// Convert Color to int value
	int nValue = value.GetRawColor();
	char val[32];
	sprintf_s(val, "%d", nValue);
	InternalSetValue(val);
}

void ConVar::Create(const char* pName, const char* pHelpString, int flags, const char* pDefaultValue, bool bMin, float fMin, bool bMax, float fMax, FnChangeCallback_t callback)
{
	// This is a helper method that can be called to initialize a ConVar
	// For this implementation, we're handling initialization in the constructors
}

void ConVar::Init()
{
	ConCommandBase::Init();
}

const char* ConVar::GetBaseName(void) const
{
	return m_pszName;
}

int ConVar::GetSplitScreenPlayerSlot(void) const
{
	return 0;
}

bool ConVar::GetMin(float& minVal) const
{
	return false;
}

bool ConVar::GetMax(float& maxVal) const
{
	return false;
}

const char* ConVar::GetDefault(void) const
{
	return m_pszDefaultValue;
}

//-----------------------------------------------------------------------------
// CCommand implementation
//-----------------------------------------------------------------------------

CCommand::CCommand() : m_nArgc(0), m_nArgv0Size(0)
{
	m_pArgvBuffer[0] = 0;
	m_pArgSBuffer[0] = 0;
	for (int i = 0; i < COMMAND_MAX_ARGC; i++)
		m_ppArgv[i] = nullptr;
}

CCommand::CCommand(int nArgC, const char** ppArgV) : m_nArgc(nArgC), m_nArgv0Size(0)
{
	// Simplified implementation
}

bool CCommand::Tokenize(const char* pCommand, characterset_t* pBreakSet)
{
	// Simplified tokenization - for now just return false
	// A full implementation would parse the command string
	m_nArgc = 0;
	m_pArgvBuffer[0] = 0;
	m_pArgSBuffer[0] = 0;
	return false;
}

void CCommand::Reset()
{
	m_nArgc = 0;
	m_nArgv0Size = 0;
	m_pArgvBuffer[0] = 0;
	m_pArgSBuffer[0] = 0;
	for (int i = 0; i < COMMAND_MAX_ARGC; i++)
		m_ppArgv[i] = nullptr;
}

const char* CCommand::FindArg(const char* pName) const
{
	for (int i = 1; i < m_nArgc; i++)
	{
		if (strcmp(m_ppArgv[i], pName) == 0)
		{
			if (i + 1 < m_nArgc)
				return m_ppArgv[i + 1];
			return "";
		}
	}
	return nullptr;
}

int CCommand::FindArgInt(const char* pName, int nDefaultVal) const
{
	const char* pVal = FindArg(pName);
	if (pVal)
		return atoi(pVal);
	return nDefaultVal;
}

// Note: characterset_t is forward-declared in convar.h but not defined
// For basic functionality, we can return nullptr
characterset_t* CCommand::DefaultBreakSet()
{
	return nullptr;
}

//-----------------------------------------------------------------------------
// ConVar_Register implementation
//-----------------------------------------------------------------------------

class CDefaultAccessor : public IConCommandBaseAccessor
{
public:
	virtual bool RegisterConCommandBase(ConCommandBase* pVar) override
	{
		// The Init() method in ConCommandBase will handle setting m_bRegistered
		// We don't need to do anything here
		return true;
	}
};

static CDefaultAccessor s_DefaultAccessor;

void ConVar_Register(int nCVarFlag, IConCommandBaseAccessor* pAccessor)
{
	// DEBUG: Log entry to ConVar_Register
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] ===== ConVar_Register called =====\n");
	}

	if (pAccessor)
	{
		ConCommandBase::s_pAccessor = pAccessor;
	}
	else
	{
		ConCommandBase::s_pAccessor = &s_DefaultAccessor;
	}

	// Count total cvars before Init
	int count = 0;
	int test_cvar_count = 0;
	ConCommandBase* pCur = ConCommandBase::s_pConCommandBases;
	while (pCur)
	{
		count++;
		const char* name = pCur->GetName();
		if (strncmp(name, "test_", 5) == 0) {
			test_cvar_count++;
			if (I::Cvar) {
				I::Cvar->ConsolePrintf("[CVar DEBUG] Found test cvar in list: %s (this=%p)\n", name, pCur);
			}
		}
		pCur = pCur->m_pNext;
	}
	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] Total: %d ConCommandBase instances, %d are test cvars\n", count, test_cvar_count);
		I::Cvar->ConsolePrintf("[CVar DEBUG] s_pConCommandBases head = %p\n", ConCommandBase::s_pConCommandBases);
	}

	// Initialize all ConCommandBase instances
	int init_count = 0;
	pCur = ConCommandBase::s_pConCommandBases;
	while (pCur)
	{
		init_count++;
		const char* name = pCur->GetName();
		bool is_test_cvar = (strncmp(name, "test_", 5) == 0);

		if (I::Cvar) {
			I::Cvar->ConsolePrintf("[CVar DEBUG] [%d/%d] Calling Init() for: %s (this=%p, next=%p)\n",
				init_count, count, name, pCur, pCur->m_pNext);
		}

		ConCommandBase* pNext = pCur->m_pNext;  // Save next BEFORE calling Init()
		pCur->Init();

		// Verify the list is still intact after Init()
		if (I::Cvar && is_test_cvar) {
			I::Cvar->ConsolePrintf("[CVar DEBUG] After Init(), pCur->m_pNext = %p (was %p)\n",
				pCur->m_pNext, pNext);
		}

		pCur = pNext;  // Use the saved next pointer
	}

	if (I::Cvar) {
		I::Cvar->ConsolePrintf("[CVar DEBUG] Initialized %d ConCommandBase instances\n", init_count);
		I::Cvar->ConsolePrintf("[CVar DEBUG] ===== ConVar_Register completed =====\n");
	}
}

void ConVar_Unregister()
{
	// TODO: Implement if needed
}

//-----------------------------------------------------------------------------
// ConVar utility functions
//-----------------------------------------------------------------------------

void ConVar_PrintFlags(const ConCommandBase* var)
{
	// TODO: Implement if needed
}

void ConVar_PrintDescription(const ConCommandBase* pVar)
{
	// TODO: Implement if needed
}
