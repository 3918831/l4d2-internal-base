#pragma once

#include <assert.h>

#include "Vector/Vector.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

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

// From L4D2VR
//class CViewSetup
//{
//public:
//	int32_t x; //0x0000
//	int32_t m_nUnscaledX; //0x0004
//	int32_t y; //0x0008
//	int32_t m_nUnscaledY; //0x000C
//	int32_t width; //0x0010
//	int32_t m_nUnscaledWidth; //0x0014
//	int32_t height; //0x0018
//	int32_t m_nUnscaledHeight; //0x001C
//	char pad_0020[20]; //0x0020
//	float fov; //0x0034
//	float fovViewmodel; //0x0038
//	Vector origin; //0x003C
//	QAngle angles; //0x0048
//	float zNear; //0x0054
//	float zFar; //0x0058
//	float zNearViewmodel; //0x005C
//	float zFarViewmodel; //0x0060
//	float m_flAspectRatio; //0x0064
//	char pad_0068[1660]; //0x0068
//}; //Size: 0x06E4
//static_assert(sizeof(CViewSetup) == 0x6E4);

// From Gemini
class CViewSetup
{
public:
	int            x;                  // 0x0000
	int            m_nUnscaledX;       // 0x0004
	int            y;                  // 0x0008
	int            m_nUnscaledY;       // 0x000C
	int            width;              // 0x0010
	int            m_nUnscaledWidth;   // 0x0014
	int            height;             // 0x0018
	int            m_nUnscaledHeight;  // 0x001C
	bool           m_bOrtho;           // 0x0020
	char           pad_0021[3];        // 0x0021 (Padding for alignment)
	float          m_OrthoLeft;        // 0x0024
	float          m_OrthoTop;         // 0x0028
	float          m_OrthoRight;       // 0x002C
	float          m_OrthoBottom;      // 0x0030
	float          fov;                // 0x0034
	float          fovViewmodel;       // 0x0038
	Vector         origin;             // 0x003C
	QAngle         angles;             // 0x0048
	float          zNear;              // 0x0054
	float          zFar;               // 0x0058
	float          zNearViewmodel;     // 0x005C
	float          zFarViewmodel;      // 0x0060
	float          m_flAspectRatio;    // 0x0064
	float          m_flNearBlurDepth;  // 0x0068
	float          m_flNearFocusDepth; // 0x006C
	float          m_flFarFocusDepth;  // 0x0070
	float          m_flFarBlurDepth;   // 0x0074
	float          m_flNearBlurRadius; // 0x0078
	float          m_flFarBlurRadius;  // 0x007C
	int            m_nDoFQuality;      // 0x0080
	int            m_nMotionBlurMode;  // 0x0084
	float          m_flShutterTime;    // 0x0088
	Vector         m_vShutterOpenPosition; // 0x008C
	QAngle         m_vShutterOpenAngles;   // 0x0098
	Vector         m_vShutterClosePosition;// 0x00A4
	QAngle         m_vShutterCloseAngles;  // 0x00B0
	float          m_flOffCenterTop;   // 0x00BC
	float          m_flOffCenterBottom;// 0x00C0
	float          m_flOffCenterLeft;  // 0x00C4
	float          m_flOffCenterRight; // 0x00C8
	bool           m_bOffCenter;       // 0x00CC
	bool           m_bRenderToSubrectOfLargerScreen; // 0x00CD

	// 【我们需要的关键变量就在这里！】
	bool           m_bDoBloomAndToneMapping; // 0x00CE

	bool           m_bCacheFullSceneState; // 0x00CF
	char           pad_00D0[1556];     // 0x00D0
}; //Size: 0x6E4
static_assert(sizeof(CViewSetup) == 0x6E4); // 使用 static_assert 再次确认总大小无误


struct matrix3x4_t
{
	matrix3x4_t() {
		m_flMatVal[0][0] = 0.0f; m_flMatVal[0][1] = 0.0f; m_flMatVal[0][2] = 0.0f; m_flMatVal[0][3] = 0.0f;
		m_flMatVal[1][0] = 0.0f; m_flMatVal[1][1] = 0.0f; m_flMatVal[1][2] = 0.0f; m_flMatVal[1][3] = 0.0f;
		m_flMatVal[2][0] = 0.0f; m_flMatVal[2][1] = 0.0f; m_flMatVal[2][2] = 0.0f; m_flMatVal[2][3] = 0.0f;
	}
	matrix3x4_t(
		float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23)
	{
		m_flMatVal[0][0] = m00;	m_flMatVal[0][1] = m01; m_flMatVal[0][2] = m02; m_flMatVal[0][3] = m03;
		m_flMatVal[1][0] = m10;	m_flMatVal[1][1] = m11; m_flMatVal[1][2] = m12; m_flMatVal[1][3] = m13;
		m_flMatVal[2][0] = m20;	m_flMatVal[2][1] = m21; m_flMatVal[2][2] = m22; m_flMatVal[2][3] = m23;
	}

	//-----------------------------------------------------------------------------
	// Creates a matrix where the X axis = forward
	// the Y axis = left, and the Z axis = up
	//-----------------------------------------------------------------------------
	void Init(const Vector& xAxis, const Vector& yAxis, const Vector& zAxis, const Vector& vecOrigin)
	{
		m_flMatVal[0][0] = xAxis.x; m_flMatVal[0][1] = yAxis.x; m_flMatVal[0][2] = zAxis.x; m_flMatVal[0][3] = vecOrigin.x;
		m_flMatVal[1][0] = xAxis.y; m_flMatVal[1][1] = yAxis.y; m_flMatVal[1][2] = zAxis.y; m_flMatVal[1][3] = vecOrigin.y;
		m_flMatVal[2][0] = xAxis.z; m_flMatVal[2][1] = yAxis.z; m_flMatVal[2][2] = zAxis.z; m_flMatVal[2][3] = vecOrigin.z;
	}

	//-----------------------------------------------------------------------------
	// Creates a matrix where the X axis = forward
	// the Y axis = left, and the Z axis = up
	//-----------------------------------------------------------------------------
	matrix3x4_t(const Vector& xAxis, const Vector& yAxis, const Vector& zAxis, const Vector& vecOrigin)
	{
		Init(xAxis, yAxis, zAxis, vecOrigin);
	}


	float* operator[](int i) { assert((i >= 0) && (i < 3)); return m_flMatVal[i]; }
	const float* operator[](int i) const { assert((i >= 0) && (i < 3)); return m_flMatVal[i]; }
	float* Base() { return &m_flMatVal[0][0]; }
	const float* Base() const { return &m_flMatVal[0][0]; }

	float m_flMatVal[3][4];
};

class VMatrix
{
public:

	VMatrix();
	VMatrix(
		vec_t m00, vec_t m01, vec_t m02, vec_t m03,
		vec_t m10, vec_t m11, vec_t m12, vec_t m13,
		vec_t m20, vec_t m21, vec_t m22, vec_t m23,
		vec_t m30, vec_t m31, vec_t m32, vec_t m33
	);

	// Creates a matrix where the X axis = forward
	// the Y axis = left, and the Z axis = up
	VMatrix(const Vector& forward, const Vector& left, const Vector& up);

	// Construct from a 3x4 matrix
	VMatrix(const matrix3x4_t& matrix3x4);

	// Set the values in the matrix.
	void		Init(
		vec_t m00, vec_t m01, vec_t m02, vec_t m03,
		vec_t m10, vec_t m11, vec_t m12, vec_t m13,
		vec_t m20, vec_t m21, vec_t m22, vec_t m23,
		vec_t m30, vec_t m31, vec_t m32, vec_t m33
	);


	// Initialize from a 3x4
	void		Init(const matrix3x4_t& matrix3x4);

	// Transpose.
	VMatrix		Transpose() const;

	// array access
	inline float* operator[](int i)
	{
		return m[i];
	}

	inline const float* operator[](int i) const
	{
		return m[i];
	}

	// Get a pointer to m[0][0]
	inline float* Base()
	{
		return &m[0][0];
	}

	inline const float* Base() const
	{
		return &m[0][0];
	}

	// The matrix.
	vec_t		m[4][4];
};

inline void VMatrix::Init(const matrix3x4_t& matrix3x4)
{
	memcpy(m, matrix3x4.Base(), sizeof(matrix3x4_t));

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = 0.0f;
	m[3][3] = 1.0f;
}

inline VMatrix::VMatrix()
{
}

inline void VMatrix::Init(
	vec_t m00, vec_t m01, vec_t m02, vec_t m03,
	vec_t m10, vec_t m11, vec_t m12, vec_t m13,
	vec_t m20, vec_t m21, vec_t m22, vec_t m23,
	vec_t m30, vec_t m31, vec_t m32, vec_t m33
)
{
	m[0][0] = m00;
	m[0][1] = m01;
	m[0][2] = m02;
	m[0][3] = m03;

	m[1][0] = m10;
	m[1][1] = m11;
	m[1][2] = m12;
	m[1][3] = m13;

	m[2][0] = m20;
	m[2][1] = m21;
	m[2][2] = m22;
	m[2][3] = m23;

	m[3][0] = m30;
	m[3][1] = m31;
	m[3][2] = m32;
	m[3][3] = m33;
}

inline VMatrix::VMatrix(
	vec_t m00, vec_t m01, vec_t m02, vec_t m03,
	vec_t m10, vec_t m11, vec_t m12, vec_t m13,
	vec_t m20, vec_t m21, vec_t m22, vec_t m23,
	vec_t m30, vec_t m31, vec_t m32, vec_t m33)
{
	Init(
		m00, m01, m02, m03,
		m10, m11, m12, m13,
		m20, m21, m22, m23,
		m30, m31, m32, m33
	);
}


inline VMatrix VMatrix::Transpose() const
{
	return VMatrix(
		m[0][0], m[1][0], m[2][0], m[3][0],
		m[0][1], m[1][1], m[2][1], m[3][1],
		m[0][2], m[1][2], m[2][2], m[3][2],
		m[0][3], m[1][3], m[2][3], m[3][3]);
}

void inline SinCos(float radians, float* sine, float* cosine)
{
	_asm
	{
		fld	DWORD PTR[radians]
		fsincos

		mov edx, DWORD PTR[cosine]
		mov eax, DWORD PTR[sine]

		fstp DWORD PTR[edx]
		fstp DWORD PTR[eax]
	}
}

FORCEINLINE vec_t DotProduct(const Vector& a, const Vector& b)
{
	return(a.x * b.x + a.y * b.y + a.z * b.z);
}

//using matrix3x4_t = float[3][4];
//using VMatrix = float[4][4];

class CUtil_Math
{
public:
	void VectorTransform(const Vector input, const matrix3x4_t& matrix, Vector& output);
	void BuildTransformedBox(Vector* v2, const Vector bbmin, const Vector bbmax, const matrix3x4_t& m);
	void PointsFromBox(const Vector mins, const Vector maxs, Vector* points);
	void VectorAngles(const Vector& forward, Vector& angles);
	void AngleVectors(const Vector vAngles, Vector* vForward);
	void ClampAngles(Vector& v);
	void RotateTriangle(Vector2D* v, const float flRotation);

	float GetFovBetween(const Vector vSrc, const Vector vDst);
	float NormalizeAngle(const float ang);

	Vector GetAngleToPosition(const Vector vFrom, const Vector vTo);

	// ==============================================================================
	// 自行添加的Math相关函数
	float FloatMakePositive(vec_t f);
	bool MatrixInverseGeneral(const VMatrix& src, VMatrix& dst);

	// 这是一个我们自己实现的 "包装" 函数，提供了我们需要的签名
	bool MatrixInverse(const matrix3x4_t& src, matrix3x4_t& dst);

	void MatrixGetColumn(const matrix3x4_t& in, int column, Vector& out);
	void MatrixAngles(const matrix3x4_t& matrix, float* angles);
	inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles)
	{
		MatrixAngles(matrix, &angles.x);
	}
	void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out);
	void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out);
	void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix);
	void MatrixSetColumn(const Vector& in, int column, matrix3x4_t& out);
	void AngleMatrix(const QAngle& angles, const Vector& position, matrix3x4_t& matrix);
	void SetIdentityMatrix(matrix3x4_t& matrix);
	void AngleVectors(const QAngle& angles, Vector* forward, Vector* right, Vector* up);
	void VectorTransform(const float* in1, const matrix3x4_t& in2, float* out);
	//inline void VectorTransform(const Vector& in1, const matrix3x4_t& in2, Vector& out)
	//{
	//	VectorTransform(&in1.x, in2, &out.x);
	//}
	float VectorNormalize(Vector& vec);



	// 自行添加的Math相关函数
	// ==============================================================================
	
public:
	template<typename T>
	inline T Clamp(const T val, const T min, const T max) {
		const T t = (val < min) ? min : val;
		return (t > max) ? max : t;
	}

	template<typename T>
	inline T Min(const T a, const T b) {
		return ((a > b) * b) + ((a <= b) * a);
	}

	template<typename T>
	inline T Max(const T a, const T b) {
		return ((a > b) * a) + ((a <= b) * b);
	}

	//Not really math related at all.
	template<typename F, typename ... T>
	inline bool CompareGroup(F&& first, T&& ... t) {
		return ((first == t) || ...);
	}
};

namespace U { inline CUtil_Math Math; }