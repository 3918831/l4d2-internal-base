#pragma once

#include "../public/worldsize.h"
#include "../../SDk/SDK.h"

class CWeaponPortalgun
{
public:
    void    FirePortal1();
    void    FirePortal2();
    void    FirePortal(bool bPortal2, Vector* pVector = 0, bool bTest = false);
    void    TraceFirePortal(bool bPortal2, const Vector& vTraceStart, const Vector& vDirection, trace_t& tr, Vector& vFinalPosition, Vector& vNormal, QAngle& qFinalAngles, int iPlacedBy, bool bTest /*= false*/);
    

    float   m_flModelScale;
    float   m_fMinRange1 = 0.0f;
    float   m_fMaxRange1 = MAX_TRACE_LENGTH;
    C_TerrorPlayer* pPlayer;

};