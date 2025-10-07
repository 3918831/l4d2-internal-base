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
    /*
    // 检查我们是否正在渲染传送门内部，并且引擎是否正试图应用一个辉光材质
    if (newMaterial) {
        const char* materialName = newMaterial->GetName();
        if (strstr(materialName, "glow") != nullptr ||
            strstr(materialName, "Glow") != nullptr) {
            printf("materialName: %s\n", materialName);
        }
    }

    if (g_bDrawingPortalView && newMaterial)
    {
        const char* materialName = newMaterial->GetName();
        printf("materialName: %s\n",materialName);
        // L4D2的辉光材质通常包含 "dev/glow"
        if (strstr(materialName, "dev/glow") != nullptr)
        {
            // 拦截此操作，直接返回，不调用原始函数
            // 这样辉光材质就永远不会被应用
            return;
        }
    }
    */

    // 对于所有其他情况，正常调用原始函数
	Table.Original<FN>(Index)(ecx, edx, newMaterial, nOverrideType);
}
#ifdef PLAN_A //PLAN_A: 指在DrawModelExecute中绘制模板，并紧接着反向调用原生的RenderView函数绘制场景，这种方案暂时无法规避传送门框永远在最前面的问题
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
        IMaterial* g_pWriteStencilMaterial = I::MaterialSystem->FindMaterial("dev/write_stencil", TEXTURE_GROUP_OTHER);

        if (pRenderContext && g_pWriteStencilMaterial)
        {
            // --- 阶段 1: 绘制模板遮罩 ---
            I::ModelRender->ForcedMaterialOverride(g_pWriteStencilMaterial);

            ShaderStencilState_t stencilState;
            stencilState.m_bEnable = true;
            stencilState.m_nReferenceValue = 1;
            stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS; // 总是尝试进行模板操作
            stencilState.m_PassOp = STENCILOPERATION_REPLACE;           // 如果深度测试通过，则写入模板
            stencilState.m_FailOp = STENCILOPERATION_KEEP;              // (这个不重要，因为测试总是ALWAYS)

            // ======================= [关键逻辑修正] =======================
            // 如果深度测试失败 (即被遮挡), 则保持模板缓冲原样，不做任何事
            stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
            // =============================================================

            pRenderContext->SetStencilState(stencilState);

            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            I::ModelRender->ForcedMaterialOverride(nullptr);

            // FIX #2 (备用方案): 强制刷新渲染队列，确保状态变更被提交
            //pRenderContext->Flush(true);

            // --- 阶段 2: 渲染传送门内的场景 ---
            pRenderContext->OverrideDepthEnable(false, true);

            stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
            stencilState.m_PassOp = STENCILOPERATION_KEEP;
            stencilState.m_FailOp = STENCILOPERATION_KEEP;
            stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
            pRenderContext->SetStencilState(stencilState);

            CViewSetup portalView = g_ViewSetup; // 创建一个副本进行修改
            portalView.angles.y += 180.0f; // 仅修改副本
            portalView.origin.x = 0.0f;
            portalView.origin.y = 0.0f;
            portalView.origin.z = 0.0f;

            using RenderView_FN = void(__fastcall*)(void*, void*, CViewSetup&, CViewSetup&, int, int);
            auto oRenderView = BaseClient::RenderView::Func.Original<RenderView_FN>();

            if (g_pClient_this_ptr) {
                oRenderView(
                    g_pClient_this_ptr,
                    nullptr,
                    portalView, // 使用修改后的副本
                    g_hudViewSetup, // HUD视角通常不需要改
                    2, // 使用枚举代替魔法数字34
                    0
                );
            }

            /*
            // ============================ 修正后的：阶段 2.5 - 模板清理 ============================
            IMaterial* pStencilClearMaterial = I::MaterialSystem->FindMaterial("dev/stencil_clear", TEXTURE_GROUP_OTHER);
            if (pStencilClearMaterial)
            {
                // 1. 设置模板状态，准备清空
                stencilState.m_bEnable = true;
                stencilState.m_nReferenceValue = 1; // 目标是之前我们标记的传送门区域
                stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL; // 只在传送门区域内操作
                stencilState.m_PassOp = STENCILOPERATION_ZERO; // 将通过测试的像素的模板值清零
                stencilState.m_FailOp = STENCILOPERATION_KEEP;
                stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
                pRenderContext->SetStencilState(stencilState);

                // 2. 我们的材质已经处理了颜色和深度写入，这里无需再调用API

                // 3. 绘制一个全屏四边形来执行清理操作
                pRenderContext->DrawScreenSpaceQuad(pStencilClearMaterial);

                // 4. 执行完毕，所有状态将在阶段3前被重置，这里无需额外操作
            }
            // ===================================================================================
            //*/

            // --- 阶段 3: 绘制传送门边框 (修正Z冲突) ---
            // 禁用模板测试，我们现在要正常绘制模型了
            stencilState.m_bEnable = false;
            pRenderContext->SetStencilState(stencilState);


            //* ##################### --- 3.0 方案,至少可行,但是门框透视有问题
            // ============================ FIX #3: 完成阶段3的绘制 ============================

            // ============================ [关键修复] ============================
            // 保持深度测试(默认开启)，但禁用深度写入，以防止与门内场景发生Z冲突
            // 同时，由于深度测试是开启的，近处的物体(如队友)依然可以正确遮挡门框

            pRenderContext->OverrideDepthEnable(true, false);
            //// 正常绘制模型本身，让边框显示出来
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);

            //// [!!非常重要!!] 立即恢复深度写入，以免影响后续所有模型的正常渲染
            pRenderContext->OverrideDepthEnable(false, true);            
            // ==============================================================================
            //*/

            /* ##################### ---  3.1 尝试修正传送门透视问题
            // ======================= [关键代码修正] =======================
            // 将材质路径修改为 Crowbar 和 VTFEdit 确认过的正确路径
            // 路径是相对于 materials/ 文件夹的，所以 "materials/sprites/blborder.vmt" 对应 "sprites/blborder"
            IMaterial* pPortalFrameMaterial = I::MaterialSystem->FindMaterial("sprites/blborder", TEXTURE_GROUP_OTHER);
            // =============================================================

            if (pPortalFrameMaterial)
            {
                I::ModelRender->ForcedMaterialOverride(pPortalFrameMaterial);
                Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
                I::ModelRender->ForcedMaterialOverride(nullptr);
            }
            else
            {
                Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            }
            //*/
        }

        g_bDrawingPortalView = false;
        // ============================ FIX #2: 添加 return 避免二次绘制 ============================
        return;
        // ==============================================================================
    }

    // 如果不是传送门模型，正常调用原始函数
    Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
}
#endif

#ifdef PLAN_B
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
        IMaterial* g_pWriteStencilMaterial = I::MaterialSystem->FindMaterial("dev/write_stencil", TEXTURE_GROUP_OTHER);

        if (pRenderContext && g_pWriteStencilMaterial)
        {
            // --- 阶段 1: 绘制模板遮罩和深度 (完全遵循深度规则) ---
            // (这部分代码与我们之前修正好的版本完全一致)
            pRenderContext->OverrideDepthEnable(false, true); // 确保深度写入开启
            I::ModelRender->ForcedMaterialOverride(g_pWriteStencilMaterial);
            ShaderStencilState_t stencilState;
            stencilState.m_bEnable = true;
            stencilState.m_nReferenceValue = 1;
            stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
            stencilState.m_PassOp = STENCILOPERATION_REPLACE;
            stencilState.m_ZFailOp = STENCILOPERATION_KEEP; // 关键：被遮挡时不写入
            stencilState.m_FailOp = STENCILOPERATION_KEEP;
            pRenderContext->SetStencilState(stencilState);
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            I::ModelRender->ForcedMaterialOverride(nullptr);

            // --- 阶段 2: 将RTT纹理绘制在模板区域内 ---
            if (G::G_L4D2Portal.m_pPortalMaterial && G::G_L4D2Portal.m_pPortalTexture)
            {
                // 设置模板，只在值为1的区域绘制
                stencilState.m_bEnable = true;
                stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
                stencilState.m_PassOp = STENCILOPERATION_KEEP;
                stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
                pRenderContext->SetStencilState(stencilState);

                // 不需要深度，因为我们是画在2D屏幕上
                pRenderContext->OverrideDepthEnable(true, false);

                pRenderContext->DrawScreenSpaceQuad(G::G_L4D2Portal.m_pPortalMaterial);

                // 恢复深度
                pRenderContext->OverrideDepthEnable(false, true);
            }

            // --- 阶段 3: 使用“深度相等”材质绘制边框 ---
            // (这部分代码与我们之前修正好的版本完全一致)
            stencilState.m_bEnable = false;
            pRenderContext->SetStencilState(stencilState);
            IMaterial* pPortalFrameMaterial = I::MaterialSystem->FindMaterial("sprites/blborder", TEXTURE_GROUP_OTHER);
            if (pPortalFrameMaterial)
            {
                I::ModelRender->ForcedMaterialOverride(pPortalFrameMaterial);
                Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
                I::ModelRender->ForcedMaterialOverride(nullptr);
            }
        }
        g_bDrawingPortalView = false;
        // ============================ FIX #2: 添加 return 避免二次绘制 ============================
        return;
        // ==============================================================================
    }

    // 如果不是传送门模型，正常调用原始函数
    Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
}
#endif


void ModelRender::Init()
{
	XASSERT(Table.Init(I::ModelRender) == false);
	XASSERT(Table.Hook(&ForcedMaterialOverride::Detour, ForcedMaterialOverride::Index) == false);
	XASSERT(Table.Hook(&DrawModelExecute::Detour, DrawModelExecute::Index) == false);
}