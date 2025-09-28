#include "ModelRender.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Hooks/BaseClient/BaseClient.h"
using namespace Hooks;

// 状态管理
bool g_bDrawingPortalView = false; // 防止在绘制传送门内部世界时，再次触发传送门逻辑，造成无限递归
static int s_nRenderViewRecursion = 0; // RenderView的递归深度计数

// 在你的初始化函数中预加载这个材质
//IMaterial* g_pWriteStencilMaterial = I::MaterialSystem->FindMaterial("materials/dev/write_stencil", TEXTURE_GROUP_OTHER);

// 1. 从你的Hook管理器中获取原始函数指针，并使用正确的函数签名类型。
//    注意这里我们使用了修正后的、更可能正确的L4D2签名。
//using RenderView_FN = void(__fastcall*)(void*, void*, CViewSetup&, CViewSetup&, int, int);

extern CViewSetup g_ViewSetup;
extern CViewSetup g_hudViewSetup;
extern void* g_pClient_this_ptr;

// 检查模型是否是我们的传送门模型
bool IsPortalModel(const char* modelName) {
	// TODO: 将 "portal_model.mdl" 替换为你的传送门模型的实际名称
	// 使用 strstr 可以匹配路径中的一部分，更灵活
	return (strstr(modelName, "models/blackops/portal.mdl") != nullptr);
}

void __fastcall ModelRender::ForcedMaterialOverride::Detour(void* ecx, void* edx, IMaterial* newMaterial, OverrideType_t nOverrideType)
{
	Table.Original<FN>(Index)(ecx, edx, newMaterial, nOverrideType);
}

void __fastcall ModelRender::DrawModelExecute::Detour(void* ecx, void* edx, const DrawModelState_t& state, const ModelRenderInfo_t& pInfo, matrix3x4_t* pCustomBoneToWorld)
{
    // 基础检查和递归保护
    if (g_bDrawingPortalView || !I::EngineClient->IsInGame()) {
        Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
        return;
    }

    const char* modelName = I::ModelInfo->GetModelName(pInfo.pModel);
    if (modelName && strcmp(modelName, "models/blackops/portal.mdl") == 0)
    {
        // ============================ FIX #1: 添加初始化保护 ============================
        if (!g_pClient_this_ptr) {
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }
        // ==============================================================================

        g_bDrawingPortalView = true;
        IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
        IMaterial* g_pWriteStencilMaterial = I::MaterialSystem->FindMaterial("materials/dev/write_stencil", TEXTURE_GROUP_OTHER);

        if (pRenderContext && g_pWriteStencilMaterial)
        {
            // --- 阶段 1: 绘制模板遮罩 ---
            I::ModelRender->ForcedMaterialOverride(g_pWriteStencilMaterial);

            ShaderStencilState_t stencilState;
            stencilState.m_bEnable = true;
            stencilState.m_nReferenceValue = 1;
            stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
            stencilState.m_PassOp = STENCILOPERATION_REPLACE;
            stencilState.m_FailOp = STENCILOPERATION_KEEP;
            stencilState.m_ZFailOp = STENCILOPERATION_REPLACE; // ZFail也应REPLACE，确保被遮挡部分也能形成模板
            pRenderContext->SetStencilState(stencilState);

            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            I::ModelRender->ForcedMaterialOverride(nullptr);

            // FIX #2 (备用方案): 强制刷新渲染队列，确保状态变更被提交
            pRenderContext->Flush(true);

            // --- 阶段 2: 渲染传送门内的场景 ---
            pRenderContext->OverrideDepthEnable(false, true);

            stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
            stencilState.m_PassOp = STENCILOPERATION_KEEP;
            stencilState.m_FailOp = STENCILOPERATION_KEEP;
            stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
            pRenderContext->SetStencilState(stencilState);

            CViewSetup portalView = g_ViewSetup; // 创建一个副本进行修改
            portalView.angles.y += 180.0f; // 仅修改副本

            using RenderView_FN = void(__fastcall*)(void*, void*, CViewSetup&, CViewSetup&, int, int);
            auto oRenderView = BaseClient::RenderView::Func.Original<RenderView_FN>();

            if (g_pClient_this_ptr) {
                oRenderView(
                    g_pClient_this_ptr,
                    nullptr,
                    portalView, // 使用修改后的副本
                    g_hudViewSetup, // HUD视角通常不需要改
                    2, // 使用枚举代替魔法数字34
                    RENDERVIEW_DRAWVIEWMODEL
                );
            }

            // --- 阶段 3: 绘制传送门模型本身 (例如边框) ---
            stencilState.m_bEnable = false;
            pRenderContext->SetStencilState(stencilState);

            // ============================ FIX #3: 完成阶段3的绘制 ============================
            // 正常绘制模型本身，让边框显示出来
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            // ==============================================================================
        }

        g_bDrawingPortalView = false;
        // ============================ FIX #2: 添加 return 避免二次绘制 ============================
        return;
        // ==============================================================================
    }

    // 如果不是传送门模型，正常调用原始函数
    Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
}

void ModelRender::Init()
{
	XASSERT(Table.Init(I::ModelRender) == false);
	XASSERT(Table.Hook(&ForcedMaterialOverride::Detour, ForcedMaterialOverride::Index) == false);
	XASSERT(Table.Hook(&DrawModelExecute::Detour, DrawModelExecute::Index) == false);
}