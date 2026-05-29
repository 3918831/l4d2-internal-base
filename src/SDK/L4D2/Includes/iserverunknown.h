//========= Copyright ?1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ISERVERUNKNOWN_H
#define ISERVERUNKNOWN_H

#ifdef _WIN32
#pragma once
#endif


#include "..\Entities\IHandleEntity.h"

class ICollideable;
class IServerNetworkable;
class CBaseEntity;
struct string_t;


// This is the server's version of IUnknown. We may want to use a QueryInterface-like
// mechanism if this gets big.
class IServerUnknown : public IHandleEntity
{
public:
	// Gets the interface to the collideable + networkable representation of the entity
	virtual ICollideable*		GetCollideable() = 0;
	virtual IServerNetworkable*	GetNetworkable() = 0;
	virtual CBaseEntity*		GetBaseEntity() = 0;
};

class IServerEntity : public IServerUnknown
{
public:
	virtual					~IServerEntity() {}

	virtual int				GetModelIndex(void) const = 0;
	virtual string_t		GetModelName(void) const = 0;
	virtual void			SetModelIndex(int index) = 0;
};

#endif // ISERVERUNKNOWN_H
