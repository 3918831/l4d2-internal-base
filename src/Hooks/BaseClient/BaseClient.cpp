#include <memory>
#include <cassert>
#include "BaseClient.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../SDK/L4D2/Interfaces/RenderView.h"
#include "../../SDK/L4D2/Interfaces/EngineClient.h"
#include "../../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../../SDK/L4D2/Interfaces/ModelRender.h"
#include "../../SDK/L4D2/Includes/const.h"
//#include "../../Portal/public/mathlib.h"

//CViewSetup g_ViewSetup;
//CViewSetup g_hudViewSetup;
void* g_pClient_this_ptr = nullptr; // 用于存储 RenderView 的真实 this 指针

#ifndef RAD2DEG
#define RAD2DEG( x  )  ( (float)(x) * (float)(180.f / M_PI_F) )
#endif

#ifndef DEG2RAD
#define DEG2RAD( x  )  ( (float)(x) * (float)(M_PI_F / 180.f) )
#endif

enum
{
	PITCH = 0,	// up / down
	YAW,		// left / right
	ROLL		// fall over
};

inline float FloatMakePositive(vec_t f)
{
	return (float)fabs(f);
}

bool MatrixInverseGeneral(const VMatrix& src, VMatrix& dst)
{
	int iRow, i, j, iTemp, iTest;
	vec_t mul, fTest, fLargest;
	vec_t mat[4][8];
	int rowMap[4], iLargest;
	vec_t* pOut, * pRow, * pScaleRow;


	// How it's done.
	// AX = I
	// A = this
	// X = the matrix we're looking for
	// I = identity

	// Setup AI
	for (i = 0; i < 4; i++)
	{
		const vec_t* pIn = src[i];
		pOut = mat[i];

		for (j = 0; j < 4; j++)
		{
			pOut[j] = pIn[j];
		}

		pOut[4] = 0.0f;
		pOut[5] = 0.0f;
		pOut[6] = 0.0f;
		pOut[7] = 0.0f;
		pOut[i + 4] = 1.0f;

		rowMap[i] = i;
	}

	// Use row operations to get to reduced row-echelon form using these rules:
	// 1. Multiply or divide a row by a nonzero number.
	// 2. Add a multiple of one row to another.
	// 3. Interchange two rows.

	for (iRow = 0; iRow < 4; iRow++)
	{
		// Find the row with the largest element in this column.
		fLargest = 0.001f;
		iLargest = -1;
		for (iTest = iRow; iTest < 4; iTest++)
		{
			fTest = (vec_t)FloatMakePositive(mat[rowMap[iTest]][iRow]);
			if (fTest > fLargest)
			{
				iLargest = iTest;
				fLargest = fTest;
			}
		}

		// They're all too small.. sorry.
		if (iLargest == -1)
		{
			return false;
		}

		// Swap the rows.
		iTemp = rowMap[iLargest];
		rowMap[iLargest] = rowMap[iRow];
		rowMap[iRow] = iTemp;

		pRow = mat[rowMap[iRow]];

		// Divide this row by the element.
		mul = 1.0f / pRow[iRow];
		for (j = 0; j < 8; j++)
			pRow[j] *= mul;

		pRow[iRow] = 1.0f; // Preserve accuracy...

		// Eliminate this element from the other rows using operation 2.
		for (i = 0; i < 4; i++)
		{
			if (i == iRow)
				continue;

			pScaleRow = mat[rowMap[i]];

			// Multiply this row by -(iRow*the element).
			mul = -pScaleRow[iRow];
			for (j = 0; j < 8; j++)
			{
				pScaleRow[j] += pRow[j] * mul;
			}

			pScaleRow[iRow] = 0.0f; // Preserve accuracy...
		}
	}

	// The inverse is on the right side of AX now (the identity is on the left).
	for (i = 0; i < 4; i++)
	{
		const vec_t* pIn = mat[rowMap[i]] + 4;
		pOut = dst.m[i];

		for (j = 0; j < 4; j++)
		{
			pOut[j] = pIn[j];
		}
	}

	return true;
}

// 这是一个我们自己实现的 "包装" 函数，提供了我们需要的签名
bool MatrixInverse(const matrix3x4_t& src, matrix3x4_t& dst)
{
	// 1. 将 3x4 矩阵转换为 4x4 的 VMatrix
	VMatrix src_4x4;
	src_4x4.Init(src); // VMatrix 类通常有一个从 matrix3x4_t 初始化的构造函数或方法

	// 如果没有 Init 方法，可以手动转换：
	 //for (int i = 0; i < 3; i++) {
	 //    for (int j = 0; j < 4; j++) {
	 //        src_4x4.m[i][j] = src[i][j];
	 //    }
	 //}
	 //src_4x4.m[3][0] = 0.0f;
	 //src_4x4.m[3][1] = 0.0f;
	 //src_4x4.m[3][2] = 0.0f;
	 //src_4x4.m[3][3] = 1.0f;

	// 2. 调用 SDK 提供的通用 4x4 矩阵求逆函数
	VMatrix dst_4x4;
	if (!MatrixInverseGeneral(src_4x4, dst_4x4))
	{
		return false; // 如果求逆失败，直接返回
	}

	// 3. 将结果 4x4 矩阵转换回 3x4 矩阵
	//    VMatrix 类也应该有一个方法可以直接转换
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++) {
			dst[i][j] = dst_4x4.m[i][j];
		}
	}

	return true;
}

void MatrixGetColumn(const matrix3x4_t& in, int column, Vector& out)
{
	out.x = in[0][column];
	out.y = in[1][column];
	out.z = in[2][column];
}

void MatrixAngles(const matrix3x4_t& matrix, float* angles)
{
	float forward[3];
	float left[3];
	float up[3];

	//
	// Extract the basis vectors from the matrix. Since we only need the Z
	// component of the up vector, we don't get X and Y.
	//
	forward[0] = matrix[0][0];
	forward[1] = matrix[1][0];
	forward[2] = matrix[2][0];
	left[0] = matrix[0][1];
	left[1] = matrix[1][1];
	left[2] = matrix[2][1];
	up[2] = matrix[2][2];

	float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

	// enough here to get angles?
	if (xyDist > 0.001f)
	{
		// (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
		angles[1] = RAD2DEG(atan2f(forward[1], forward[0]));

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		// (roll)	z = ATAN( left.z, up.z );
		angles[2] = RAD2DEG(atan2f(left[2], up[2]));
	}
	else	// forward is mostly Z, gimbal lock-
	{
		// (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
		angles[1] = RAD2DEG(atan2f(-left[0], left[1]));

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		// Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
		angles[2] = 0;
	}
}

inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles)
{
	MatrixAngles(matrix, &angles.x);
}

void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
	memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}

void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
	if (&in1 == &out)
	{
		matrix3x4_t in1b;
		MatrixCopy(in1, in1b);
		ConcatTransforms(in1b, in2, out);
		return;
	}
	if (&in2 == &out)
	{
		matrix3x4_t in2b;
		MatrixCopy(in2, in2b);
		ConcatTransforms(in1, in2b, out);
		return;
	}
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
		in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
		in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
		in1[0][2] * in2[2][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
		in1[0][2] * in2[2][3] + in1[0][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
		in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
		in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
		in1[1][2] * in2[2][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
		in1[1][2] * in2[2][3] + in1[1][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
		in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
		in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
		in1[2][2] * in2[2][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
		in1[2][2] * in2[2][3] + in1[2][3];
}

void inline SinCos(float radians, float* sine, float* cosine)
{
	_asm
	{
		fld		DWORD PTR[radians]
		fsincos

		mov edx, DWORD PTR[cosine]
		mov eax, DWORD PTR[sine]

		fstp DWORD PTR[edx]
		fstp DWORD PTR[eax]
	}
}


void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
{

	float sr, sp, sy, cr, cp, cy;
	SinCos(DEG2RAD(angles[YAW]), &sy, &cy);
	SinCos(DEG2RAD(angles[PITCH]), &sp, &cp);
	SinCos(DEG2RAD(angles[ROLL]), &sr, &cr);

	// matrix = (YAW * PITCH) * ROLL
	matrix[0][0] = cp * cy;
	matrix[1][0] = cp * sy;
	matrix[2][0] = -sp;

	float crcy = cr * cy;
	float crsy = cr * sy;
	float srcy = sr * cy;
	float srsy = sr * sy;
	matrix[0][1] = sp * srcy - crsy;
	matrix[1][1] = sp * srsy + crcy;
	matrix[2][1] = sr * cp;

	matrix[0][2] = (sp * crcy + srsy);
	matrix[1][2] = (sp * crsy - srcy);
	matrix[2][2] = cr * cp;

	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
}

void MatrixSetColumn(const Vector& in, int column, matrix3x4_t& out)
{
	out[0][column] = in.x;
	out[1][column] = in.y;
	out[2][column] = in.z;
}

void AngleMatrix(const QAngle& angles, const Vector& position, matrix3x4_t& matrix)
{
	AngleMatrix(angles, matrix);
	MatrixSetColumn(position, 3, matrix);
}

void SetIdentityMatrix(matrix3x4_t& matrix)
{
	memset(matrix.Base(), 0, sizeof(float) * 3 * 4);
	matrix[0][0] = 1.0;
	matrix[1][1] = 1.0;
	matrix[2][2] = 1.0;
}

void AngleVectors(const QAngle& angles, Vector* forward, Vector* right, Vector* up)
{
	float sr, sp, sy, cr, cp, cy;
	SinCos(DEG2RAD(angles[YAW]), &sy, &cy);
	SinCos(DEG2RAD(angles[PITCH]), &sp, &cp);
	SinCos(DEG2RAD(angles[ROLL]), &sr, &cr);

	if (forward)
	{
		forward->x = cp * cy;
		forward->y = cp * sy;
		forward->z = -sp;
	}

	if (right)
	{
		right->x = (-1 * sr * sp * cy + -1 * cr * -sy);
		right->y = (-1 * sr * sp * sy + -1 * cr * cy);
		right->z = -1 * sr * cp;
	}

	if (up)
	{
		up->x = (cr * sp * cy + -sr * -sy);
		up->y = (cr * sp * sy + -sr * cy);
		up->z = cr * cp;
	}
}


FORCEINLINE vec_t DotProduct(const Vector& a, const Vector& b)
{
	return(a.x * b.x + a.y * b.y + a.z * b.z);
}


struct PortalViewResult_t
{
	CViewSetup viewSetup;
	VMatrix    viewMatrix;
};

void VectorTransform(const float* in1, const matrix3x4_t& in2, float* out)
{
	assert(in1 != out);
	out[0] = DotProduct(in1, in2[0]) + in2[0][3];
	out[1] = DotProduct(in1, in2[1]) + in2[1][3];
	out[2] = DotProduct(in1, in2[2]) + in2[2][3];
}

inline void VectorTransform(const Vector& in1, const matrix3x4_t& in2, Vector& out)
{
	VectorTransform(&in1.x, in2, &out.x);
}

float VectorNormalize(Vector& vec)
{
	float radius = sqrtf(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);

	// FLT_EPSILON is added to the radius to eliminate the possibility of divide by zero.
	float iradius = 1.f / (radius + FLT_EPSILON);

	vec.x *= iradius;
	vec.y *= iradius;
	vec.z *= iradius;

	return radius;
}

// 在你的代码顶部或常量定义文件中，定义一个偏移量
const float PORTAL_CAMERA_OFFSET = 0.0f; // 你可以调整这个值，比如1.0, 2.0, 5.0等，找到最合适的大小

/*
 * 计算传送门虚拟摄像机的视角
 * @param playerView      - 当前玩家的原始CViewSetup
 * @param pEntrancePortal - 玩家正在“看向”的传送门 (入口)
 * @param pExitPortal     - 应该从哪个传送门“看出去” (出口)
 * @return                - 一个新的、计算好的CViewSetup，用于渲染
 */
CViewSetup CalculatePortalView(const CViewSetup& playerView, const PortalInfo_t* pEntrancePortal, const PortalInfo_t* pExitPortal)
{
	// 1. 预计算“入口到出口”的变换矩阵
	matrix3x4_t entranceMatrix, exitMatrix, entranceMatrixInverse, thisToLinkedMatrix;
	matrix3x4_t mat_180_Z_Rot;
	SetIdentityMatrix(mat_180_Z_Rot);
	mat_180_Z_Rot[0][0] = -1.0f;
	mat_180_Z_Rot[1][1] = -1.0f;

	AngleMatrix(pEntrancePortal->angles, pEntrancePortal->origin, entranceMatrix);
	AngleMatrix(pExitPortal->angles, pExitPortal->origin, exitMatrix);

	MatrixInverse(entranceMatrix, entranceMatrixInverse);

	matrix3x4_t tempMatrix;
	ConcatTransforms(mat_180_Z_Rot, entranceMatrixInverse, tempMatrix);
	ConcatTransforms(exitMatrix, tempMatrix, thisToLinkedMatrix);

	// 2. 直接复制 playerView，以此为基础进行修改
	CViewSetup portalView = playerView;

	// 3. 变换 origin
	VectorTransform(playerView.origin, thisToLinkedMatrix, portalView.origin);

	// 4. 变换 angles
	matrix3x4_t playerAnglesMatrix, newAnglesMatrix;
	AngleMatrix(playerView.angles, playerAnglesMatrix);
	ConcatTransforms(thisToLinkedMatrix, playerAnglesMatrix, newAnglesMatrix);
	MatrixAngles(newAnglesMatrix, portalView.angles);

	// 5. 确保 zNear 不小于引擎允许的最小值
	if (portalView.zNear < 1.0f) {
		portalView.zNear = 1.0f;
	}

	// 6. 【核心修正】将虚拟摄像机沿出口法线方向稍微向前推，以进入有效的Visleaf
	// 这是解决黑天、全亮模型和渲染缺失问题的关键
	Vector exitPortalNormal;
	AngleVectors(pExitPortal->angles, &exitPortalNormal, nullptr, nullptr);

	// 将摄像机向前推动一个很小的单位（例如 1.0f），确保它在传送门“外面”
	portalView.origin += exitPortalNormal * 1.0f;

	return portalView;
}

using namespace Hooks;

void __fastcall BaseClient::LevelInitPreEntity::Detour(void* ecx, void* edx, char const* pMapName)
{
	Table.Original<FN>(Index)(ecx, edx, pMapName);
}

void __fastcall BaseClient::LevelInitPostEntity::Detour(void* ecx, void* edx)
{
	Table.Original<FN>(Index)(ecx, edx);
}

void __fastcall BaseClient::LevelShutdown::Detour(void* ecx, void* edx)
{
	Table.Original<FN>(Index)(ecx, edx);
}

void __fastcall BaseClient::FrameStageNotify::Detour(void* ecx, void* edx, ClientFrameStage_t curStage)
{
	Table.Original<FN>(Index)(ecx, edx, curStage);
}

//void __fastcall BaseClient::RenderView::Detour(void* ecx, void* edx, CViewSetup& setup, int nClearFlags, int whatToDraw)
//{
//	printf("RenderView Hooked.\n");
//	Table.Original<FN>(Index)(ecx, edx, setup, nClearFlags, whatToDraw);
//}

// 全局标志，需要在ModelRender.cpp中声明
//extern bool g_bIsRenderingPortalTexture;
void __fastcall BaseClient::RenderView::Detour(void* ecx, void* edx, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw)
{
	if (!g_pClient_this_ptr) {
		g_pClient_this_ptr = ecx;
	}
#ifdef PLAN_B
	// 只有在游戏内且传送门系统初始化后才执行
	if (I::EngineClient->IsInGame() && G::G_L4D2Portal.m_pMaterialSystem
		&& G::G_L4D2Portal.m_pPortalTexture && G::G_L4D2Portal.m_pPortalTexture_2
		&& G::G_L4D2Portal.g_BluePortal.bIsActive && G::G_L4D2Portal.g_OrangePortal.bIsActive)
	{
		IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
		if (pRenderContext)
		{
			// --- 渲染蓝门内的场景 (裁剪平面在橙门) ---
			{
				// 边界条件检查: 玩家必须在蓝门前方且大致朝向蓝门
				// (这部分逻辑是好的优化，保持)
				Vector vPlayerFwd, vBluePortalFwd, vToPlayer;
				AngleVectors(setup.angles, &vPlayerFwd, nullptr, nullptr);
				AngleVectors(G::G_L4D2Portal.g_BluePortal.angles, &vBluePortalFwd, nullptr, nullptr);
				vToPlayer = setup.origin - G::G_L4D2Portal.g_BluePortal.origin;
				VectorNormalize(vToPlayer); // 归一化以获得纯方向

				if (/*DotProduct(vToPlayer, vBluePortalFwd) >= 0.0f*/ true)
				{
					pRenderContext->PushRenderTargetAndViewport();
					pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture);
					pRenderContext->Viewport(0, 0, setup.width, setup.height);
					pRenderContext->ClearBuffers(true, true, true);

					// --- 定义出口（橙门）的裁剪平面 ---
					Vector exitNormal_O;
					AngleVectors(G::G_L4D2Portal.g_OrangePortal.angles, &exitNormal_O, nullptr, nullptr);

					float clipPlane_O[4];
					clipPlane_O[0] = exitNormal_O.x;
					clipPlane_O[1] = exitNormal_O.y;
					clipPlane_O[2] = exitNormal_O.z;

					// 【核心修正】修正 D 值的计算，遵循 n·p - d = 0 的形式
					// d = n·p，并加入一个小的偏移量防止瑕疵
					clipPlane_O[3] = DotProduct(G::G_L4D2Portal.g_OrangePortal.origin, exitNormal_O) - 0.5f;

					CViewSetup portalView = CalculatePortalView(setup, &G::G_L4D2Portal.g_BluePortal, &G::G_L4D2Portal.g_OrangePortal);

					pRenderContext->PushCustomClipPlane(clipPlane_O);
					pRenderContext->EnableClipping(true);


					//I::RenderView->Push3DView(pRenderContext, &portalView, 0, G::G_L4D2Portal.m_pPortalTexture, &G::G_L4D2Portal.m_Frustum);
					// 递归调用原函数进行渲染, 不渲染玩家模型和HUD
					Func.Original<FN>()(ecx, edx, portalView, hudViewSetup, nClearFlags, whatToDraw & (~RENDERVIEW_DRAWVIEWMODEL) & (~RENDERVIEW_DRAWHUD));
					//I::RenderView->PopView(pRenderContext, &G::G_L4D2Portal.m_Frustum);


					pRenderContext->EnableClipping(false);
					pRenderContext->PopCustomClipPlane();
					pRenderContext->PopRenderTargetAndViewport();
				}
			}

			// --- 渲染橙门内的场景 (裁剪平面在蓝门) ---
			{
				// 对橙门执行相同的边界条件检查
				Vector vPlayerFwd, vOrangePortalFwd, vToPlayer;
				AngleVectors(setup.angles, &vPlayerFwd, nullptr, nullptr);
				AngleVectors(G::G_L4D2Portal.g_OrangePortal.angles, &vOrangePortalFwd, nullptr, nullptr);
				vToPlayer = setup.origin - G::G_L4D2Portal.g_OrangePortal.origin;
				VectorNormalize(vToPlayer);

				if (/*DotProduct(vToPlayer, vOrangePortalFwd) >= 0.0f*/ true)
				{
					pRenderContext->PushRenderTargetAndViewport();
					pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture_2);
					pRenderContext->Viewport(0, 0, setup.width, setup.height);
					pRenderContext->ClearBuffers(true, true, true);

					// --- 定义出口（蓝门）的裁剪平面 ---
					Vector exitNormal_B;
					AngleVectors(G::G_L4D2Portal.g_BluePortal.angles, &exitNormal_B, nullptr, nullptr);

					float clipPlane_B[4];
					clipPlane_B[0] = exitNormal_B.x;
					clipPlane_B[1] = exitNormal_B.y;
					clipPlane_B[2] = exitNormal_B.z;

					// 【核心修正】修正 D 值的计算
					clipPlane_B[3] = DotProduct(G::G_L4D2Portal.g_BluePortal.origin, exitNormal_B) - 0.5f;

					CViewSetup portalView2 = CalculatePortalView(setup, &G::G_L4D2Portal.g_OrangePortal, &G::G_L4D2Portal.g_BluePortal);


					pRenderContext->PushCustomClipPlane(clipPlane_B);
					pRenderContext->EnableClipping(true);


					//I::RenderView->Push3DView(pRenderContext, &portalView2, 0, G::G_L4D2Portal.m_pPortalTexture_2, &G::G_L4D2Portal.m_Frustum);
					Func.Original<FN>()(ecx, edx, portalView2, hudViewSetup, nClearFlags, whatToDraw & (~RENDERVIEW_DRAWVIEWMODEL) & (~RENDERVIEW_DRAWHUD));
					//I::RenderView->PopView(pRenderContext, &G::G_L4D2Portal.m_Frustum);

					pRenderContext->EnableClipping(false);
					pRenderContext->PopCustomClipPlane();
					pRenderContext->PopRenderTargetAndViewport();
				}
			}
		}
	}
#endif
	// 正常情况下调用原函数，渲染玩家的主视角
	Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
}

void BaseClient::Init()
{
	XASSERT(Table.Init(I::BaseClient) == false);
	XASSERT(Table.Hook(&LevelInitPreEntity::Detour, LevelInitPreEntity::Index) == false);
	XASSERT(Table.Hook(&LevelInitPostEntity::Detour, LevelInitPostEntity::Index) == false);
	XASSERT(Table.Hook(&LevelShutdown::Detour, LevelShutdown::Index) == false);
	XASSERT(Table.Hook(&FrameStageNotify::Detour, FrameStageNotify::Index) == false);
	//XASSERT(Table.Hook(&RenderView::Detour, RenderView::Index) == false);

	//RenderView
	{
		using namespace RenderView;

		const FN pfRenderView = reinterpret_cast<FN>(U::Offsets.m_dwRenderView);
		printf("[BaseClient] RenderView: %p\n", pfRenderView);
		XASSERT(pfRenderView == nullptr);

		if (pfRenderView)
			XASSERT(Func.Init(pfRenderView, &Detour) == false);
	}
}