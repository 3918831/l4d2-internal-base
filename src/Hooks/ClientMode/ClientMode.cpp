#include "ClientMode.h"

#include "../../Features/Vars.h"
#include "../../Features/EnginePrediction/EnginePrediction.h"
#include "../../Features/NoSpread/NoSpread.h"
#include "../../SDK/L4D2/Interfaces/ICvar.h"
#include "../../Portal/L4D2_Portal.h"

using namespace Hooks;

bool __fastcall ClientMode::ShouldDrawFog::Detour(void* ecx, void* edx)
{
	return Table.Original<FN>(Index)(ecx, edx);	
}

bool __fastcall ClientMode::CreateMove::Detour(void* ecx, void* edx, float input_sample_frametime, CUserCmd* cmd)
{
    // 调用原始函数
    auto result = Table.Original<FN>(Index)(ecx, edx, input_sample_frametime, cmd);

    if (!cmd || !cmd->command_number) {
        return result;
    }

    // 获取当前时间
    float curtime = I::EngineClient->OBSOLETE_Time();

    // 保存原始按键状态用于传送门创建判断
    int originalButtons = cmd->buttons;

    // === 传送门创建逻辑（使用原始按键判断）===

    // 左键 - 创建蓝色传送门
    if (originalButtons & IN_ATTACK) {
        if (G::G_L4D2Portal.m_flNextBluePortalTime <= curtime) {
            I::Cvar->ConsolePrintf("[Portal] Left click - Blue portal\n");
            G::G_L4D2Portal.m_pWeaponPortalgun->FirePortal1();
            G::G_L4D2Portal.m_flNextBluePortalTime = curtime + G::G_L4D2Portal.m_flPortalCooldown;
        }
    }

    // 右键 - 创建橙色传送门
    if (originalButtons & IN_ATTACK2) {
        if (G::G_L4D2Portal.m_flNextOrangePortalTime <= curtime) {
            I::Cvar->ConsolePrintf("[Portal] Right click - Orange portal\n");
            G::G_L4D2Portal.m_pWeaponPortalgun->FirePortal2();
            G::G_L4D2Portal.m_flNextOrangePortalTime = curtime + G::G_L4D2Portal.m_flPortalCooldown;
        }
    }

    // R键 - 关闭传送门(每扇门用自身的冷却时间)
    if (originalButtons & IN_RELOAD) {
        // 关闭蓝门
        if (G::G_L4D2Portal.m_flNextBluePortalTime <= curtime) {
            G::G_L4D2Portal.StartPortalCloseAnimation(&G::G_L4D2Portal.g_BluePortal);
            G::G_L4D2Portal.m_flNextBluePortalTime = curtime + G::G_L4D2Portal.m_flPortalCooldown;
        }
        // 关闭橙门
        if (G::G_L4D2Portal.m_flNextOrangePortalTime <= curtime) {
            G::G_L4D2Portal.StartPortalCloseAnimation(&G::G_L4D2Portal.g_OrangePortal);
            G::G_L4D2Portal.m_flNextOrangePortalTime = curtime + G::G_L4D2Portal.m_flPortalCooldown;
        }
        I::Cvar->ConsolePrintf("[Portal] Reload - Close portals\n");
    }

    // === 按键替换（用于动画播放）===
    // 右键替换为左键，与传送门创建无关
    if (cmd->buttons & IN_ATTACK2) {
        cmd->buttons &= ~IN_ATTACK2;    // 清除右键
        cmd->buttons |= IN_ATTACK;      // 设置为左键
    }

    return result;
}

void __fastcall ClientMode::DoPostScreenSpaceEffects::Detour(void* ecx, void* edx, const void* pSetup)
{
    Table.Original<FN>(Index)(ecx, edx, pSetup);
}

float __fastcall ClientMode::GetViewModelFOV::Detour(void* ecx, void* edx)
{
    return Table.Original<FN>(Index)(ecx, edx);
}

void ClientMode::Init()
{
    XASSERT(Table.Init(I::ClientMode) == false);
    XASSERT(Table.Hook(&ShouldDrawFog::Detour, ShouldDrawFog::Index) == false);
    XASSERT(Table.Hook(&CreateMove::Detour, CreateMove::Index) == false);
    XASSERT(Table.Hook(&DoPostScreenSpaceEffects::Detour, DoPostScreenSpaceEffects::Index) == false);
    XASSERT(Table.Hook(&GetViewModelFOV::Detour, GetViewModelFOV::Index) == false);
}