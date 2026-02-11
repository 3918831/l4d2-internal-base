#pragma once

#include <string>

// Simple CUtlString class for compatibility with Source SDK
class CUtlString
{
public:
	CUtlString() : m_str() {}
	CUtlString(const char* pStr) : m_str(pStr ? pStr : "") {}
	CUtlString(const CUtlString& other) : m_str(other.m_str) {}
	CUtlString(const std::string& str) : m_str(str) {}

	// Conversion operators
	operator const char*() const { return m_str.c_str(); }
	operator std::string&() { return m_str; }
	operator const std::string&() const { return m_str; }

	// Assignment
	CUtlString& operator=(const CUtlString& other) {
		m_str = other.m_str;
		return *this;
	}
	CUtlString& operator=(const char* pStr) {
		m_str = pStr ? pStr : "";
		return *this;
	}

	// Access
	const char* Get() const { return m_str.c_str(); }
	const char* String() const { return m_str.c_str(); }
	bool IsEmpty() const { return m_str.empty(); }
	int Length() const { return static_cast<int>(m_str.length()); }

	// Comparison
	bool operator==(const CUtlString& other) const { return m_str == other.m_str; }
	bool operator!=(const CUtlString& other) const { return m_str != other.m_str; }
	bool operator<(const CUtlString& other) const { return m_str < other.m_str; }

private:
	std::string m_str;
};
