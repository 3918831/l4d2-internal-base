#pragma once

#include "usercmd.h"

class CGlobalVarsBase
{
public:
	CGlobalVarsBase( bool bIsClient );

	// This can be used to filter debug output or to catch the client or server in the act.
	bool IsClient() const;

	// for encoding m_flSimulationTime, m_flAnimTime
	int GetNetworkBase( int nTick, int nEntity );

public:
	float			realtime;
	int				framecount;
	float			absoluteframetime;
	float			curtime;
	float			frametime;
	int				maxClients;
	int				tickcount;
	float			interval_per_tick;
	float			interpolation_amount;
	int				simTicksThisFrame;
	int				network_protocol;
	void*			pSaveData;
	bool			m_bClient;
	int				nTimestampNetworkingBase;
	int				nTimestampRandomizeWindow;
};

inline CGlobalVarsBase::CGlobalVarsBase( bool bIsClient ) :
	m_bClient( bIsClient ),
	nTimestampNetworkingBase( 100 ),
	nTimestampRandomizeWindow( 32 )
{
}

namespace I { inline CGlobalVarsBase* GlobalVars = nullptr; }

#define TICK_INTERVAL			(I::GlobalVars->interval_per_tick)
#define TIME_TO_TICKS( dt )		( (int)( 0.5f + (float)(dt) / TICK_INTERVAL ) )
#define TICKS_TO_TIME( t )		( TICK_INTERVAL *( t ) )
#define ROUND_TO_TICKS( t )		( TICK_INTERVAL * TIME_TO_TICKS( t ) )
#define TICK_NEVER_THINK		(-1)
