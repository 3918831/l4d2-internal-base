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

CViewSetup g_ViewSetup;
CViewSetup g_hudViewSetup;
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

// 在你的代码顶部或常量定义文件中，定义一个偏移量
const float PORTAL_CAMERA_OFFSET = 200.0f; // 你可以调整这个值，比如1.0, 2.0, 5.0等，找到最合适的大小

/*
 * 计算传送门虚拟摄像机的视角
 * @param playerView      - 当前玩家的原始CViewSetup
 * @param pEntrancePortal - 玩家正在“看向”的传送门 (入口)
 * @param pExitPortal     - 应该从哪个传送门“看出去” (出口)
 * @return                - 一个新的、计算好的CViewSetup，用于渲染
 */
CViewSetup CalculatePortalView(const CViewSetup& playerView, const PortalInfo_t* pEntrancePortal, const PortalInfo_t* pExitPortal)
{
	// --- 1. 获取所有参与者的世界变换矩阵 ---
	matrix3x4_t playerMatrix, entranceMatrix, exitMatrix;
	AngleMatrix(playerView.angles, playerView.origin, playerMatrix);
	AngleMatrix(pEntrancePortal->angles, pEntrancePortal->origin, entranceMatrix);
	AngleMatrix(pExitPortal->angles, pExitPortal->origin, exitMatrix);

	// --- 2. 计算入口传送门矩阵的逆矩阵 ---
	matrix3x4_t entranceMatrixInverse;
	MatrixInverse(entranceMatrix, entranceMatrixInverse);

	// --- 3. 定义一个围绕Z轴旋转180度的“镜像”矩阵 ---
	matrix3x4_t flipMatrix;
	SetIdentityMatrix(flipMatrix);
	flipMatrix[0][0] = -1.0f;
	flipMatrix[1][1] = -1.0f;

	// --- 4. 核心矩阵运算： Final = Exit * Flip * Entrance_Inverse * Player ---
	matrix3x4_t tempMatrix, finalMatrix;
	ConcatTransforms(entranceMatrixInverse, playerMatrix, tempMatrix); // temp = Entrance_Inverse * Player
	ConcatTransforms(flipMatrix, tempMatrix, tempMatrix);              // temp = Flip * temp
	ConcatTransforms(exitMatrix, tempMatrix, finalMatrix);             // final = Exit * temp

	// --- 5. 从零开始构建一个纯净的 CViewSetup，防止任何状态污染 ---
	CViewSetup portalView;
	memset(&portalView, 0, sizeof(CViewSetup));

	// 从原始玩家视角中复制绝对安全的参数
	portalView.width = playerView.width;
	portalView.height = playerView.height;
	portalView.fov = playerView.fov;
	portalView.zNear = playerView.zNear;
	portalView.zFar = playerView.zFar;
	portalView.m_flAspectRatio = (float)portalView.width / (float)portalView.height;

	// 从我们最终计算出的矩阵中提取新的位置和角度
	MatrixGetColumn(finalMatrix, 3, portalView.origin);
	MatrixAngles(finalMatrix, portalView.angles);

	// ============================ 新增的位置偏移修正 ============================
	// 1. 获取出口传送门的前进方向向量
	Vector exitForward;
	AngleVectors(pExitPortal->angles, &exitForward, nullptr, nullptr);

	// 2. 将计算出的摄像机位置，沿着出口方向向前推一小段距离
	//    这使得摄像机刚好“离开”墙壁，进入到可渲染的区域内
	portalView.origin += exitForward * PORTAL_CAMERA_OFFSET;
	// ==========================================================================


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
	// 如果我们还没有捕获过这个指针，就现在捕获它
	// 这个操作只会在游戏过程中发生一次

	if (!g_pClient_this_ptr) {
		g_pClient_this_ptr = ecx;
	}

	g_ViewSetup = setup;
	g_hudViewSetup = hudViewSetup;

#ifdef PLAN_B //PLAN_B: 指在RenderView中绘制纹理，在DrawModelExecute中使用模板测试和纹理来绘制传送门内的场景
	// 检查是否在游戏中且需要渲染portal
	if (I::EngineClient->IsInGame() && G::G_L4D2Portal.m_pMaterialSystem 
		&& G::G_L4D2Portal.m_pPortalTexture && G::G_L4D2Portal.m_pPortalTexture_2)
	{

		// 获取渲染上下文
		IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
		if (pRenderContext)
		{
			// ==================== 渲染第一个传送门 (例如蓝色) ====================
			//pRenderContext->PushRenderTargetAndViewport(G::G_L4D2Portal.m_pPortalTexture);
			pRenderContext->PushRenderTargetAndViewport();			
			pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture);
			//pRenderContext->CopyRenderTargetToTextureEx(G::G_L4D2Portal.m_pPortalTexture, 0, NULL, NULL);
			pRenderContext->Viewport(0, 0, setup.width, setup.height);
			pRenderContext->ClearColor4ub(0, 0, 255, 255);
			pRenderContext->ClearBuffers(true, false, false);

			//CViewSetup portalView = g_ViewSetup; // 创建一个副本进行修改
			//portalView.angles.y -= 90.0f; // 仅修改副本
			//portalView.origin = Vector(0.0f, -1000.0f, -57.96875f);

			// 动态计算视角：玩家 -> 看向蓝门 -> 从橙门看出去
			CViewSetup portalView = CalculatePortalView(setup, &G::G_L4D2Portal.g_BluePortal, &G::G_L4D2Portal.g_OrangePortal);

			Func.Original<FN>()(ecx, edx, portalView, hudViewSetup, nClearFlags, whatToDraw & (~RENDERVIEW_DRAWVIEWMODEL) & (~RENDERVIEW_DRAWHUD));

			// 恢复渲染状态
			pRenderContext->PopRenderTargetAndViewport();
			//return; // 直接返回，不执行原函数的完整渲染
			//pRenderContext->DrawScreenSpaceQuad(G::G_L4D2Portal.m_pPortalMaterial);


			// ==================== 渲染第二个传送门 (例如橙色) ====================
			{
				pRenderContext->PushRenderTargetAndViewport();
				pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture_2);

				pRenderContext->Viewport(0, 0, setup.width, setup.height);
				pRenderContext->ClearBuffers(true, true, true);

				// 【核心修正】同样基于当前的、实时的 setup
				//CViewSetup portalView2 = setup;
				// TODO: 将这里的临时代码替换为我们之前设计的 CalculatePortalView 函数
				//portalView2.angles.y += 90.0f;
				//portalView2.origin = Vector(0.0f, 1000.0f, -57.96875f);

				// 动态计算视角：玩家 -> 看向橙门 -> 从蓝门看出去
				CViewSetup portalView2 = CalculatePortalView(setup, &G::G_L4D2Portal.g_OrangePortal, &G::G_L4D2Portal.g_BluePortal);

				Func.Original<FN>()(ecx, edx, portalView2, hudViewSetup, nClearFlags, whatToDraw & (~RENDERVIEW_DRAWVIEWMODEL) & (~RENDERVIEW_DRAWHUD));
				pRenderContext->PopRenderTargetAndViewport();
			}
			
		}		
	}
#endif

	// 正常情况下调用原函数
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