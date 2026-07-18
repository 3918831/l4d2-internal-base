#include "PortalControlledNoclipMovement.h"

#include <algorithm>
#include <cmath>

#include "../SDK/L4D2/Interfaces/GameMovement.h"
#include "../Util/Logger/PortalFileLog.h"
#include "L4D2_Portal.h"
#include "PortalTransitionDecision.h"
#include "PortalPhysicsMode.h"

namespace
{
    constexpr float kNoclipSpeedScale = 1.75f;
    constexpr float kNoclipAccelerate = 5.0f;
    constexpr float kMaxControlledSpeed = 520.0f;
    constexpr float kMaxDownSpeed = -3000.0f;
    constexpr float kGravity = 800.0f;
    constexpr float kCorridorForwardLimit = 32.0f;
    constexpr float kCorridorHalfWidth = 20.0f;
    constexpr float kCorridorHalfHeight = 44.0f;

    struct PortalLocalPosition
    {
        float forward = 0.0f;
        float right = 0.0f;
        float up = 0.0f;
        float frontDist = 0.0f;
    };

    struct PortalClampResult
    {
        Vector origin;
        PortalLocalPosition before;
        PortalLocalPosition after;
        bool clampedRight = false;
        bool clampedUp = false;
    };

    float ClampFloat(float value, float minValue, float maxValue)
    {
        return value < minValue ? minValue : (value > maxValue ? maxValue : value);
    }

    float TickInterval()
    {
        return (I::GlobalVars && I::GlobalVars->interval_per_tick > 0.0f)
            ? I::GlobalVars->interval_per_tick
            : (1.0f / 30.0f);
    }

    PortalInfo_t* PortalForControlledMove(const PortalTransitionContext& context)
    {
        const PortalTransitionSide side = context.phase == PortalTransitionPhase::ExitingPortal
            ? context.exitSide
            : context.entrySide;

        switch (side)
        {
        case PortalTransitionSide::Blue:
            return &G::G_L4D2Portal.g_BluePortal;
        case PortalTransitionSide::Orange:
            return &G::G_L4D2Portal.g_OrangePortal;
        case PortalTransitionSide::None:
        default:
            return nullptr;
        }
    }

    const char* SideName(PortalTransitionSide side)
    {
        switch (side)
        {
        case PortalTransitionSide::Blue:
            return "Blue";
        case PortalTransitionSide::Orange:
            return "Orange";
        case PortalTransitionSide::None:
        default:
            return "None";
        }
    }

    PortalTransitionSide ControlledMoveSide(const PortalTransitionContext& context)
    {
        return context.phase == PortalTransitionPhase::ExitingPortal
            ? context.exitSide
            : context.entrySide;
    }

    Vector ClampedLength(const Vector& value, float maxLength)
    {
        const float lengthSqr = value.LenghtSqr();
        if (lengthSqr <= maxLength * maxLength || lengthSqr <= 0.0001f)
            return value;

        const float scale = maxLength / std::sqrt(lengthSqr);
        return value * scale;
    }

    Vector BuildAcceleration(const CMoveData& move)
    {
        QAngle yawOnlyAngles = move.m_vecViewAngles;
        yawOnlyAngles.x = 0.0f;
        yawOnlyAngles.z = 0.0f;

        Vector forward;
        Vector right;
        Vector up;
        U::Math.AngleVectors(yawOnlyAngles, &forward, &right, &up);

        Vector wish = forward * move.m_flForwardMove
            + right * move.m_flSideMove;
        wish.z = 0.0f;

        const float wishSpeed = std::sqrt(wish.Lenght2DSqr());
        if (wishSpeed <= 0.001f)
            return Vector();

        const float maxSpeed = std::max(1.0f, move.m_flMaxSpeed);
        const float accelSpeed = std::min(wishSpeed, maxSpeed);
        wish.NormalizeInPlace();
        return wish * (accelSpeed * kNoclipSpeedScale);
    }

    PortalLocalPosition ToPortalLocalPosition(const PortalInfo_t& portal, const Vector& origin)
    {
        Vector forward;
        Vector right;
        Vector up;
        U::Math.AngleVectors(portal.angles, &forward, &right, &up);

        const Vector delta = origin - portal.origin;
        PortalLocalPosition local;
        local.forward = delta.Dot(forward);
        local.right = delta.Dot(right);
        local.up = delta.Dot(up);
        local.frontDist = delta.Dot(portal.normal);
        return local;
    }

    PortalClampResult ClampToPortalCorridor(const PortalInfo_t& portal, const Vector& predictedOrigin)
    {
        PortalClampResult result;
        result.before = ToPortalLocalPosition(portal, predictedOrigin);
        result.after = result.before;

        if (std::fabs(result.before.forward) <= kCorridorForwardLimit)
        {
            result.after.right = ClampFloat(result.before.right, -kCorridorHalfWidth, kCorridorHalfWidth);
            result.after.up = ClampFloat(result.before.up, -kCorridorHalfHeight, kCorridorHalfHeight);
            result.clampedRight = std::fabs(result.after.right - result.before.right) > 0.001f;
            result.clampedUp = std::fabs(result.after.up - result.before.up) > 0.001f;
        }

        Vector forward;
        Vector right;
        Vector up;
        U::Math.AngleVectors(portal.angles, &forward, &right, &up);
        result.origin = portal.origin
            + forward * result.after.forward
            + right * result.after.right
            + up * result.after.up;
        result.after = ToPortalLocalPosition(portal, result.origin);
        return result;
    }
}

bool PortalControlledNoclipMovement::TryApply(
    const char* domain,
    unsigned int heartbeat,
    CMoveData* move,
    const PortalTransitionContext& context)
{
    if (!PortalPhysicsMode::ShouldMutatePlayerMovement())
        return false;

    if (!move || !PortalTransitionDecision::RequiresControlledNoclip(context.phase))
        return false;

    const PortalTransitionSide activeSide = ControlledMoveSide(context);
    PortalInfo_t* portal = PortalForControlledMove(context);
    if (!portal || !portal->bIsActive || portal->normal.LenghtSqr() < 0.25f)
        return false;

    const float dt = TickInterval();
    const Vector originBefore = move->GetAbsOrigin();
    const Vector velocityBefore = move->m_vecVelocity;

    Vector velocity = velocityBefore + BuildAcceleration(*move) * (dt * kNoclipAccelerate);
    velocity.z -= kGravity * dt;
    velocity.z = std::max(velocity.z, kMaxDownSpeed);

    const float friction = std::max(0.0f, 0.98f - dt * 5.0f);
    velocity.x *= friction;
    velocity.y *= friction;
    velocity = ClampedLength(velocity, kMaxControlledSpeed);

    const Vector unclampedOrigin = originBefore + velocity * dt;
    const PortalClampResult clamp = ClampToPortalCorridor(*portal, unclampedOrigin);
    const Vector originAfter = clamp.origin;

    move->m_vecVelocity = velocity;
    move->SetAbsOrigin(originAfter);
    move->m_bGameCodeMovedPlayer = true;

    U::PortalFileLog::WriteFormat(
        "[PortalControlledMove] domain=%s heartbeat=%u phase=%d side=%s origin=(%.2f %.2f %.2f)->(%.2f %.2f %.2f) unclamped=(%.2f %.2f %.2f) localBefore=(f=%.2f r=%.2f u=%.2f front=%.2f) localAfter=(f=%.2f r=%.2f u=%.2f front=%.2f) clamp=(r=%s u=%s) velocity=(%.2f %.2f %.2f)->(%.2f %.2f %.2f) input=(f=%.1f s=%.1f u=%.1f) maxSpeed=%.1f\n",
        domain ? domain : "unknown",
        heartbeat,
        static_cast<int>(context.phase),
        SideName(activeSide),
        originBefore.x, originBefore.y, originBefore.z,
        originAfter.x, originAfter.y, originAfter.z,
        unclampedOrigin.x, unclampedOrigin.y, unclampedOrigin.z,
        clamp.before.forward, clamp.before.right, clamp.before.up, clamp.before.frontDist,
        clamp.after.forward, clamp.after.right, clamp.after.up, clamp.after.frontDist,
        clamp.clampedRight ? "true" : "false",
        clamp.clampedUp ? "true" : "false",
        velocityBefore.x, velocityBefore.y, velocityBefore.z,
        velocity.x, velocity.y, velocity.z,
        move->m_flForwardMove,
        move->m_flSideMove,
        move->m_flUpMove,
        move->m_flMaxSpeed);

    return true;
}
