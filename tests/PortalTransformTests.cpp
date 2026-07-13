#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/Portal/L4D2_Portal.h"
#include "../src/Portal/PortalTransform.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    void ExpectNear(float actual, float expected, const char* message)
    {
        Expect(std::fabs(actual - expected) < 0.001f, message);
    }

    QAngle MakeAngles(float x, float y, float z)
    {
        QAngle angles;
        angles.x = x;
        angles.y = y;
        angles.z = z;
        return angles;
    }

    PortalInfo_t MakePortal(const Vector& origin, const QAngle& angles)
    {
        PortalInfo_t portal;
        portal.origin = origin;
        portal.angles = angles;
        U::Math.AngleVectors(angles, &portal.normal, nullptr, nullptr);
        return portal;
    }
}

int main()
{
    const PortalInfo_t entry = MakePortal(Vector(100.0f, 200.0f, 10.0f), MakeAngles(0.0f, 0.0f, 0.0f));
    const PortalInfo_t exit = MakePortal(Vector(-300.0f, 400.0f, 20.0f), MakeAngles(0.0f, 90.0f, 0.0f));
    const Vector head = entry.origin + Vector(12.0f, -7.0f, 30.0f);

    const PortalTransform::PortalLocalPoint entryLocal = PortalTransform::WorldToPortalLocal(entry, head);
    const Vector gmodHead = PortalTransform::ComputeGModPortalHeadPosition(entry, exit, head);
    const PortalTransform::PortalLocalPoint exitLocal = PortalTransform::WorldToPortalLocal(exit, gmodHead);

    ExpectNear(exitLocal.forward, -entryLocal.forward, "GMod position offset mirrors local forward across the entry portal");
    ExpectNear(exitLocal.right, -entryLocal.right, "GMod position offset mirrors local right across the entry portal");
    ExpectNear(exitLocal.up, entryLocal.up, "GMod position offset preserves local up across the entry portal");

    const Vector eyeFromOrigin(0.0f, 0.0f, 62.0f);
    const Vector gmodOrigin = PortalTransform::ComputeOriginForGModHeadPosition(gmodHead, eyeFromOrigin);
    ExpectNear((gmodOrigin + eyeFromOrigin).x, gmodHead.x, "GMod SetHeadPos-compatible origin preserves head x");
    ExpectNear((gmodOrigin + eyeFromOrigin).y, gmodHead.y, "GMod SetHeadPos-compatible origin preserves head y");
    ExpectNear((gmodOrigin + eyeFromOrigin).z, gmodHead.z, "GMod SetHeadPos-compatible origin preserves head z");

    std::cout << "PortalTransform tests passed\n";
    return 0;
}
