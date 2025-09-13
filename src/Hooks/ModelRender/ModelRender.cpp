#include "ModelRender.h"
#include "../../Portal/L4D2_Portal.h"
using namespace Hooks;

// 全局标志，用于跟踪是否正在渲染Portal纹理
//static bool g_bIsRenderingPortalTexture = true;

void __fastcall ModelRender::ForcedMaterialOverride::Detour(void* ecx, void* edx, IMaterial* newMaterial, OverrideType_t nOverrideType)
{
	Table.Original<FN>(Index)(ecx, edx, newMaterial, nOverrideType);
}

void __fastcall ModelRender::DrawModelExecute::Detour(void* ecx, void* edx, const DrawModelState_t& state, const ModelRenderInfo_t& pInfo, matrix3x4_t* pCustomBoneToWorld)
{
	/* 20250913：以下代码尝试跳过对固定模型的渲染，目的是在RenderView阶段不参与绘制,解决类<传送门>纹理无限递归的问题，但始终找不到对应的模型名称
	// 检查是否正在渲染Portal纹理
	if (g_bIsRenderingPortalTexture && pInfo.pModel)
	{
		// 检查模型名称是否是特定的portal模型
		const char* modelName = I::ModelInfo->GetModelName(pInfo.pModel);
		//if (modelName && strstr(modelName, "models/zimu/zimu1_hd.mdl"))
		if (modelName && strcmp(modelName, "models/zimu/zimu1_hd.mdl") == 0)
		{
			// 直接跳过渲染特定的portal模型
			volatile int breakpoint_here = 0; // 这一行设置普通断点
			return;
		}

		// 获取模型材质
		IMaterial** materials = new IMaterial*[10];
		int materialCount = I::ModelInfo->GetModelMaterialCount(pInfo.pModel);
		// 限制最大材质数量为数组大小
		if (materialCount > 10) materialCount = 10;
		I::ModelInfo->GetModelMaterials(pInfo.pModel, materialCount, materials);
		
		// 检查材质是否与Portal纹理相关
		for (int i = 0; i < materialCount; i++)
		{
			if (!materials[i]) continue;

			// 检查材质名称
			const char* materialName = materials[i]->GetName();
			if (materialName && strstr(materialName, "models/zimu/zimu1_hd/zimu1_hd"))
			{
				delete[] materials;
				return;
			}

			// 检查材质的basetexture纹理名称
			bool found = false;
			IMaterialVar* baseTextureVar = materials[i]->FindVar("$basetexture", &found, false);
			if (found && baseTextureVar)
			{
				ITexture* baseTexture = baseTextureVar->GetTextureValue();
				if (baseTexture)
				{
					const char* textureName = baseTexture->GetName();
					if (textureName && strstr(textureName, "_rt_PortalTexture"))
					{
						delete[] materials;
						return;
					}
				}
			}
		}
		delete[] materials;
	}
	*/

	// 正常渲染
	Table.Original<FN>(Index)(ecx, edx, state, pInfo, pCustomBoneToWorld);
}

void ModelRender::Init()
{
	XASSERT(Table.Init(I::ModelRender) == false);
	XASSERT(Table.Hook(&ForcedMaterialOverride::Detour, ForcedMaterialOverride::Index) == false);
	XASSERT(Table.Hook(&DrawModelExecute::Detour, DrawModelExecute::Index) == false);
}