#pragma once

#include "Vector2D.h"

#define M_RADPI		57.295779513082f
#define M_PI		3.14159265358979323846f

#define DEG2RAD(x)	(static_cast<float>(x) * (M_PI / 180.0f))
#define RAD2DEG(x)	(static_cast<float>(x) * (180.0f / M_PI))

#define DEG2RADF(f)	(f * (M_PI / 180.0f))
#define RAD2DEGF(f)	(f * (180.0f / M_PI))

class Vector
{
public:
	Vector(void)
	{
		x = y = z = 0.0f;
	}

	Vector(float X, float Y, float Z)
	{
		x = X; y = Y; z = Z;
	}

	Vector(float* v)
	{
		x = v[0]; y = v[1]; z = v[2];
	}

	Vector(const float* v)
	{
		x = v[0]; y = v[1]; z = v[2];
	}

	Vector(const Vector& v)
	{
		x = v.x; y = v.y; z = v.z;
	}

	Vector(const Vector2D& v)
	{
		x = v.x; y = v.y; z = 0.0f;
	}

	Vector& operator=(const Vector& v)
	{
		x = v.x; y = v.y; z = v.z; return *this;
	}

	Vector& operator=(const Vector2D& v)
	{
		x = v.x; y = v.y; z = 0.0f; return *this;
	}

	float& operator[](int i)
	{
		return ((float*)this)[i];
	}

	float operator[](int i) const
	{
		return ((float*)this)[i];
	}

	Vector& operator+=(const Vector& v)
	{
		x += v.x; y += v.y; z += v.z; return *this;
	}

	Vector& operator-=(const Vector& v)
	{
		x -= v.x; y -= v.y; z -= v.z; return *this;
	}

	Vector& operator*=(const Vector& v)
	{
		x *= v.x; y *= v.y; z *= v.z; return *this;
	}

	Vector& operator/=(const Vector& v)
	{
		x /= v.x; y /= v.y; z /= v.z; return *this;
	}

	Vector& operator+=(float v)
	{
		x += v; y += v; z += v; return *this;
	}

	Vector& operator-=(float v)
	{
		x -= v; y -= v; z -= v; return *this;
	}

	Vector& operator*=(float v)
	{
		x *= v; y *= v; z *= v; return *this;
	}

	Vector& operator/=(float v)
	{
		x /= v; y /= v; z /= v; return *this;
	}

	Vector operator+(const Vector& v) const
	{
		return Vector(x + v.x, y + v.y, z + v.z);
	}

	Vector operator-(const Vector& v) const
	{
		return Vector(x - v.x, y - v.y, z - v.z);
	}

	Vector operator*(const Vector& v) const
	{
		return Vector(x * v.x, y * v.y, z * v.z);
	}

	Vector operator/(const Vector& v) const
	{
		return Vector(x / v.x, y / v.y, z / v.z);
	}

	Vector operator+(float v) const
	{
		return Vector(x + v, y + v, z + v);
	}

	Vector operator-(float v) const
	{
		return Vector(x - v, y - v, z - v);
	}

	Vector operator*(float v) const
	{
		return Vector(x * v, y * v, z * v);
	}

	Vector operator/(float v) const
	{
		return Vector(x / v, y / v, z / v);
	}

	float Lenght(void) const
	{
		return sqrtf(x * x + y * y + z * z);
	}

	float LenghtSqr(void) const
	{
		return (x * x + y * y + z * z);
	}

	float Normalize()
	{
		float fl_lenght = Lenght();
		float fl_lenght_normal = 1.f / ((1.19209290E-07F) + fl_lenght); //FLT_EPSILON

		x = x * fl_lenght_normal;
		y = y * fl_lenght_normal;
		z = z * fl_lenght_normal;

		return fl_lenght;
	}

	void Rotate(const float flYaw)
	{
		const float r = DEG2RAD(flYaw);
		const float s = sinf(r), c = cosf(r);
		const float flX = x, flY = y;

		x = (flX * c) - (flY * s);
		y = (flX * s) + (flY * c);
	}

	float NormalizeInPlace()
	{
		return Normalize();
	}

	float Lenght2D(void) const
	{
		return sqrtf(x * x + y * y);
	}

	float Lenght2DSqr(void) const
	{
		return (x * x + y * y);
	}

	float DistTo(const Vector& v) const
	{
		return (*this - v).Lenght();
	}

	float DistToSqr(const Vector& v) const
	{
		return (*this - v).LenghtSqr();
	}

	float Dot(const Vector& v) const
	{
		return (x * v.x + y * v.y + z * v.z);
	}

	Vector Cross(const Vector& v) const
	{
		return Vector(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
	}

	bool IsZero(const float flScale = 0.0f) const
	{
		return (x > -flScale && x < flScale&&
			y > -flScale && y < flScale&&
			z > -flScale && z < flScale);
	}

	Vector Scale(float fl) {
		return Vector(x * fl, y * fl, z * fl);
	}

	void Init(float ix = 0.0f, float iy = 0.0f, float iz = 0.0f)
	{
		x = ix; y = iy; z = iz;
	}

	void Add(const Vector& a, const Vector& b)
	{
		x = (a.x + b.x);
		y = (a.y + b.y);
		z = (a.z + b.z);
	}

public:
	vec_t x, y, z;
};

class __declspec(align(16))VectorAligned : public Vector
{
public:
	inline VectorAligned(void) { };

	inline VectorAligned(float x, float y, float z) {
		Init(x, y, z);
	}

	explicit VectorAligned(const Vector& othr) {
		Init(othr.x, othr.y, othr.z);
	}

	VectorAligned& operator=(const Vector& othr) {
		Init(othr.x, othr.y, othr.z);
		return *this;
	}

	vec_t w = 0.0f;
};

class QAngle
{
public:
	// Members
	vec_t x, y, z;

	// Construction/destruction
	QAngle(void) {
        x = y = z = 0.0f;
	}
	QAngle(vec_t X, vec_t Y, vec_t Z);
	//	QAngle(RadianEuler const &angles);	// evil auto type promotion!!!

		// Allow pass-by-value
	//operator QAngleByValue& () { return *((QAngleByValue*)(this)); }
	//operator const QAngleByValue& () const { return *((const QAngleByValue*)(this)); }

	// Initialization
	void Init(vec_t ix = 0.0f, vec_t iy = 0.0f, vec_t iz = 0.0f);
	void Random(vec_t minVal, vec_t maxVal);

	// Got any nasty NAN's?
	bool IsValid() const;
	void Invalidate();

	// array access...
	vec_t operator[](int i) const;
	vec_t& operator[](int i);

	// Base address...
	vec_t* Base();
	vec_t const* Base() const;

	// equality
	bool operator==(const QAngle& v) const;
	bool operator!=(const QAngle& v) const;

	// arithmetic operations
	QAngle& operator+=(const QAngle& v);
	QAngle& operator-=(const QAngle& v);
	QAngle& operator*=(float s);
	QAngle& operator/=(float s);

	// Get the vector's magnitude.
	vec_t	Length() const;
	vec_t	LengthSqr() const;

	// negate the QAngle components
	//void	Negate(); 

	// No assignment operators either...
	QAngle& operator=(const QAngle& src);

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// copy constructors

	// arithmetic operations
	QAngle	operator-(void) const;

	QAngle	operator+(const QAngle& v) const;
	QAngle	operator-(const QAngle& v) const;
	QAngle	operator*(float fl) const;
	QAngle	operator/(float fl) const;
#else

private:
	// No copy constructors allowed if we're in optimal mode
	QAngle(const QAngle& vOther);

#endif
};

inline QAngle& QAngle::operator=(const QAngle& vOther)
{
	//CHECK_VALID(vOther);
	x = vOther.x; y = vOther.y; z = vOther.z;
	return *this;
}

inline vec_t& QAngle::operator[](int i)
{
	assert((i >= 0) && (i < 3));
	return ((vec_t*)this)[i];
}

inline vec_t QAngle::operator[](int i) const
{
	assert((i >= 0) && (i < 3));
	return ((vec_t*)this)[i];
}