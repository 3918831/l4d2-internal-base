#pragma once

#include <stdint.h>
#include "../Util/Math/Vector/Vector.h"
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
#define _MAX_SIGS_LEN_ 128

//Render:
constexpr const char Sigs_RenderInstance[_MAX_SIGS_LEN_] = { "8B 0D ? ? ? ? 8B 01 8B 96 D8 00 00 00" };//V
constexpr const char Sigs_FnGetVisibleFogVolume[_MAX_SIGS_LEN_] = { "55 8B EC 83 EC 30 A1 ? ? ? ? C7 45 F4" };//v

constexpr const char Sigs_FnDrawWorldAndEntities[_MAX_SIGS_LEN_] = { "57 57 57 57 8D 45 DC 50 51 8B 49 50 FF D1" };//V
constexpr const char Sigs_FnDetermineWaterRenderInfo[_MAX_SIGS_LEN_] = { "55 8B EC 83 EC 1C A1 ? ? ? ? 53 33 DB 89 5D F4" };//V

constexpr const char Sigs_TerrorViewRenderInstance[_MAX_SIGS_LEN_] = { "8B 0D ? ? ? ? D9 05 ? ? ? ? 8B 01 8B 50 7C 51" };//V

class CViewSetup;
class ITerrorViewRender;

class WaterRenderInfo_t
{
	bool m_bCheapWater : 1;
	bool m_bReflect : 1;
	bool m_bRefract : 1;
	bool m_bReflectEntities : 1;
	bool m_bReflectOnlyMarkedEntities : 1;
	bool m_bDrawWaterSurface : 1;
	bool m_bOpaqueWater : 1;
	bool m_bPseudoTranslucentWater : 1;
	bool m_bReflect2DSkybox : 1;
	char pad[8];
};
class VisibleFogVolumeInfo_t
{
	int		m_nVisibleFogVolume;
	int		m_nVisibleFogVolumeLeaf;
	bool	m_bEyeInFogVolume;
	float	m_flDistanceToWater;
	float	m_flWaterHeight;
	IMaterial* m_pFogVolumeMaterial;
	char pad[8];
};

using VPlane = cplane_t;

typedef void(__cdecl* FnGetVisibleFogVolume)(Vector&, VisibleFogVolumeInfo_t&);
typedef void(__thiscall* FnDrawWorldAndEntities)(ITerrorViewRender*, bool, CViewSetup&, int, VisibleFogVolumeInfo_t*, WaterRenderInfo_t*, void*);
typedef char(__stdcall* FnDetermineWaterRenderInfo)(VisibleFogVolumeInfo_t*, WaterRenderInfo_t*);

class IRender
{
public:
	void Push3DView(CViewSetup& setup, int nFlags, ITexture* rt, VPlane* vp, ITexture* dt)
	{
		using FN = void(__thiscall*)(IRender*, CViewSetup&, int, ITexture*, VPlane*, ITexture*);
		return (*(FN**)this)[40](this, setup, nFlags, rt, vp, dt);
	}
	void PopView(VPlane* vp)
	{
		using FN = void(__thiscall*)(IRender*, VPlane*);
		return (*(FN**)this)[43](this, vp);
	}
	inline void GetVisibleFogVolumeInfo(Vector& vec, VisibleFogVolumeInfo_t& fog)
	{
		return FnGetVisibleFogVolume_func(vec, fog);
	}
	void Init()
	{
		FnGetVisibleFogVolume_func = (FnGetVisibleFogVolume)U::Pattern.Find("engine.dll", Sigs_FnGetVisibleFogVolume);
		if (FnGetVisibleFogVolume_func) {
			printf("[IRender]FnGetVisibleFogVolume_func: %p\n", FnGetVisibleFogVolume_func);
		}
	}
	inline static FnGetVisibleFogVolume FnGetVisibleFogVolume_func = 0x0;
};

class ITerrorViewRender
{
public:
	inline VPlane* GetFrustum()
	{
		return reinterpret_cast<VPlane*>(this + 216);
	}
	inline void	DrawWorldAndEntities(bool drawSkybox, CViewSetup& view, int nClearFlags, VisibleFogVolumeInfo_t* fog, WaterRenderInfo_t* water, void* pCustomVisibility = nullptr)
	{
		return FnDrawWorldAndEntities_func_new(this, drawSkybox, view, nClearFlags, fog, water, pCustomVisibility);
	}
	inline char DetermineWaterRenderInfo(VisibleFogVolumeInfo_t* fog, WaterRenderInfo_t* water)
	{
		return FnDetermineWaterRenderInfo_func(fog, water);
	}
	void Init()
	{
		DWORD ABC = U::Pattern.Find("client.dll", Sigs_FnDrawWorldAndEntities) - 0xC0;
		FnDrawWorldAndEntities_func_new = (FnDrawWorldAndEntities)ABC;
		if (FnDrawWorldAndEntities_func_new) {
			printf("[ITerrorViewRender]FnDrawWorldAndEntities_func_new: %p\n", FnDrawWorldAndEntities_func_new);
		}
		FnDetermineWaterRenderInfo_func = (FnDetermineWaterRenderInfo)U::Pattern.Find("client.dll", Sigs_FnDetermineWaterRenderInfo);
		if (FnDetermineWaterRenderInfo_func) {
			printf("[ITerrorViewRender]FnDetermineWaterRenderInfo_func: %p\n", FnDetermineWaterRenderInfo_func);
		}
	}
private:

	inline static FnDetermineWaterRenderInfo FnDetermineWaterRenderInfo_func = 0x0;
	inline static FnDrawWorldAndEntities FnDrawWorldAndEntities_func_new = 0x0;
};

namespace I { inline IRender* CustomRender = nullptr; }
namespace I { inline ITerrorViewRender* CustomView = nullptr; }