#include "Math.h"

void CUtil_Math::VectorTransform(const Vector input, const matrix3x4_t& matrix, Vector& output)
{
	output[0] = input.Dot({ matrix[0][0], matrix[0][1], matrix[0][2] }) + matrix[0][3];
	output[1] = input.Dot({ matrix[1][0], matrix[1][1], matrix[1][2] }) + matrix[1][3];
	output[2] = input.Dot({ matrix[2][0], matrix[2][1], matrix[2][2] }) + matrix[2][3];
}

void CUtil_Math::BuildTransformedBox(Vector* v2, const Vector bbmin, const Vector bbmax, const matrix3x4_t& m)
{
	Vector v[8];
	PointsFromBox(bbmin, bbmax, v);

	VectorTransform(v[0], m, v2[0]);
	VectorTransform(v[1], m, v2[1]);
	VectorTransform(v[2], m, v2[2]);
	VectorTransform(v[3], m, v2[3]);
	VectorTransform(v[4], m, v2[4]);
	VectorTransform(v[5], m, v2[5]);
	VectorTransform(v[6], m, v2[6]);
	VectorTransform(v[7], m, v2[7]);
}

void CUtil_Math::PointsFromBox(const Vector mins, const Vector maxs, Vector* points)
{
	points[0][0] = mins[0];
	points[0][1] = mins[1];
	points[0][2] = mins[2];

	points[1][0] = mins[0];
	points[1][1] = mins[1];
	points[1][2] = maxs[2];

	points[2][0] = mins[0];
	points[2][1] = maxs[1];
	points[2][2] = mins[2];

	points[3][0] = mins[0];
	points[3][1] = maxs[1];
	points[3][2] = maxs[2];

	points[4][0] = maxs[0];
	points[4][1] = mins[1];
	points[4][2] = mins[2];

	points[5][0] = maxs[0];
	points[5][1] = mins[1];
	points[5][2] = maxs[2];

	points[6][0] = maxs[0];
	points[6][1] = maxs[1];
	points[6][2] = mins[2];

	points[7][0] = maxs[0];
	points[7][1] = maxs[1];
	points[7][2] = maxs[2];
}

void CUtil_Math::VectorAngles(const Vector& forward, Vector& angles)
{
	float yaw, pitch;

	if (forward.y == 0.0f && forward.x == 0.0f)
	{
		yaw = 0.0f;
		pitch = (forward.z > 0.0f) ? 270.0f : 90.0f;
	}
	else
	{
		yaw = RAD2DEGF(::atan2f(forward.y, forward.x));
		yaw += (360.0f * (yaw < 0.0f));

		const float tmp = forward.Lenght2D();

		pitch = RAD2DEGF(::atan2f(-forward.z, tmp));
		pitch += (360.0f * (pitch < 0.0f));
	}

	angles[0] = pitch;
	angles[1] = yaw;
	angles[2] = 0.0f;
}

void CUtil_Math::VectorAngles(const Vector& forward, QAngle& angles)
{
	float yaw, pitch;

	if (forward.y == 0.0f && forward.x == 0.0f)
	{
		yaw = 0.0f;
		pitch = (forward.z > 0.0f) ? 270.0f : 90.0f;
	}
	else
	{
		yaw = RAD2DEGF(::atan2f(forward.y, forward.x));
		yaw += (360.0f * (yaw < 0.0f));

		const float tmp = forward.Lenght2D();

		pitch = RAD2DEGF(::atan2f(-forward.z, tmp));
		pitch += (360.0f * (pitch < 0.0f));
	}

	angles[0] = pitch;
	angles[1] = yaw;
	angles[2] = 0.0f;
}

void CUtil_Math::AngleVectors(const Vector vAngles, Vector* vForward)
{
	float sp, sy, cp, cy;

	const float flX = DEG2RADF(vAngles.x);
	sp = ::sinf(flX);
	cp = ::cosf(flX);

	const float flY = DEG2RADF(vAngles.y);
	sy = ::sinf(flY);
	cy = ::cosf(flY);

	if (vForward)
	{
		vForward->x = (cp * cy);
		vForward->y = (cp * sy);
		vForward->z = -sp;
	}
}

void CUtil_Math::ClampAngles(Vector& v)
{
	v.x = Max(-89.0f, Min(89.0f, NormalizeAngle(v.x)));
	v.y = NormalizeAngle(v.y);
	v.z = 0.0f;
}

void CUtil_Math::RotateTriangle(Vector2D* v, const float flRotation)
{
	const Vector2D vCenter = (v[0] + v[1] + v[2]) / 3.0f;

	for (int n = 0; n < 3; n++)
	{
		v[n] -= vCenter;

		const float flX = v[n].x;
		const float flY = v[n].y;

		const float r = DEG2RADF(flRotation);
		const float c = ::cosf(r);
		const float s = ::sinf(r);

		v[n].x = (flX * c) - (flY * s);
		v[n].y = (flX * s) + (flY * c);

		v[n] += vCenter;
	}
}

float CUtil_Math::GetFovBetween(const Vector vSrc, const Vector vDst)
{
	Vector v_src = { };
	AngleVectors(vSrc, &v_src);

	Vector v_dst = { };
	AngleVectors(vDst, &v_dst);

	float result = RAD2DEG(acos(v_dst.Dot(v_src) / v_dst.LenghtSqr()));

	if (!isfinite(result) || isinf(result) || isnan(result))
		result = FLT_MAX;

	return result;
}

float CUtil_Math::NormalizeAngle(const float ang)
{
	if (!::isfinite(ang))
		return 0.0f;

	return ::remainderf(ang, 360.0f);
}

Vector CUtil_Math::GetAngleToPosition(const Vector vFrom, const Vector vTo)
{
	const Vector vDelta = (vFrom - vTo);
	const float flHyp = ::sqrtf((vDelta.x * vDelta.x) + (vDelta.y * vDelta.y));

	return { (::atanf(vDelta.z / flHyp) * M_RADPI), (::atanf(vDelta.y / vDelta.x) * M_RADPI) + (180.0f * (vDelta.x >= 0.0f)), 0.0f };
}

float CUtil_Math::FloatMakePositive(vec_t f)
{
	return (float)fabs(f);
}

bool CUtil_Math::MatrixInverseGeneral(const VMatrix& src, VMatrix& dst)
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
bool CUtil_Math::MatrixInverse(const matrix3x4_t& src, matrix3x4_t& dst)
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

void CUtil_Math::MatrixGetColumn(const matrix3x4_t& in, int column, Vector& out)
{
	out.x = in[0][column];
	out.y = in[1][column];
	out.z = in[2][column];
}

void CUtil_Math::MatrixAngles(const matrix3x4_t& matrix, float* angles)
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

void CUtil_Math::MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
	memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}

void CUtil_Math::ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
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

void CUtil_Math::AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
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

void CUtil_Math::MatrixSetColumn(const Vector& in, int column, matrix3x4_t& out)
{
	out[0][column] = in.x;
	out[1][column] = in.y;
	out[2][column] = in.z;
}

void CUtil_Math::AngleMatrix(const QAngle& angles, const Vector& position, matrix3x4_t& matrix)
{
	AngleMatrix(angles, matrix);
	MatrixSetColumn(position, 3, matrix);
}

void CUtil_Math::SetIdentityMatrix(matrix3x4_t& matrix)
{
	memset(matrix.Base(), 0, sizeof(float) * 3 * 4);
	matrix[0][0] = 1.0;
	matrix[1][1] = 1.0;
	matrix[2][2] = 1.0;
}

void CUtil_Math::AngleVectors(const QAngle& angles, Vector* forward, Vector* right, Vector* up)
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

void CUtil_Math::VectorTransform(const float* in1, const matrix3x4_t& in2, float* out)
{
	assert(in1 != out);
	out[0] = DotProduct(in1, in2[0]) + in2[0][3];
	out[1] = DotProduct(in1, in2[1]) + in2[1][3];
	out[2] = DotProduct(in1, in2[2]) + in2[2][3];
}

float CUtil_Math::VectorNormalize(Vector& vec)
{
	float radius = sqrtf(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);

	// FLT_EPSILON is added to the radius to eliminate the possibility of divide by zero.
	float iradius = 1.f / (radius + FLT_EPSILON);

	vec.x *= iradius;
	vec.y *= iradius;
	vec.z *= iradius;

	return radius;
}