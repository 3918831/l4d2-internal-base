#include "ModelRender.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Hooks/BaseClient/BaseClient.h"
#include "../Hooks.h"
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

#ifdef RECURSIVE_RENDERING
void __fastcall ModelRender::DrawModelExecute::Detour(void* ecx, void* edx, const DrawModelState_t& state, const ModelRenderInfo_t& pInfo, matrix3x4_t* pCustomBoneToWorld)
{
    // 1. 递归保护：防止画自己
    if (G::G_L4D2Portal.m_nPortalRenderDepth > 0) {
        // RenderPortalViewRecursive 在进入时设置它，退出时清除它。
        PortalInfo_t* currentExitPortal = G::G_L4D2Portal.m_pCurrentExitPortal;

        // 如果正在绘制的模型的原点，和当前视图的出口原点非常接近，
        // 就认为它们是同一个门。
        if (currentExitPortal && pInfo.origin.DistTo(currentExitPortal->origin) < 5.0f)
        {
            // 我们正在尝试绘制我们“摄像机”所在的那个门。
            // 在这里我们不应该再发起递归渲染，而是应该直接跳过，
            // 或者用一个纯色材质把它画出来，防止无限递归。
            // 最简单的做法是直接调用原始函数，让它被正常（无效果地）绘制。
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }
    }

    // 2. 模型过滤
    const char* modelName = I::ModelInfo->GetModelName(pInfo.pModel);
    // 性能优化：先做简单的字符串首字母检查，再做 strcmp
    if (!modelName || modelName[0] != 'm') {
        Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
        return;
    }
    bool isBluePortal = (modelName && strcmp(modelName, "models/blackops/portal.mdl") == 0);
    bool isOrangePortal = (modelName && strcmp(modelName, "models/blackops/portal_og.mdl") == 0);

    if (isBluePortal) {
        // 【安全检查】检查传送门系统是否已初始化
        if (!G::G_L4D2Portal.m_pMaterialSystem) {
            printf("[ModelRender] Blue portal: MaterialSystem not initialized, skipping animation.\n");
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }

        // 【安全检查】检查 EngineClient 是否可用（用于时间函数）
        if (!I::EngineClient) {
            printf("[ModelRender] Blue portal: EngineClient is nullptr, skipping animation.\n");
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }

        // 检测位置是否发生变化（用于触发缩放动画）
        float flDistToLast = pInfo.origin.DistTo(G::G_L4D2Portal.g_BluePortal.lastOrigin);

        // 如果位置变化超过阈值（传送门被移动），重置缩放动画
        if (flDistToLast > 1.0f) {
            G::G_L4D2Portal.g_BluePortal.isAnimating = true;
            G::G_L4D2Portal.g_BluePortal.currentScale = 0.0f;
            G::G_L4D2Portal.g_BluePortal.lastTime = I::EngineClient->OBSOLETE_Time();
        }

        // 更新缩放动画
        if (G::G_L4D2Portal.g_BluePortal.isAnimating) {
            float flCurrentTime = I::EngineClient->OBSOLETE_Time();
            float flFrameTime = flCurrentTime - G::G_L4D2Portal.g_BluePortal.lastTime;
            G::G_L4D2Portal.g_BluePortal.lastTime = flCurrentTime;

            G::G_L4D2Portal.g_BluePortal.currentScale += G::G_L4D2Portal.g_BluePortal.SCALE_SPEED * flFrameTime;

            // 达到目标缩放比例，停止动画
            if (G::G_L4D2Portal.g_BluePortal.currentScale >= 1.0f) {
                G::G_L4D2Portal.g_BluePortal.currentScale = 1.0f;
                G::G_L4D2Portal.g_BluePortal.isAnimating = false;
            }
        }

        // 保存当前位置作为下一次比较的基准
        G::G_L4D2Portal.g_BluePortal.lastOrigin = pInfo.origin;
        G::G_L4D2Portal.g_BluePortal.origin = pInfo.origin;
        G::G_L4D2Portal.g_BluePortal.angles.x = pInfo.angles.x;
        G::G_L4D2Portal.g_BluePortal.angles.y = pInfo.angles.y;
        G::G_L4D2Portal.g_BluePortal.angles.z = pInfo.angles.z;
        G::G_L4D2Portal.g_BluePortal.bIsActive = true;

        // 应用缩放到模型
        int model_index = pInfo.entity_index;
        C_BaseAnimating* pEntity = reinterpret_cast<C_BaseAnimating*>(I::ClientEntityList->GetClientEntity(model_index));
        if (pEntity) {
            float* pScale = (float*)((uintptr_t)pEntity + 0x728); // 0x728 是C_BaseAnimating的m_flModelScale值(client.dll)
            if (pScale) {
                *pScale = G::G_L4D2Portal.g_BluePortal.currentScale;
            }
        }
    }

    if (isOrangePortal) {
        // 【安全检查】检查传送门系统是否已初始化
        if (!G::G_L4D2Portal.m_pMaterialSystem) {
            printf("[ModelRender] Orange portal: MaterialSystem not initialized, skipping animation.\n");
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }

        // 【安全检查】检查 EngineClient 是否可用（用于时间函数）
        if (!I::EngineClient) {
            printf("[ModelRender] Orange portal: EngineClient is nullptr, skipping animation.\n");
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }

        // 检测位置是否发生变化（用于触发缩放动画）
        float flDistToLast = pInfo.origin.DistTo(G::G_L4D2Portal.g_OrangePortal.lastOrigin);

        // 如果位置变化超过阈值（传送门被移动），重置缩放动画
        if (flDistToLast > 1.0f) {
            G::G_L4D2Portal.g_OrangePortal.isAnimating = true;
            G::G_L4D2Portal.g_OrangePortal.currentScale = 0.0f;
            G::G_L4D2Portal.g_OrangePortal.lastTime = I::EngineClient->OBSOLETE_Time();
        }

        // 更新缩放动画
        if (G::G_L4D2Portal.g_OrangePortal.isAnimating) {
            float flCurrentTime = I::EngineClient->OBSOLETE_Time();
            float flFrameTime = flCurrentTime - G::G_L4D2Portal.g_OrangePortal.lastTime;
            G::G_L4D2Portal.g_OrangePortal.lastTime = flCurrentTime;

            G::G_L4D2Portal.g_OrangePortal.currentScale += G::G_L4D2Portal.g_OrangePortal.SCALE_SPEED * flFrameTime;

            // 达到目标缩放比例，停止动画
            if (G::G_L4D2Portal.g_OrangePortal.currentScale >= 1.0f) {
                G::G_L4D2Portal.g_OrangePortal.currentScale = 1.0f;
                G::G_L4D2Portal.g_OrangePortal.isAnimating = false;
            }
        }

        // 保存当前位置作为下一次比较的基准
        G::G_L4D2Portal.g_OrangePortal.lastOrigin = pInfo.origin;
        G::G_L4D2Portal.g_OrangePortal.origin = pInfo.origin;
        G::G_L4D2Portal.g_OrangePortal.angles.x = pInfo.angles.x;
        G::G_L4D2Portal.g_OrangePortal.angles.y = pInfo.angles.y;
        G::G_L4D2Portal.g_OrangePortal.angles.z = pInfo.angles.z;
        G::G_L4D2Portal.g_OrangePortal.bIsActive = true;

        // 应用缩放到模型
        int model_index = pInfo.entity_index;
        C_BaseAnimating* pEntity = reinterpret_cast<C_BaseAnimating*>(I::ClientEntityList->GetClientEntity(model_index));
        if (pEntity) {
            float* pScale = (float*)((uintptr_t)pEntity + 0x728); // 0x728 是C_BaseAnimating的m_flModelScale值(client.dll)
            if (pScale) {
                *pScale = G::G_L4D2Portal.g_OrangePortal.currentScale;
            }
        }
    }

    if (isBluePortal || isOrangePortal) {
        // 【安全检查】检查传送门系统是否已初始化
        if (!G::G_L4D2Portal.m_pMaterialSystem) {
            printf("[ModelRender] Portal system not initialized, calling original function.\n");
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }

        // 0. 决定入口和出口
        PortalInfo_t* entryPortal = isBluePortal ? &G::G_L4D2Portal.g_BluePortal : &G::G_L4D2Portal.g_OrangePortal;
        PortalInfo_t* exitPortal = isBluePortal ? &G::G_L4D2Portal.g_OrangePortal : &G::G_L4D2Portal.g_BluePortal;

        // 如果只有一扇门激活，直接画模型返回
        if (!G::G_L4D2Portal.g_BluePortal.bIsActive || !G::G_L4D2Portal.g_BluePortal.bIsActive || G::G_L4D2Portal.m_vViewStack.empty()) {
            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            return;
        }

        // 获取当前的 CViewSetup, 你需要一种方式来获取它，通常可以从 I::RenderView 中获取
        const CViewSetup& currentView = G::G_L4D2Portal.m_vViewStack.back();

        // 【发起递归渲染】
        // 这里会调用 RenderPortalViewRecursive -> DrawWorldAndEntities -> DrawModelExecute (下一层)
        bool bRenderSuccess = G::G_L4D2Portal.RenderPortalViewRecursive(currentView, entryPortal, exitPortal);

        // 发起递归渲染
        if (bRenderSuccess) {

            // 2. 动态绑定纹理到材质
            if (!G::G_L4D2Portal.m_pDynamicPortalMaterial) {
                printf("[DrawModelExecute] G::G_L4D2Portal::m_pDynamicPortalMaterial is nullptr\n");
                return;
            }

            // 【安全检查】检查数组索引是否有效
            if (G::G_L4D2Portal.m_nPortalRenderDepth < 0 ||
                G::G_L4D2Portal.m_nPortalRenderDepth >= static_cast<int>(G::G_L4D2Portal.m_vPortalTextures.size())) {
                printf("[DrawModelExecute] ERROR: m_nPortalRenderDepth=%d out of bounds [0, %zu)! Skipping texture bind.\n",
                       G::G_L4D2Portal.m_nPortalRenderDepth, G::G_L4D2Portal.m_vPortalTextures.size());
                return;
            }

            // 动态绑定刚刚渲染好的纹理
            IMaterialVar* pBaseTextureVar = G::G_L4D2Portal.m_pDynamicPortalMaterial->FindVar("$basetexture", nullptr);
            if (pBaseTextureVar) {
                // 将刚刚渲染好的纹理设置给材质
                ITexture* pTexture = G::G_L4D2Portal.m_vPortalTextures[G::G_L4D2Portal.m_nPortalRenderDepth];
                if (pTexture) {
                    pBaseTextureVar->SetTextureValue(pTexture);
                } else {
                    printf("[DrawModelExecute] WARNING: m_vPortalTextures[%d] is nullptr!\n", G::G_L4D2Portal.m_nPortalRenderDepth);
                }
            }

            IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
            if (pRenderContext && G::G_L4D2Portal.m_pWriteStencilMaterial) {
                // 根据当前是哪个传送门，选择对应的材质
                IMaterial* pPortalFrameMaterial = isBluePortal
                    ? I::MaterialSystem->FindMaterial("sprites/blborder", TEXTURE_GROUP_OTHER)
                    : I::MaterialSystem->FindMaterial("sprites/ogborder", TEXTURE_GROUP_OTHER);

                // 【修复 A】为不同的传送门分配不同的模板ID
                int stencilRefValue = isBluePortal ? 1 : 2;

                // --- 阶段 1: 绘制模板遮罩 ---
                // (这段模板测试逻辑本身是完美的，我们保持原样)
                pRenderContext->OverrideDepthEnable(false, true);
                I::ModelRender->ForcedMaterialOverride(G::G_L4D2Portal.m_pWriteStencilMaterial);

                ShaderStencilState_t stencilState;
                stencilState.m_bEnable = true;
                stencilState.m_nReferenceValue = stencilRefValue; // 使用我们分配的唯一ID
                stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
                stencilState.m_PassOp = STENCILOPERATION_REPLACE;
                stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
                stencilState.m_FailOp = STENCILOPERATION_KEEP;
                pRenderContext->SetStencilState(stencilState);

                Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
                I::ModelRender->ForcedMaterialOverride(nullptr);

                // --- 阶段 2: 将RTT纹理绘制在模板区域内 ---
                if (G::G_L4D2Portal.m_pDynamicPortalMaterial)
                {
                    stencilState.m_bEnable = true;
                    stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
                    stencilState.m_PassOp = STENCILOPERATION_KEEP;
                    pRenderContext->SetStencilState(stencilState);

                    pRenderContext->OverrideDepthEnable(true, false);
                    pRenderContext->DrawScreenSpaceQuad(G::G_L4D2Portal.m_pDynamicPortalMaterial);
                    pRenderContext->OverrideDepthEnable(false, true);
                }

                // --- 阶段 3: 绘制边框 ---
                // 绘制边框前禁用模板测试，以免互相影响
                stencilState.m_bEnable = false;
                pRenderContext->SetStencilState(stencilState);

                if (pPortalFrameMaterial)
                {

                    // 复制当前的变换矩阵
                    matrix3x4_t modifiedMatrix = *pCustomBoneToWorld;
                    // 从角度计算出法线向量 (通常是 'forward' 前向向量)
                    Vector forward, right, up;
                    U::Math.AngleVectors(pInfo.angles, &forward, &right, &up);

                    // 获取当前位置
                    Vector position;
                    U::Math.MatrixGetColumn(*pCustomBoneToWorld, 3, position);

                    // 将位置沿着法线方向稍微向前推一点
                    position += forward * 0.1f; // 0.1f 是一个需要微调的小距离,如果仍然有z-fighting的问题,尝试调大这个值

                    // 将新的位置设置回矩阵
                    U::Math.MatrixSetColumn(position, 3, modifiedMatrix);

                    I::ModelRender->ForcedMaterialOverride(pPortalFrameMaterial);
                    Table.Original<FN>(Index)(ecx, edx, state, pInfo, &modifiedMatrix);
                    I::ModelRender->ForcedMaterialOverride(nullptr);
                }
            }
        } else {
            
            // 这是最深的一层，我们没有为它生成纹理。
            // 此时我们应该用一个“终止”材质来绘制它，而不是用动态材质。
            // 这里我们用边框材质来模拟一个有颜色的平面。
            // --- 渲染失败路径 (达到递归上限) ---
               // 【修正】使用我们专用的黑色材质来绘制最深处的门
            if (G::G_L4D2Portal.m_pBlackoutMaterial)
            {
                I::ModelRender->ForcedMaterialOverride(G::G_L4D2Portal.m_pBlackoutMaterial);
                Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
                I::ModelRender->ForcedMaterialOverride(nullptr);
            }            
        }
        return; // 处理完传送门后必须返回，防止默认绘制
    }

    // 如果不是传送门模型，正常调用原始函数
    Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
}
#endif

#ifdef RECURSIVE_RENDERING_0
void __fastcall ModelRender::DrawModelExecute::Detour(void* ecx, void* edx, const DrawModelState_t& state, const ModelRenderInfo_t& pInfo, matrix3x4_t* pCustomBoneToWorld)
{
    const char* modelName = I::ModelInfo->GetModelName(pInfo.pModel);
    // 性能优化：先做简单的字符串首字母检查，再做 strcmp
    if (!modelName || modelName[0] != 'm') {
        Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
        return;
    }
    bool isBluePortal = (modelName && strcmp(modelName, "models/blackops/portal.mdl") == 0);
    bool isOrangePortal = (modelName && strcmp(modelName, "models/blackops/portal_og.mdl") == 0);

    // 如果当前是在渲染传送门纹理过程中（Depth > 0），则不处理门，防止干扰
    // 方案 B 依赖反馈，不需要在渲染纹理时再进行模板测试
    if (!isBluePortal && !isOrangePortal) {
        Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
        return;
    }

    if (isBluePortal || isOrangePortal)
    {
        IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
        if (pRenderContext)
        {
            // 【修复 A】为不同的传送门分配不同的模板ID
            int stencilRefValue = isBluePortal ? 1 : 2;
            // 根据当前是哪个传送门，选择对应的材质
            IMaterial* pPortalFrameMaterial = isBluePortal
                ? I::MaterialSystem->FindMaterial("sprites/blborder", TEXTURE_GROUP_OTHER)
                : I::MaterialSystem->FindMaterial("sprites/ogborder", TEXTURE_GROUP_OTHER);
            
            // 阶段 1: 写入模板
            // (这段模板测试逻辑本身是完美的，我们保持原样)
            pRenderContext->OverrideDepthEnable(false, true);
            I::ModelRender->ForcedMaterialOverride(G::G_L4D2Portal.m_pWriteStencilMaterial);

            ShaderStencilState_t stencilState;
            stencilState.m_bEnable = true;
            stencilState.m_nReferenceValue = stencilRefValue; // 使用我们分配的唯一ID
            stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
            stencilState.m_PassOp = STENCILOPERATION_REPLACE;
            stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
            stencilState.m_FailOp = STENCILOPERATION_KEEP;
            pRenderContext->SetStencilState(stencilState);

            Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
            I::ModelRender->ForcedMaterialOverride(nullptr);

            // 阶段 2: 绘制预渲染好的纹理 (放映机逻辑)
            // 根据是蓝门还是橙门，绑定正确的 RT
            ITexture* pTex = isBluePortal ? G::G_L4D2Portal.m_pPortalTexture_Blue : G::G_L4D2Portal.m_pPortalTexture_Orange;
            IMaterialVar* pBaseTextureVar = G::G_L4D2Portal.m_pDynamicPortalMaterial->FindVar("$basetexture", nullptr);
            if (pBaseTextureVar && pTex) {
                stencilState.m_bEnable = true;
                stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
                stencilState.m_PassOp = STENCILOPERATION_KEEP;
                pRenderContext->SetStencilState(stencilState);

                pBaseTextureVar->SetTextureValue(pTex);
                pRenderContext->OverrideDepthEnable(true, false);
                pRenderContext->DrawScreenSpaceQuad(G::G_L4D2Portal.m_pDynamicPortalMaterial);
                pRenderContext->OverrideDepthEnable(false, true);
            }

            // 阶段 3: 绘制边框 (使用之前的偏移 0.1 方案)
            // 绘制边框前禁用模板测试，以免互相影响
            stencilState.m_bEnable = false;
            pRenderContext->SetStencilState(stencilState);

            if (pPortalFrameMaterial)
            {

                // 复制当前的变换矩阵
                matrix3x4_t modifiedMatrix = *pCustomBoneToWorld;
                // 从角度计算出法线向量 (通常是 'forward' 前向向量)
                Vector forward, right, up;
                U::Math.AngleVectors(pInfo.angles, &forward, &right, &up);

                // 获取当前位置
                Vector position;
                U::Math.MatrixGetColumn(*pCustomBoneToWorld, 3, position);

                // 将位置沿着法线方向稍微向前推一点
                position += forward * 0.1f; // 0.1f 是一个需要微调的小距离,如果仍然有z-fighting的问题,尝试调大这个值

                // 将新的位置设置回矩阵
                U::Math.MatrixSetColumn(position, 3, modifiedMatrix);

                I::ModelRender->ForcedMaterialOverride(pPortalFrameMaterial);
                Table.Original<FN>(Index)(ecx, edx, state, pInfo, &modifiedMatrix);
                I::ModelRender->ForcedMaterialOverride(nullptr);
            }
        }
        return;
    }
    Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
}
#endif

void ModelRender::Init()
{
	XASSERT(Table.Init(I::ModelRender) == false);
	XASSERT(Table.Hook(&ForcedMaterialOverride::Detour, ForcedMaterialOverride::Index) == false);
	XASSERT(Table.Hook(&DrawModelExecute::Detour, DrawModelExecute::Index) == false);
}