#pragma once
#include "../../SDK/SDK.h"

typedef void(__thiscall* FnTeleport)(void*, Vector const*, QAngle const*, Vector const*);
typedef void(__thiscall* FnSetModel)(void*, const char*);

class CProp_Portal
{
public:
    static CProp_Portal*    FindPortal(bool bPortal2, bool bCreateIfNothingFound  = false);
    void                    PlacePortal(const Vector& vOrigin, const QAngle& qAngles, float fPlacementSuccess, bool bDelay = false);
    void                    NewLocation(const Vector& vOrigin, const QAngle& qAngles);
    void                    Teleport(Vector const* newPosition, QAngle const* newAngles, Vector const* newVelocity);
    void                    SetModel(const char* pModelName);

    void** vtable_ptr = *reinterpret_cast<void***>(this);

private:
    // 获取虚函数表指针
    void** GetVTable() {
        return *reinterpret_cast<void***>(this);
    }

    size_t TELEPORT_INDEX = 118;
    size_t SETMODEL_INDEX = 27;

};
