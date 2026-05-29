#pragma once

#include "iserverunknown.h"
#include "..\..\..\Util\Math\Vector\Vector.h"
#include "..\Includes\globalvars_base.h"

class IServerEntity;
struct edict_t;
//-----------------------------------------------------------------------------
// Purpose: Defines the ways that a map can be loaded.
//-----------------------------------------------------------------------------
enum MapLoadType_t
{
	MapLoad_NewGame = 0,
	MapLoad_LoadGame,
	MapLoad_Transition,
	MapLoad_Background,
};

struct string_t
{
public:
	bool operator!() const							{ return ( pszValue == NULL );			}
	bool operator==( const string_t &rhs ) const	{ return ( pszValue == rhs.pszValue );	}
	bool operator!=( const string_t &rhs ) const	{ return ( pszValue != rhs.pszValue );	}
	bool operator<( const string_t &rhs ) const		{ return ((void *)pszValue < (void *)rhs.pszValue ); }

	const char *ToCStr() const						{ return ( pszValue ) ? pszValue : ""; 	}
	
protected:
	const char *pszValue;
};
//-----------------------------------------------------------------------------
// Purpose: Global variables shared between the engine and the game .dll
//-----------------------------------------------------------------------------
class CGlobalVars : public CGlobalVarsBase
{	
public:

	CGlobalVars( bool bIsClient );

public:
	
	// Current map
	string_t		mapname;
	int				mapversion;
	string_t		startspot;
	MapLoadType_t	eLoadType;		// How the current map was loaded
	bool			bMapLoadFailed;	// Map has failed to load, we need to kick back to the main menu

	// game specific flags
	bool			deathmatch;
	bool			coop;
	bool			teamplay;
	// current maxentities
	int				maxEntities;

	int				serverCount;
	edict_t			*pEdicts;
};

inline CGlobalVars::CGlobalVars( bool bIsClient ) : 
	CGlobalVarsBase( bIsClient )
{
	serverCount = 0;
}


class IChangeInfoAccessor
{
public:
	inline void SetChangeInfo(unsigned short info)
	{
		m_iChangeInfo = info;
	}

	inline void SetChangeInfoSerialNumber(unsigned short sn)
	{
		m_iChangeInfoSerialNumber = sn;
	}

	inline unsigned short	 GetChangeInfo() const
	{
		return m_iChangeInfo;
	}

	inline unsigned short	 GetChangeInfoSerialNumber() const
	{
		return m_iChangeInfoSerialNumber;
	}

private:
	unsigned short m_iChangeInfo;
	unsigned short m_iChangeInfoSerialNumber;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
// NOTE: YOU CAN'T CHANGE THE LAYOUT OR SIZE OF CBASEEDICT AND REMAIN COMPATIBLE WITH HL2_VC6!!!!!
class CBaseEdict
{
public:

	// Returns an IServerEntity if FL_FULLEDICT is set or NULL if this 
	// is a lightweight networking entity.
	IServerEntity*			GetIServerEntity() { return static_cast<IServerEntity*>(m_pUnk); }
	const IServerEntity*	GetIServerEntity() const { return static_cast<const IServerEntity*>(m_pUnk); }

	IServerNetworkable*		GetNetworkable() { return m_pNetworkable; }
    IServerUnknown *GetUnknown() { return m_pUnk; }

    // Set when initting an entity. If it's only a networkable, this is false.
	void				SetEdict( IServerUnknown *pUnk, bool bFullEdict );
	
	int					AreaNum() const;
	const char *		GetClassName() const;
	
	bool				IsFree() const;
	void				SetFree();
	void				ClearFree();

	bool				HasStateChanged() const;
	void				ClearStateChanged();
	void				StateChanged();
	void				StateChanged( unsigned short offset );

	void				ClearTransmitState();
	
	void SetChangeInfo( unsigned short info );
	void SetChangeInfoSerialNumber( unsigned short sn );
	unsigned short	 GetChangeInfo() const;
	unsigned short	 GetChangeInfoSerialNumber() const;

public:
	int	m_fStateFlags;	

	// NOTE: this is in the edict instead of being accessed by a virtual because the engine needs fast access to it.
	int m_NetworkSerialNumber;	// Game DLL sets this when it gets a serial number for its EHANDLE.

	// NOTE: this is in the edict instead of being accessed by a virtual because the engine needs fast access to it.
	IServerNetworkable	*m_pNetworkable;

protected:
	IServerUnknown		*m_pUnk;		


public:

	IChangeInfoAccessor *GetChangeAccessor(); // The engine implements this and the game .dll implements as
	const IChangeInfoAccessor *GetChangeAccessor() const; // The engine implements this and the game .dll implements as
	// as callback through to the engine!!!

	// NOTE: YOU CAN'T CHANGE THE LAYOUT OR SIZE OF CBASEEDICT AND REMAIN COMPATIBLE WITH HL2_VC6!!!!!
	// This breaks HL2_VC6!!!!!
	// References a CEdictChangeInfo with a list of modified network props.
	//unsigned short m_iChangeInfo;
	//unsigned short m_iChangeInfoSerialNumber;

	friend void InitializeEntityDLLFields( edict_t *pEdict );
};

//-----------------------------------------------------------------------------
// Purpose: The engine's internal representation of an entity, including some
//  basic collision and position info and a pointer to the class wrapped on top
//  of the structure
//-----------------------------------------------------------------------------
struct edict_t : public CBaseEdict
{
public:
	ICollideable *GetCollideable() {}
};
