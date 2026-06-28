#include <cstdlib>
#include <cmath>
#include <iostream>

#include "../src/Portal/PortalTransitionDecision.h"

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
}

int main()
{
    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, true, true),
        "front-to-back crossing inside the aperture must commit");
    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.0f, -0.25f, true, true, true),
        "leaving the exact portal plane toward the back must commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(3.0f, 0.25f, true, true, true),
        "remaining in front of the portal must not commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(-0.25f, 0.50f, true, true, true),
        "back-to-front movement must not commit the entry portal");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, false, true),
        "crossing outside the aperture must not commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, false, true, true),
        "a crossing without previous-frame history must not commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, true, false),
        "a crossing while moving away from the portal must not commit");

    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(4.23f, 16.0f, 2.0f),
        13.77f,
        "an intersecting exit hull must be pushed fully in front of the wall");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(20.0f, 16.0f, 2.0f),
        0.0f,
        "an already clear exit hull must not receive an extra push");

    std::cout << "PortalTransitionDecision tests passed\n";
    return 0;
}
