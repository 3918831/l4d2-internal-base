#pragma once
#include "../../SDK/SDK.h"

class CProp_Portal
{
public:
    static CProp_Portal*    FindPortal(bool bPortal2, bool bCreateIfNothingFound /*= false*/);
    void                    PlacePortal(const Vector& vOrigin, const QAngle& qAngles, float fPlacementSuccess, bool bDelay /*= false*/);
    void                    NewLocation(const Vector& vOrigin, const QAngle& qAngles);

};
