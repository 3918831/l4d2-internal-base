#pragma once

// Forward declarations
class KeyValues;
class SendTable;
class ServerClass;
class IMoveHelper;
struct Ray_t;
class CGameTrace;
typedef CGameTrace trace_t;
struct typedescription_t;
class CSaveRestoreData;
struct datamap_t;
class CBaseEntity;
class CRestore;
class CSave;
class variant_t;
struct vcollide_t;
class IRecipientFilter;
class ITraceFilter;
class INetChannelInfo;
class ISpatialPartition;
class IScratchPad3D;
class CStandardSendProxies;
class IAchievementMgr;
class CGamestatsData;
class CSteamID;
class ISPSharedMemory;
class ICollideable;
class IChangeInfoAccessor;
struct con_nprint_s;
using VPlane = cplane_t;  // Use same definition as CustomRender.h
struct PVSInfo_t;
struct CCheckTransmitInfo;
struct CSharedEdictChangeInfo;
typedef unsigned int QueryCvarCookie_t;
enum soundlevel_t;
class bf_write;
template<int> class CBitVec;

// Forward declare edict_t as struct to match existing usage
struct edict_t;

// Don't redefine MAX_EDICTS, it's already defined in const.h
// #define MAX_EDICTS (1<<MAX_EDICT_BITS)

struct bbox_t
{
	Vector mins;
	Vector maxs;
};

#define ABSOLUTE_PLAYER_LIMIT 64

class IVEngineServer
{
public:
	// Index 0
	virtual void ChangeLevel(const char* s1, const char* s2) = 0;

	// Index 1
	virtual int IsMapValid(const char* filename) = 0;

	// Index 2
	virtual bool IsDedicatedServer(void) = 0;

	// Index 3
	virtual int IsInEditMode(void) = 0;

	// Index 4
	virtual KeyValues* GetLaunchOptions(void) = 0;

	// Index 5 - PrecacheModel: Add to the server/client lookup/precache table
	// NOTE: The indices for PrecacheModel are 1 based
	//  a 0 returned from those methods indicates the model or sound was not correctly precached
	// However, generic and decal are 0 based
	// If preload is specified, the file is loaded into the server/client's cache memory before level startup
	virtual int PrecacheModel(const char* s, bool preload = false) = 0;

	// Index 6
	virtual int PrecacheSentenceFile(const char* s, bool preload = false) = 0;

	// Index 7
	virtual int PrecacheDecal(const char* name, bool preload = false) = 0;

	// Index 8
	virtual int PrecacheGeneric(const char* s, bool preload = false) = 0;

	// Index 9
	virtual bool IsModelPrecached(char const* s) const = 0;

	// Index 10
	virtual bool IsDecalPrecached(char const* s) const = 0;

	// Index 11
	virtual bool IsGenericPrecached(char const* s) const = 0;

	// Index 12
	virtual int GetClusterForOrigin(const Vector& org) = 0;

	// Index 13
	virtual int GetPVSForCluster(int cluster, int outputpvslength, unsigned char* outputpvs) = 0;

	// Index 14
	virtual bool CheckOriginInPVS(const Vector& org, const unsigned char* checkpvs, int checkpvssize) = 0;

	// Index 15
	virtual bool CheckBoxInPVS(const Vector& mins, const Vector& maxs, const unsigned char* checkpvs, int checkpvssize) = 0;

	// Index 16
	virtual int GetPlayerUserId(const edict_t* e) = 0;

	// Index 17
	virtual const char* GetPlayerNetworkIDString(const edict_t* e) = 0;

	// Index 18
	virtual bool IsUserIDInUse(int userID) = 0;

	// Index 19
	virtual int GetLoadingProgressForUserID(int userID) = 0;

	// Index 20
	virtual int GetEntityCount(void) = 0;

	// Index 21
	virtual INetChannelInfo* GetPlayerNetInfo(int playerIndex) = 0;

	// Index 22
	virtual edict_t* CreateEdict(int iForceEdictIndex = -1) = 0;

	// Index 23
	virtual void RemoveEdict(edict_t* e) = 0;

	// Index 24
	virtual void* PvAllocEntPrivateData(long cb) = 0;

	// Index 25
	virtual void FreeEntPrivateData(void* pEntity) = 0;

	// Index 26
	virtual void* SaveAllocMemory(size_t num, size_t size) = 0;

	// Index 27
	virtual void SaveFreeMemory(void* pSaveMem) = 0;

	// Index 28
	virtual void EmitAmbientSound(int entindex, const Vector& pos, const char* samp, float vol, soundlevel_t soundlevel, int fFlags, int pitch, float delay = 0.0f) = 0;

	// Index 29
	virtual void FadeClientVolume(const edict_t* pEdict, float fadePercent, float fadeOutSeconds, float holdTime, float fadeInSeconds) = 0;

	// Index 30
	virtual int SentenceGroupPick(int groupIndex, char* name, int nameBufLen) = 0;

	// Index 31
	virtual int SentenceGroupPickSequential(int groupIndex, char* name, int nameBufLen, int sentenceIndex, int reset) = 0;

	// Index 32
	virtual int SentenceIndexFromName(const char* pSentenceName) = 0;

	// Index 33
	virtual const char* SentenceNameFromIndex(int sentenceIndex) = 0;

	// Index 34
	virtual int SentenceGroupIndexFromName(const char* pGroupName) = 0;

	// Index 35
	virtual const char* SentenceGroupNameFromIndex(int groupIndex) = 0;

	// Index 36
	virtual float SentenceLength(int sentenceIndex) = 0;

	// Index 37
	virtual void ServerCommand(const char* str) = 0;

	// Index 38
	virtual void ServerExecute(void) = 0;

	// Index 39
	virtual void ClientCommand(edict_t* pEdict, const char* szFmt, ...) = 0;

	// Index 40
	virtual void LightStyle(int style, const char* val) = 0;

	// Index 41
	virtual void StaticDecal(const Vector& originInEntitySpace, int decalIndex, int entityIndex, int modelIndex, bool lowpriority) = 0;

	// Index 42
	virtual void Message_DetermineMulticastRecipients(bool usepas, const Vector& origin, CBitVec<ABSOLUTE_PLAYER_LIMIT>& playerbits) = 0;

	// Index 43
	virtual bf_write* EntityMessageBegin(int ent_index, ServerClass* ent_class, bool reliable) = 0;

	// Index 44
	virtual bf_write* UserMessageBegin(IRecipientFilter* filter, int msg_type, char const* pchMsgName) = 0;

	// Index 45
	virtual void MessageEnd(void) = 0;

	// Index 46
	virtual void ClientPrintf(edict_t* pEdict, const char* szMsg) = 0;

	// Index 47
	virtual void Con_NPrintf(int pos, const char* fmt, ...) = 0;

	// Index 48
	virtual void Con_NXPrintf(const struct con_nprint_s* info, const char* fmt, ...) = 0;

	// Index 49
	virtual void SetView(const edict_t* pClient, const edict_t* pViewent) = 0;

	// Index 50
	virtual float OBSOLETE_Time(void) = 0;

	// Index 51
	virtual void CrosshairAngle(const edict_t* pClient, float pitch, float yaw) = 0;

	// Index 52
	virtual void GetGameDir(char* szGetGameDir, int maxlength) = 0;

	// Index 53
	virtual int CompareFileTime(const char* filename1, const char* filename2, int* iCompare) = 0;

	// Index 54
	virtual bool LockNetworkStringTables(bool lock) = 0;

	// Index 55
	virtual edict_t* CreateFakeClient(const char* netname) = 0;

	// Index 56
	virtual const char* GetClientConVarValue(int clientIndex, const char* name) = 0;

	// Index 57
	virtual const char* ParseFile(const char* data, char* token, int maxlen) = 0;

	// Index 58
	virtual bool CopyFile(const char* source, const char* destination) = 0;

	// Index 59
	virtual void ResetPVS(unsigned char* pvs, int pvssize) = 0;

	// Index 60
	virtual void AddOriginToPVS(const Vector& origin) = 0;

	// Index 61
	virtual void SetAreaPortalState(int portalNumber, int isOpen) = 0;

	// Index 62
	virtual void PlaybackTempEntity(IRecipientFilter& filter, float delay, const void* pSender, const SendTable* pST, int classID) = 0;

	// Index 63
	virtual int CheckHeadnodeVisible(int nodenum, const unsigned char* pvs, int vissize) = 0;

	// Index 64
	virtual int CheckAreasConnected(int area1, int area2) = 0;

	// Index 65
	virtual int GetArea(const Vector& origin) = 0;

	// Index 66
	virtual void GetAreaBits(int area, unsigned char* bits, int buflen) = 0;

	// Index 67
	virtual bool GetAreaPortalPlane(Vector const& vViewOrigin, int portalKey, VPlane* pPlane) = 0;

	// Index 68
	virtual bool LoadGameState(char const* pMapName, bool createPlayers) = 0;

	// Index 69
	virtual void LoadAdjacentEnts(const char* pOldLevel, const char* pLandmarkName) = 0;

	// Index 70
	virtual void ClearSaveDir() = 0;

	// Index 71
	virtual const char* GetMapEntitiesString() = 0;

	// Index 72
	virtual client_textmessage_t* TextMessageGet(const char* pName) = 0;

	// Index 73
	virtual void LogPrint(const char* msg) = 0;

	// Index 74
	virtual bool IsLogEnabled() = 0;

	// Index 75
	virtual void BuildEntityClusterList(edict_t* pEdict, PVSInfo_t* pPVSInfo) = 0;

	// Index 76
	virtual void SolidMoved(edict_t* pSolidEnt, ICollideable* pSolidCollide, const Vector* pPrevAbsOrigin, bool testSurroundingBoundsOnly) = 0;

	// Index 77
	virtual void TriggerMoved(edict_t* pTriggerEnt, bool testSurroundingBoundsOnly) = 0;

	// Index 78
	virtual ISpatialPartition* CreateSpatialPartition(const Vector& worldmin, const Vector& worldmax) = 0;

	// Index 79
	virtual void DestroySpatialPartition(ISpatialPartition*) = 0;

	// Index 80
	virtual void DrawMapToScratchPad(IScratchPad3D* pPad, unsigned long iFlags) = 0;

	// Index 81
	virtual const CBitVec<(1<<MAX_EDICT_BITS)>* GetEntityTransmitBitsForClient(int iClientIndex) = 0;

	// Index 82
	virtual bool IsPaused() = 0;

	// Index 83
	virtual float GetTimescale(void) const = 0;

	// Index 84
	virtual void ForceExactFile(const char* s) = 0;

	// Index 85
	virtual void ForceModelBounds(const char* s, const Vector& mins, const Vector& maxs) = 0;

	// Index 86
	virtual void ClearSaveDirAfterClientLoad() = 0;

	// Index 87
	virtual void SetFakeClientConVarValue(edict_t* pEntity, const char* cvar, const char* value) = 0;

	// Index 88
	virtual void ForceSimpleMaterial(const char* s) = 0;

	// Index 89
	virtual int IsInCommentaryMode(void) = 0;

	// Index 90
	virtual bool IsLevelMainMenuBackground(void) = 0;

	// Index 91
	virtual void SetAreaPortalStates(const int* portalNumbers, const int* isOpen, int nPortals) = 0;

	// Index 92
	virtual void NotifyEdictFlagsChange(int iEdict) = 0;

	// Index 93
	virtual const CCheckTransmitInfo* GetPrevCheckTransmitInfo(edict_t* pPlayerEdict) = 0;

	// Index 94
	virtual CSharedEdictChangeInfo* GetSharedEdictChangeInfo() = 0;

	// Index 95
	virtual void AllowImmediateEdictReuse() = 0;

	// Index 96
	virtual bool IsInternalBuild(void) = 0;

	// Index 97
	virtual IChangeInfoAccessor* GetChangeAccessor(const edict_t* pEdict) = 0;

	// Index 98
	virtual char const* GetMostRecentlyLoadedFileName() = 0;

	// Index 99
	virtual char const* GetSaveFileName() = 0;

	// Index 100
	virtual void WriteSavegameScreenshot(const char* filename) = 0;

	// Index 101
	virtual int GetLightForPointListenServerOnly(const Vector&, bool, Vector*) = 0;

	// Index 102
	virtual int TraceLightingListenServerOnly(const Vector&, const Vector&, Vector*, Vector*) = 0;

	// Index 103
	virtual void CleanUpEntityClusterList(PVSInfo_t* pPVSInfo) = 0;

	// Index 104
	virtual void SetAchievementMgr(IAchievementMgr* pAchievementMgr) = 0;

	// Index 105
	virtual IAchievementMgr* GetAchievementMgr() = 0;

	// Index 106
	virtual int GetAppID() = 0;

	// Index 107
	virtual bool IsLowViolence() = 0;

	// Index 108
	virtual bool IsAnyClientLowViolence() = 0;

	// Index 109
	virtual QueryCvarCookie_t StartQueryCvarValue(edict_t* pPlayerEntity, const char* pName) = 0;

	// Index 110
	virtual void InsertServerCommand(const char* str) = 0;

	// Index 111
	virtual bool GetPlayerInfo(int ent_num, player_info_t* pinfo) = 0;

	// Index 112
	virtual bool IsClientFullyAuthenticated(edict_t* pEdict) = 0;

	// Index 113
	virtual void SetDedicatedServerBenchmarkMode(bool bBenchmarkMode) = 0;

	// Index 114
	virtual bool IsSplitScreenPlayer(int ent_num) = 0;

	// Index 115
	virtual edict_t* GetSplitScreenPlayerAttachToEdict(int ent_num) = 0;

	// Index 116
	virtual int GetNumSplitScreenUsersAttachedToEdict(int ent_num) = 0;

	// Index 117
	virtual edict_t* GetSplitScreenPlayerForEdict(int ent_num, int nSlot) = 0;

	// Index 118
	virtual bool IsOverrideLoadGameEntsOn() = 0;

	// Index 119
	virtual void ForceFlushEntity(int iEntity) = 0;

	// Index 120
	virtual ISPSharedMemory* GetSinglePlayerSharedMemorySpace(const char* szName, int ent_num = (1<<MAX_EDICT_BITS)) = 0;

	// Index 121
	virtual void* AllocLevelStaticData(size_t bytes) = 0;

	// Index 122
	virtual int GetClusterCount() = 0;

	// Index 123
	virtual int GetAllClusterBounds(bbox_t* pBBoxList, int maxBBox) = 0;

	// Index 124
	virtual bool IsCreatingReslist() = 0;

	// Index 125
	virtual bool IsCreatingXboxReslist() = 0;

	// Index 126
	virtual bool IsDedicatedServerForXbox() = 0;

	// Index 127
	virtual void Pause(bool bPause, bool bForce = false) = 0;

	// Index 128
	virtual void SetTimescale(float flTimescale) = 0;

	// Index 129
	virtual void SetGamestatsData(CGamestatsData* pGamestatsData) = 0;

	// Index 130
	virtual CGamestatsData* GetGamestatsData() = 0;

	// Index 131
	virtual const CSteamID* GetClientSteamID(edict_t* pPlayerEdict) = 0;

	// Index 132
	virtual void HostValidateSession() = 0;

	// Index 133
	virtual void RefreshScreenIfNecessary() = 0;

	// Index 134
	virtual void* AllocLevelStaticDataName(unsigned int, const char*) = 0;

	// Index 135
	virtual void ClientCommandKeyValues(edict_t* pEdict, KeyValues* pCommand) = 0;

	// Index 136
	virtual unsigned __int64 GetClientXUID(edict_t* pPlayerEdict) = 0;
};

namespace I { inline IVEngineServer* EngineServer = nullptr; }
