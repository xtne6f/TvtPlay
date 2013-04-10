#ifndef TVTEST_STRING_UTILITY_H
#define TVTEST_STRING_UTILITY_H


#include <string>
#include <vector>


LONGLONG StringToInt64(LPCTSTR pszString);
ULONGLONG StringToUInt64(LPCTSTR pszString);
bool Int64ToString(LONGLONG Value,LPTSTR pszString,int MaxLength,int Radix=10);
bool UInt64ToString(ULONGLONG Value,LPTSTR pszString,int MaxLength,int Radix=10);

__declspec(restrict) LPSTR DuplicateString(LPCSTR pszString);
__declspec(restrict) LPWSTR DuplicateString(LPCWSTR pszString);
bool ReplaceString(LPSTR *ppszString,LPCSTR pszNewString);
bool ReplaceString(LPWSTR *ppszString,LPCWSTR pszNewString);
int RemoveTrailingWhitespace(LPTSTR pszString);

inline bool IsStringEmpty(LPCSTR pszString) {
	return pszString==NULL || pszString[0]=='\0';
}
inline bool IsStringEmpty(LPCWSTR pszString) {
	return pszString==NULL || pszString[0]==L'\0';
}
inline LPCSTR NullToEmptyString(LPCSTR pszString) {
	return pszString!=NULL?pszString:"";
}
inline LPCWSTR NullToEmptyString(LPCWSTR pszString) {
	return pszString!=NULL?pszString:L"";
}


namespace TVTest
{

	typedef std::wstring String;
	typedef std::string AnsiString;

	namespace StringUtility
	{

		void Reserve(String &Str,size_t Size);
		int Format(String &Str,LPCWSTR pszFormat, ...);
		int FormatV(String &Str,LPCWSTR pszFormat,va_list Args);
		int CompareNoCase(const String &String1,const String &String2);
		int CompareNoCase(const String &String1,LPCWSTR pszString2);
		int CompareNoCase(const String &String1,const String &String2,String::size_type Length);
		int CompareNoCase(const String &String1,LPCWSTR pszString2,String::size_type Length);
		bool Trim(String &Str,LPCWSTR pszSpaces=L" \t");
		bool Replace(String &Str,LPCWSTR pszFrom,LPCWSTR pszTo);
		bool Replace(String &Str,String::value_type From,String::value_type To);
		bool ToAnsi(const String &Src,AnsiString *pDst);
		bool Split(const String &Src,LPCWSTR pszDelimiter,std::vector<String> *pList);
		bool Combine(const std::vector<String> &List,LPCWSTR pszDelimiter,String *pDst);

	}

}


#endif
