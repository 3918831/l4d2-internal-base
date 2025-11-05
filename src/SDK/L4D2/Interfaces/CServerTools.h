#pragma once
#include "../../../Util/Math/Vector/Vector.h"

class IClientEntity;
class IServerEntity;

class CEntityRespawnInfo
{
public:
	int m_nHammerID;
	const char* m_pEntText;
};

// All interfaces derive from this.
//class IBaseInterface
//{
//public:
//	virtual	~IBaseInterface() {}
//};


//-----------------------------------------------------------------------------
// Purpose: Interface from engine to tools for manipulating entities
//-----------------------------------------------------------------------------
class IServerTools : public IBaseInterface
{
public:
	virtual IServerEntity* GetIServerEntity(IClientEntity* pClientEntity) = 0;
	virtual bool SnapPlayerToPosition(const Vector& org, const QAngle& ang, IClientEntity* pClientPlayer = NULL) = 0;
	virtual bool GetPlayerPosition(Vector& org, QAngle& ang, IClientEntity* pClientPlayer = NULL) = 0;
	virtual bool SetPlayerFOV(int fov, IClientEntity* pClientPlayer = NULL) = 0;
	virtual int GetPlayerFOV(IClientEntity* pClientPlayer = NULL) = 0;
	virtual bool IsInNoClipMode(IClientEntity* pClientPlayer = NULL) = 0;

	// entity searching
	virtual void* FirstEntity(void) = 0;
	virtual void* NextEntity(void* pEntity) = 0;
	virtual void* FindEntityByHammerID(int iHammerID) = 0;

	// entity query
	virtual bool GetKeyValue(void* pEntity, const char* szField, char* szValue, int iMaxLen) = 0;
	virtual bool SetKeyValue(void* pEntity, const char* szField, const char* szValue) = 0;
	virtual bool SetKeyValue(void* pEntity, const char* szField, float flValue) = 0;
	virtual bool SetKeyValue(void* pEntity, const char* szField, const Vector& vecValue) = 0;

	// entity spawning
	virtual void* CreateEntityByName(const char* szClassName) = 0;
	virtual void DispatchSpawn(void* pEntity) = 0;
	virtual bool DestroyEntityByHammerId(int iHammerID) = 0;

	// This function respawns the entity into the same entindex slot AND tricks the EHANDLE system into thinking it's the same
	// entity version so anyone holding an EHANDLE to the entity points at the newly-respawned entity.
	virtual bool RespawnEntitiesWithEdits(CEntityRespawnInfo* pInfos, int nInfos) = 0;

	// This reloads a portion or all of a particle definition file.
	// It's up to the server to decide if it cares about this file
	// Use a UtlBuffer to crack the data
	virtual void ReloadParticleDefintions(const char* pFileName, const void* pBufData, int nLen) = 0;

	virtual void AddOriginToPVS(const Vector& org) = 0;
	virtual void MoveEngineViewTo(const Vector& vPos, const QAngle& vAngles) = 0;

	// Call UTIL_Remove on the entity.
	virtual void RemoveEntity(int nHammerID) = 0;
};

namespace I { inline IServerTools* CServerTools = nullptr; }