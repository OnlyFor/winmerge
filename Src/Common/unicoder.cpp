/**
 *  @file   unicoder.cpp
 *  @author Perry Rapp, Creator, 2003-2006
 *  @date   Created: 2003-10
 *  @date   Edited:  2006-02-20 (Perry Rapp)
 *
 *  @brief  Implementation of utility unicode conversion routines
 */
// ID line follows -- this is updated by SVN
// $Id: unicoder.cpp 7024 2009-10-22 18:26:45Z kimmov $

/* The MIT License
Copyright (c) 2003 Perry Rapp
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "StdAfx.h"
#include "unicoder.h"
#include "codepage.h"
#include "Utf8FileDetect.h"
#include <mlang.h>
#ifndef __IMultiLanguage2_INTERFACE_DEFINED__
#error "IMultiLanguage2 is not defined in mlang.h. Please install latest Platform SDK."
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// This is not in older platform sdk versions
#ifndef WC_NO_BEST_FIT_CHARS
#define WC_NO_BEST_FIT_CHARS        0x00000400
#endif

namespace ucr
{

// current OS version
static bool f_osvi_fetched = false;
static OSVERSIONINFO f_osvi;

extern "C" typedef HRESULT (__stdcall *DllGetClassObjectFunc)(const CLSID &,  const IID &, void **); 

class CExconverterMLang: public IExconverter {
private:
	IMultiLanguage2 *m_pmlang;
	HMODULE m_hLibMLang;
	DWORD m_mlangcookie;
#ifdef USEUNIVERSALCHARDET
	HMODULE m_hLibCharGuess;
	int (*m_pfnCharGuessInit)(void);
	int (*m_pfnCharGuessDone)(void);
	const char* (*m_pfnGuessChardet)(const char *str);
#endif

public:
	CExconverterMLang()
	: m_pmlang(NULL)
	, m_hLibMLang(NULL)
	, m_mlangcookie(0)
	{
	}

	~CExconverterMLang()
	{
		if (m_pmlang)
			m_pmlang->Release();
		if (m_hLibMLang)
			FreeLibrary(m_hLibMLang);
	}

	bool initialize()
	{
		DllGetClassObjectFunc pfnDllGetClassObject = NULL;
		IClassFactory *pClassFactory = NULL;

		m_hLibMLang = LoadLibrary(_T("mlang.dll"));
		if (m_hLibMLang)
		{
			pfnDllGetClassObject = (DllGetClassObjectFunc)GetProcAddress(m_hLibMLang, "DllGetClassObject");
			if (pfnDllGetClassObject)
			{
				HRESULT hr = pfnDllGetClassObject(CLSID_CMultiLanguage, IID_IClassFactory, (void**)&pClassFactory);
				if (pClassFactory)
				{
					hr = pClassFactory->CreateInstance(NULL, IID_IMultiLanguage2, (void**)&m_pmlang);
					if (SUCCEEDED(hr))
					{
						pClassFactory->Release();
						return true;
					}
				}
			}
		}
		if (pClassFactory)
			pClassFactory->Release();
		if (m_hLibMLang)
		{
			FreeLibrary(m_hLibMLang);
			m_hLibMLang = NULL;
		}
		return false;
	}

	bool convert(int srcCodepage, int dstCodepage, const unsigned char * src, int * srcbytes, unsigned char * dest, int * destbytes)
	{
		bool bsucceeded;
		int wsize = *srcbytes * 2 + 6;
		wchar_t *pbuf = new wchar_t[wsize];
		bsucceeded = convertToUnicode(srcCodepage, (const char *)src, srcbytes, pbuf, &wsize);
		if (!bsucceeded)
		{
			delete pbuf;
			destbytes = 0;
			return false;
		}
		bsucceeded = convertFromUnicode(dstCodepage, pbuf, &wsize, (char *)dest, destbytes);
		delete pbuf;
		return bsucceeded;
	}

	bool convertFromUnicode(int dstCodepage, const wchar_t * src, int * srcchars, char * dest, int *destbytes)
	{
		HRESULT hr = m_pmlang->ConvertStringFromUnicode(&m_mlangcookie, dstCodepage, (wchar_t *)src, (UINT *)srcchars, (char *)dest, (UINT *)destbytes);
		return SUCCEEDED(hr) ? true : false;
	}

	bool convertToUnicode(int srcCodepage, const char * src, int * srcbytes, wchar_t * dest, int *destchars)
	{
		HRESULT hr = m_pmlang->ConvertStringToUnicode(&m_mlangcookie, srcCodepage, (char *)src, (UINT *)srcbytes, dest, (UINT *)destchars);
		return SUCCEEDED(hr) ? true : false;
	}

	int detectInputCodepage(int autodetectType, int defcodepage, const char *data, int size)
	{
		int codepage;
		IMLangConvertCharset *pcc;
		unsigned char *pdst;
		UINT dstsize;
		UINT srcsize;
		HRESULT hr;
		USES_CONVERSION;

		hr = m_pmlang->CreateConvertCharset(autodetectType, 1200, MLCONVCHARF_AUTODETECT, &pcc);
		if (FAILED(hr))
			return defcodepage;
		srcsize = size;
		dstsize = size * sizeof(wchar_t);
		pdst = new unsigned char[size * sizeof(wchar_t)];
		SetLastError(0);
		hr = pcc->DoConversion((unsigned char *)data, &srcsize, pdst, &dstsize);
		pcc->GetSourceCodePage((unsigned int *)&codepage);
		if (FAILED(hr) || GetLastError() == ERROR_NO_UNICODE_TRANSLATION || codepage == autodetectType)
		{
			int codepagestotry[3] = {0};
			if (codepage == autodetectType)
			{
				if (size < 2 || (data[0] != 0 && data[1] != 0))
				{
					codepagestotry[0] = defcodepage;
					codepagestotry[1] = 65001/*UTF-8*/;
				}
			}
			else
			{
				if (size < 2 || (data[0] != 0 && data[1] != 0))
					codepagestotry[0] = 65001/*UTF-8*/;
			}
			codepage = defcodepage;
			int i;
			for (i = 0; i < countof(codepagestotry); i++)
			{
				if (codepagestotry[i] == 0) break;
				hr = pcc->Initialize(codepagestotry[i], 1200, 0);
				dstsize = size * sizeof(wchar_t);
				SetLastError(0);
				hr = pcc->DoConversion((unsigned char *)data, &srcsize, pdst, &dstsize);
				if (FAILED(hr) || GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
					continue;
				codepage = codepagestotry[i];
				break;
			}
			if (codepagestotry[i] == 0 && (size % 2) == 0)
			{
				// UCS-2
				int lezerocount = 0;
				int lecrorlf = 0;
				int bezerocount = 0;
				int becrorlf = 0;
				for (i = 0; i < size; i += 2)
				{
					if (data[i] == 0)
					{
						bezerocount++;
						if (data[i + 1] == 0x0a || data[i + 1] == 0x0d)
							lecrorlf++;
					}
					else if (data[i + 1] == 0)
					{
						lezerocount++;
						if (data[i] == 0x0a || data[i] == 0x0d)
							lecrorlf++;
					}
				}
				if (lezerocount > 0 || bezerocount > 0)
				{
					if ((lecrorlf == 0 && size < 512 || (lecrorlf > 0 && (size / lecrorlf > 1024))) && lezerocount > bezerocount)
						codepage = 1200;
					else if ((becrorlf == 0 && size < 512 || (becrorlf > 0 && (size / becrorlf > 1024))) && lezerocount < bezerocount)
						codepage = 1201;
				}
			}
			delete pdst;
		}
		else
			delete pdst;
		if (codepage == 20127)
			return defcodepage;
		return codepage;
	}

	int enumCodePages(CodePageInfo *cpinfo, int count)
	{
		IEnumCodePage *pEnumCodePage = NULL;
		PMIMECPINFO pcpInfo;
		ULONG ccpInfo;
		HRESULT hr = m_pmlang->EnumCodePages(MIMECONTF_SAVABLE_BROWSER | MIMECONTF_VALID | MIMECONTF_VALID_NLS, 0, &pEnumCodePage);
		if (FAILED(hr))
			return 0;
		pcpInfo = (PMIMECPINFO)CoTaskMemAlloc(sizeof(MIMECPINFO) * count);
		pEnumCodePage->Next(count, pcpInfo, &ccpInfo);
		CoTaskMemRealloc((void*)pcpInfo, sizeof(MIMECPINFO)*ccpInfo);

		for (int i = 0; i < (int)ccpInfo; i++)
		{
			cpinfo[i].codepage = pcpInfo[i].uiCodePage;
			lstrcpyW(cpinfo[i].desc, pcpInfo[i].wszDescription);
		}

		return ccpInfo;
	}

	bool getCodepageFromCharsetName(LPCTSTR pszCharsetName, int *pCodepage)
	{
		MIMECSETINFO charsetInfo;
#ifdef _UNICODE
		BSTR bstrCharsetName = SysAllocString(pszCharsetName);
#else
		wchar_t szCharsetNameW[256];
		MultiByteToWideChar(CP_ACP, 0, pszCharsetName, -1, szCharsetNameW, sizeof(szCharsetNameW)/sizeof(wchar_t));
		BSTR bstrCharsetName = SysAllocString(szCharsetNameW);
#endif
		HRESULT hr = m_pmlang->GetCharsetInfo(bstrCharsetName, &charsetInfo);
		SysFreeString(bstrCharsetName);
		if (FAILED(hr))
			return false;
		*pCodepage = charsetInfo.uiInternetEncoding;
		return true;
	}

	bool getCodepageDescription(int codepage, LPTSTR pszDescription)
	{
		wchar_t szDescription[256];
		HRESULT hr = m_pmlang->GetCodePageDescription(codepage, GetSystemDefaultLangID(), szDescription, sizeof(szDescription)/sizeof(wchar_t));
		if (FAILED(hr))
			return false;

#ifdef _UNICODE
		lstrcpy(pszDescription, szDescription);
#else
		wsprintf(pszDescription, "%S", szDescription);
#endif
		return true;
	}

	bool isValidCodepage(int codepage)
	{
		TCHAR szDesc[256];
		return getCodepageDescription(codepage, szDesc);
	}

	bool getCodePageInfo(int codepage, CodePageInfo *pCodePageInfo)
	{
		MIMECPINFO mcpi = {0};
		HRESULT hr = m_pmlang->GetCodePageInfo(codepage, GetSystemDefaultLangID(), &mcpi);
		if (FAILED(hr))
			return false;
		wcscpy(pCodePageInfo->fixedWidthFont, mcpi.wszFixedWidthFont);
		pCodePageInfo->bGDICharset = mcpi.bGDICharset;
		return true;
	}

};

__declspec(thread) static IExconverter *m_pexconv = NULL;

IExconverter *createConverterMLang()
{
	CExconverterMLang *pexconv = new CExconverterMLang();
	if (!pexconv->initialize())
	{
		delete pexconv;
		return NULL;
	}
	return pexconv;
}

/**
 * @brief fetch current OS version into file level variable & set flag
 */
static void fetch_verinfo()
{
	memset(&f_osvi, 0, sizeof(f_osvi));
	f_osvi.dwOSVersionInfoSize = sizeof(f_osvi);
	GetVersionEx(&f_osvi);
	f_osvi_fetched = true;
}

/**
 * @brief Convert unicode codepoint to UTF-8 byte string
 *
 * utf8 must be a 7+ byte buffer
 * returns length of byte string written
 * Does not zero-terminate!
 */
int Ucs4_to_Utf8(unsigned int unich, unsigned char * utf8)
{
#pragma warning(disable: 4244) // possible loss of data due to type conversion
	if (unich <= 0x7f)
	{
		utf8[0] = (unsigned char)unich;
		return 1;
	}
	else if (unich <= 0x7ff)
	{
		utf8[0] = 0xc0 + (unich >> 6);
		utf8[1] = 0x80 + (unich & 0x3f);
		return 2;
	}
	else if (unich <= 0xffff)
	{
		utf8[0] = 0xe0 + (unich >> 12);
		utf8[1] = 0x80 + ((unich >> 6) & 0x3f);
		utf8[2] = 0x80 + (unich & 0x3f);
		return 3;
	}
	else if (unich <= 0x1fffff)
	{
		utf8[0] = 0xf0 + (unich >> 18);
		utf8[1] = 0x80 + ((unich >> 12) & 0x3f);
		utf8[2] = 0x80 + ((unich >> 6) & 0x3f);
		utf8[3] = 0x80 + (unich & 0x3f);
		return 4;
	}
	else if (unich <= 0x3ffffff)
	{
		utf8[0] = 0xf8 + (unich >> 24);
		utf8[1] = 0x80 + ((unich >> 18) & 0x3f);
		utf8[2] = 0x80 + ((unich >> 12) & 0x3f);
		utf8[3] = 0x80 + ((unich >> 6) & 0x3f);
		utf8[4] = 0x80 + (unich & 0x3f);
		return 5;
	}
	else if (unich <= 0x7fffffff)
	{
		utf8[0] = 0xfc + (unich >> 30);
		utf8[1] = 0x80 + ((unich >> 24) & 0x3f);
		utf8[2] = 0x80 + ((unich >> 18) & 0x3f);
		utf8[3] = 0x80 + ((unich >> 12) & 0x3f);
		utf8[4] = 0x80 + ((unich >> 6) & 0x3f);
		utf8[5] = 0x80 + (unich & 0x3f);
		return 6;
	}
	else
	{
		// Invalid Unicode codepoint (high bit was set)
		// TODO: What do we do ?
		utf8[0] = '?';
		return 1;
	}
#pragma warning(default: 4244) // possible loss of data due to type conversion
}

/**
 * @brief Gets a length of UTF-8 character in bytes.
 * @param [in] ch The character for which to get the length.
 * @return Byte length of UTF-8 character, -1 if invalid.
 */
int Utf8len_fromLeadByte(unsigned char ch)
{
	if (ch < 0x80) return 1;
	if (ch < 0xC0) return -1;
	if (ch < 0xE0) return 2;
	if (ch < 0xF0) return 3;
	if (ch < 0xF8) return 4;
	if (ch < 0xFC) return 5;
	if (ch < 0xFE) return 6;
	return -1;
}

/**
 * @brief return #bytes required to represent Unicode codepoint as UTF-8
 */
int Utf8len_fromCodepoint(unsigned int ch)
{
	if (ch <= 0x7F) return 1;
	if (ch <= 0x7FF) return 2;
	if (ch <= 0xFFFF) return 3;
	if (ch <= 0x1FFFFF) return 4;
	if (ch <= 0x3FFFFFF) return 5;
	if (ch <= 0x7FFFFFFF) return 6;
	return -1;
}

/**
 * @brief How many bytes will it take to write string as UTF-8 ?
 *
 * @param size size argument as filemapping are not 0 terminated
 *
 * @bug Fails for files larger than 2gigs
 */
unsigned int Utf8len_of_string(const wchar_t* text, int size)
{
	unsigned int len = 0;
	for (int i = 0; i < size; ++i)
	{
		int chlen = Utf8len_fromCodepoint(text[i]);
		if (chlen < 1) chlen = 1;
		len += chlen;
	}
	return len;
}
/**
 * @brief How many chars in this UTF-8 string ?
 *
 * @param size size argument as filemapping are not 0 terminated
 *
 * @bug Fails for files larger than 2gigs
 */
unsigned int stringlen_of_utf8(const char* text, int size)
{
	unsigned int len = 0;
	for (int i = 0; i < size;)
	{
		int chlen = Utf8len_fromLeadByte(text[i]);
		if (chlen < 1) chlen = 1;
		i += chlen;
		len ++;
	}
	return len;
}

/**
 * @brief Read UTF-8 character and return as Unicode
 */
unsigned int GetUtf8Char(unsigned char * str)
{
	/* test short cases first, as probably much more common */
	if (!(*str & 0x80 && *str & 0x40))
	{
		return str[0];
	}
	if (!(*str & 0x20))
	{
		unsigned int ch = ((str[0] & 0x1F) << 6)
				+ (str[1] & 0x3F);
		return ch;
	}
	if (!(*str & 0x10))
	{
		unsigned int ch = ((str[0] & 0x0f) << 12)
				+ ((str[1] & 0x3F) << 6)
				+ (str[2] & 0x3F);
		return ch;
	}
	if (!(*str & 0x08))
	{
		unsigned int ch = ((str[0] & 0x0F) << 18)
				+ ((str[1] & 0x3F) << 12)
				+ ((str[2] & 0x3F) << 6)
				+ (str[3] & 0x3F);
		return ch;
	}
	if (!(*str & 0x04))
	{
		unsigned int ch = ((str[0] & 0x0F) << 24)
				+ ((str[1] & 0x3F) << 18)
				+ ((str[2] & 0x3F) << 12)
				+ ((str[3] & 0x3F) << 6)
				+ (str[4] & 0x3F);
		return ch;
	}
	else
	{
		unsigned int ch = ((str[0] & 0x0F) << 30)
				+ ((str[1] & 0x3F) << 24)
				+ ((str[2] & 0x3F) << 18)
				+ ((str[3] & 0x3F) << 12)
				+ ((str[4] & 0x3F) << 6)
				+ (str[5] & 0x3F);
		return ch;
	}
}

/**
 * @brief Write unicode codepoint u out as UTF-8 to lpd, and advance lpd
 *
 * Returns number of bytes written (or -1 for error, in which case it writes '?')
 */
int to_utf8_advance(unsigned int u, unsigned char * &lpd)
{
#pragma warning(disable: 4244) // possible loss of data due to type conversion
	if (u < 0x80)
	{
		*lpd++ = u;
		return 1;
	}
	else if (u < 0x800)
	{
		*lpd++ = 0xC0 + (u >> 6);
		*lpd++ = 0x80 + (u & 0x3F);
		return 2;
	}
	else if (u < 0x10000)
	{
		*lpd++ = 0xE0 + (u >> 12);
		*lpd++ = 0x80 + ((u >> 6) & 0x3F);
		*lpd++ = 0x80 + (u & 0x3F);
		return 3;
	}
	else if (u < 0x200000)
	{
		*lpd++ = 0xF0 + (u >> 18);
		*lpd++ = 0x80 + ((u >> 12) & 0x3F);
		*lpd++ = 0x80 + ((u >> 6) & 0x3F);
		*lpd++ = 0x80 + (u & 0x3F);
		return 4;
	}
	else if (u < 0x4000000)
	{
		*lpd++ = 0xF8 + (u >> 24);
		*lpd++ = 0x80 + ((u >> 18) & 0x3F);
		*lpd++ = 0x80 + ((u >> 12) & 0x3F);
		*lpd++ = 0x80 + ((u >> 6) & 0x3F);
		*lpd++ = 0x80 + (u & 0x3F);
		return 5;
	}
	else if (u < 0x80000000)
	{
		*lpd++ = 0xFC + (u >> 30);
		*lpd++ = 0x80 + ((u >> 24) & 0x3F);
		*lpd++ = 0x80 + ((u >> 18) & 0x3F);
		*lpd++ = 0x80 + ((u >> 12) & 0x3F);
		*lpd++ = 0x80 + ((u >> 6) & 0x3F);
		*lpd++ = 0x80 + (u & 0x3F);
		return 6;
	}
	else
	{
		*lpd++ = '?';
		return 1;
	}
#pragma warning(default: 4244) // possible loss of data due to type conversion
}

/**
 * @brief convert character passed (Unicode codepoint) to a TCHAR (set lossy flag if imperfect conversion)
 */
void maketchar(String & ch, unsigned int unich, bool & lossy)
{
	static unsigned int codepage = CP_ACP;
	// NB: Windows always draws in CP_ACP, not CP_THREAD_ACP, so we must use CP_ACP as an internal codepage

	maketchar(ch, unich, lossy, codepage);
}

/**
 * @brief convert character passed (Unicode codepoint) to a TCHAR (set lossy flag if imperfect conversion)
 */
void maketchar(String & ch, unsigned int unich, bool & lossy, unsigned int codepage)
{
#ifdef _UNICODE
	if (unich < 0x10000)
	{
		ch = (TCHAR)unich;
		return;
	}
	lossy = TRUE;
	ch = '?';
	return;
#else
	if (unich < 0x80)
	{
		ch = (TCHAR)unich;
		return;
	}
	wchar_t wch = (wchar_t)unich;
	if (!lossy)
	{
		static bool vercheck = false;
		static bool has_no_best_fit = false;
		if (!vercheck)
		{
			if (!f_osvi_fetched) fetch_verinfo();
			// Need 2000 (5.x) or 98 (4.10)
			has_no_best_fit = f_osvi.dwMajorVersion >= 5 || (f_osvi.dwMajorVersion == 4 && f_osvi.dwMinorVersion >= 10);
			vercheck = true;
		}
		// So far it isn't lossy, so try for lossless conversion
		char outch[3] = {0};
		BOOL defaulted = FALSE;
		DWORD flags = has_no_best_fit ? WC_NO_BEST_FIT_CHARS : 0;
		if (WideCharToMultiByte(codepage, flags, &wch, 1, outch, sizeof(outch), NULL, &defaulted)
				&& !defaulted)
		{
			ch = outch;
			return;
		}
		lossy = TRUE;
	}
	// already lossy, so make our best shot
	DWORD flags = WC_COMPOSITECHECK + WC_DISCARDNS + WC_SEPCHARS + WC_DEFAULTCHAR;
	TCHAR outbuff[16];
	int n = WideCharToMultiByte(codepage, flags, &wch, 1, outbuff, sizeof(outbuff) - 1, NULL, NULL);
	if (n > 0)
	{
		outbuff[n] = 0;
		ch = outbuff;
		return;
	}
	ch = _T("?");
#endif
}

/**
 * @brief convert 8-bit character input to Unicode codepoint and return it
 */
unsigned int byteToUnicode(unsigned char ch)
{
	static unsigned int codepage = CP_ACP;
	// NB: Windows always draws in CP_ACP, not CP_THREAD_ACP, so we must use CP_ACP as an internal codepage

	return byteToUnicode(ch, codepage);
}

/**
 * @brief convert 8-bit character input to Unicode codepoint and return it
 */
unsigned int byteToUnicode(unsigned char ch, unsigned int codepage)
{

	if (ch < 0x80)
		return ch;

	DWORD flags = 0;
	wchar_t wbuff;
	int n = MultiByteToWideChar(codepage, flags, (const char*) & ch, 1, &wbuff, 1);
	if (n > 0)
		return wbuff;
	else
		return '?';
}

/**
 * @brief Return encoding used for TCHAR & String
 */
void getInternalEncoding(UNICODESET * unicoding, int * codepage)
{
#ifdef _UNICODE
	*unicoding = UCS2LE;
	*codepage = 0;
#else
	// NB: Windows always draws in CP_ACP, not CP_THREAD_ACP, so we must use CP_ACP as an internal codepage
	*unicoding = NONE;
	*codepage = CP_ACP;
#endif
}

/**
 * @brief Write appropriate BOM (Unicode byte order marker)
 * returns #bytes written
 */
int writeBom(void* dest, UNICODESET unicoding)
{
	unsigned char * lpd = reinterpret_cast<unsigned char *>(dest);
	// write Unicode byte order marker (BOM)
	if (unicoding == UCS2LE)
	{
		*lpd++ = 0xFF;
		*lpd++ = 0xFE;
		return 2;
	}
	else if (unicoding == UCS2BE)
	{
		*lpd++ = 0xFE;
		*lpd++ = 0xFF;
		return 2;
	}
	else if (unicoding == UTF8)
	{
		*lpd++ = 0xEF;
		*lpd++ = 0xBB;
		*lpd++ = 0xBF;
		return 3;
	}
	return 0;
}

/**
 * @brief Extract character from pointer, handling UCS-2 codesets
 *  This does not handle MBCS or UTF-8 codepages correctly!
 *  Client should not use this except for Unicode or SBCS codepages.
 */
unsigned int get_unicode_char(unsigned char * ptr, UNICODESET codeset, int codepage)
{
	unsigned int ch;
	switch (codeset)
	{
	case UCS2LE:
		ch = *((WORD *)ptr);
		break;
	case UCS2BE:
		ch = (ptr[0] << 8) + ptr[1];
		break;
	default:
		// TODO: How do we recognize valid codepage ?
		// if not, use byteToUnicode(*ptr)
		ch = byteToUnicode(*ptr, codepage);
	}
	return ch;
}

/**
 * @brief Convert series of bytes (8-bit chars) to TCHARs.
 *
 * @param [out] str String returned.
 * @param [in] lpd Original byte array to convert.
 * @param [in] len Length of the original byte array.
 * @param [in] codepage Codepage used.
 * @param [out] lossy Was conversion lossy?
 * @return true if conversion succeeds, false otherwise.
 * @todo This doesn't inform the caller whether translation was lossy
 *  In fact, this doesn't even know. Probably going to have to make
 *  two passes, the first with MB_ERR_INVALID_CHARS. Ugh. :(
 */
bool maketstring(String & str, const char* lpd, unsigned int len, int codepage, bool * lossy)
{
	int defcodepage = getDefaultCodepage();

	if (!len)
	{
		str.clear();
		return true;
	}

	// 0 is a valid value (CP_ACP)!
	if (codepage == -1)
		codepage = defcodepage;

#ifdef UNICODE
	// Convert input to Unicode, using specified codepage
	// TCHAR is wchar_t, so convert into String (str)
	DWORD flags = MB_ERR_INVALID_CHARS;
	int wlen = len * 2 + 6;

	try
	{
		str.resize(wlen);
	}
	catch (std::bad_alloc)
	{
		// Not enough memory - exit
		return false;
	}

	LPWSTR wbuff = &*str.begin();
	if (codepage == CP_ACP || IsValidCodePage(codepage))
	{
		int n = MultiByteToWideChar(codepage, flags, lpd, len, wbuff, wlen - 1);
		if (n)
		{
			/*
			NB: MultiByteToWideChar is documented as only zero-terminating
			if input was zero-terminated, but it appears that it can
			zero-terminate even if input wasn't.
			So we check if it zero-terminated and adjust count accordingly.
			*/
			//>2007-01-11 jtuc: We must preserve an embedded zero even if it is
			// the last input character. As we don't expect MultiByteToWideChar to
			// add a zero that does not originate from the input string, it is a
			// good idea to ASSERT that the assumption holds.
			if (wbuff[n-1] == 0 && lpd[len-1] != 0)
			{
				ASSERT(FALSE);
				--n;
			}
			try
			{
				str.resize(n);
			}
			catch (std::bad_alloc)
			{
				// Not enough memory - exit
				return false;
			}
			return true;
		}
		else
		{
			if (GetLastError() == ERROR_INVALID_FLAGS)
			{
				int n = MultiByteToWideChar(codepage, 0, lpd, len, wbuff, wlen-1);
				if (n)
				{
					/* NB: MultiByteToWideChar is documented as only zero-terminating 
					if input was zero-terminated, but it appears that it can 
					zero-terminate even if input wasn't.
					So we check if it zero-terminated and adjust count accordingly.
					*/
					if (wbuff[n-1] == 0 && lpd[len-1] != 0)
					{
						ASSERT(FALSE);
						--n;
					}
					try
					{
						str.resize(n);
					}
					catch (std::bad_alloc)
					{
						// Not enough memory - exit
						return false;
					}
					return true;
				}
			}
			if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
			{
				*lossy = true;
				flags = 0;
				// wlen & wbuff are still fine
				n = MultiByteToWideChar(codepage, flags, lpd, len, wbuff, wlen-1);
				if (n)
				{
					try
					{
						str.resize(n);
					}
					catch (std::bad_alloc)
					{
						// Not enough memory - exit
						return false;
					}
					return true;
				}
			}
			str = _T("?");
			return true;
		}
	}
	else
	{
		if (!m_pexconv)
			m_pexconv = createConverterMLang();
		if (m_pexconv)
		{
			int n = wlen;
			if (m_pexconv->convertToUnicode(codepage, lpd, (int *)&len, wbuff, (int *)&n))
			{
				try
				{
					str.resize(n);
				}
				catch (std::bad_alloc)
				{
					// Not enough memory - exit
					return false;
				}
				return true;
			}
			else
			{
				*lossy = true;
				str = _T("?");
			}
			return true;
		}
		else
		{
			*lossy = true;
			str = _T("?");
		}
		return true;
	}

#else
	int dstcodepage = IsValidCodePage(defcodepage) ? defcodepage : GetACP();

	if (EqualCodepages(codepage, dstcodepage))
	{
		// trivial case, they want the bytes in the file interpreted in our current codepage
		// Only caveat is that input (lpd) is not zero-terminated
		str = String(lpd, len);
		return true;
	}

	if (codepage == CP_ACP || IsValidCodePage(codepage))
	{
		str = CrossConvertToStringA(lpd, len, codepage, dstcodepage, lossy);
		if (*lossy)
			str = _T("?");
		return true;
	}
	else
	{
		if (!m_pexconv)
			m_pexconv = createConverterMLang();
		if (m_pexconv)
		{		
			int n = len * 6 + 6;
			try
			{
				str.resize(n);
			}
			catch (std::bad_alloc)
			{
				// Not enough memory - exit
				return false;
			}
			LPSTR buff = &*str.begin();
			m_pexconv->convert(codepage, dstcodepage, (const unsigned char *)lpd, (int *)&len, (unsigned char *)buff, (int *)&n);
			if (n)
			{
				try
				{
					str.resize(n);
				}
				catch (std::bad_alloc)
				{
					// Not enough memory - exit
					return false;
				}
			}
			else
				str = _T("?");
		}
		else
			str = _T("?");		
		return true;
	}
#endif
}

/**
 * @brief (ANSI build only) Convert from one 8 bit codepage to another
 */
#ifndef UNICODE
String CrossConvertToStringA(const char* src, unsigned int srclen, int cpin, int cpout, bool * lossy)
{
	int wlen = srclen * 2 + 6;
	int clen = wlen * 2 + 6;
	String str;
	str.resize(clen);
	char* cbuff = &*str.begin();
	int nbytes = CrossConvert(src, srclen, cbuff, clen, cpin, cpout, lossy);
	str.resize(nbytes);
	return str;
}
#endif

/**
 * @brief Convert from one 8-bit codepage to another
 *
 * destsize must be at least 2
 */
int CrossConvert(const char* src, unsigned int srclen, char* dest, unsigned int destsize, int cpin, int cpout, bool * lossy)
{
	ASSERT(destsize > 1);

	// Convert input to Unicode, using specified codepage
	DWORD flags = 0;
	int wlen = srclen * 2 + 6;
	wchar_t * wbuff = new wchar_t[wlen];
	int n = MultiByteToWideChar(cpin, flags, (const char*)src, srclen, wbuff, wlen - 1);
	if (!n)
	{
		int nsyserr = ::GetLastError();
		String syserrstr = GetSysError(nsyserr);
		delete [] wbuff;
		dest[0] = '?';
		return 1;
	}
	/*
	NB: MultiByteToWideChar is documented as only zero-terminating
	if input was zero-terminated, but it appears that it can
	zero-terminate even if input wasn't.
	So we check if it zero-terminated and adjust count accordingly.
	*/
	if (wbuff[n-1] == 0)
		--n;
	wbuff[n] = 0; // zero-terminate string

	// Now convert to TCHAR (which means defcodepage)
	flags = WC_NO_BEST_FIT_CHARS; // TODO: Think about this
	wlen = n;
	BOOL defaulted = FALSE;
	BOOL * pdefaulted = &defaulted;
	if (cpout == CP_UTF8)
	{
		flags = 0;
		pdefaulted = NULL;
	}
	n = WideCharToMultiByte(cpout, flags, wbuff, n, dest, destsize - 1, NULL, pdefaulted);
	if (!n)
	{
		int nsyserr = ::GetLastError();
		String syserrstr = GetSysError(nsyserr);
	}
	dest[n] = 0;
	delete [] wbuff;
	if (lossy)
		*lossy = !!defaulted;
	return n;
}

/**
 * @brief Buffer constructor.
 * The constructor creates buffer with given size.
 * @param [in] initialSize Buffer's size.
 */
buffer::buffer(unsigned int initialSize)
{
	size = 0;
	capacity = initialSize;
	ptr = (unsigned char *)calloc(capacity, 1);
}

/**
 * @brief Buffer destructor.
 * Frees the reserved buffer.
 */
buffer::~buffer()
{
	free(ptr);
}

/**
 * @brief Resize the buffer.
 * @param [in] newSize New size of the buffer.
 */
void buffer::resize(unsigned int newSize)
{
	if (capacity < newSize)
	{
		capacity = newSize;
		ptr = (unsigned char *)realloc(ptr, capacity);
	}
}

unsigned char *convertTtoUTF8(buffer * buf, LPCTSTR * src, int srcbytes/* = -1*/)
{
	bool bSucceeded;
#ifdef _UNICODE
	bSucceeded = convert(1200, 
		(unsigned char *)src, (srcbytes < 0) ? wcslen((const wchar_t *)src) : srcbytes,
		CP_UTF8, buf);
#else
	bSucceeded = convert(GetACP(),
		(unsigned char *)src, (srcbytes < 0) ? strlen((const char *)src) : srcbytes,
		CP_UTF8, buf);
#endif
	if (!bSucceeded)
		*((unsigned char *)buf->ptr) = 0;
	return buf->ptr;
}

unsigned char *convertTtoUTF8(LPCTSTR * src, int srcbytes/* = -1*/)
{
	buffer buf(256);
	convertTtoUTF8(&buf, src, srcbytes);
	return (unsigned char *)strdup((const char *)buf.ptr);
}

TCHAR *convertUTF8toT(buffer * buf, LPCSTR * src, int srcbytes/* = -1*/)
{
	bool bSucceeded;
#ifdef _UNICODE
	bSucceeded = convert(CP_UTF8,
		(const unsigned char *)src, (srcbytes < 0) ? strlen((const char *)src) : srcbytes,
		1200/*UCS2LE*/, buf);
#else
	bSucceeded = convert(CP_UTF8,
		(const unsigned char *)src, (srcbytes < 0) ? strlen((const char *)src) : srcbytes,
		GetACP(), buf);
#endif
	if (!bSucceeded)
		*((TCHAR *)buf->ptr) = 0;
	return (TCHAR *)buf->ptr;
}

TCHAR *convertUTF8toT(LPCSTR * src, int srcbytes/* = -1*/)
{
	buffer buf(256);
	convertUTF8toT(&buf, src, srcbytes);
	return (TCHAR *)_tcsdup((LPCTSTR)buf.ptr);
}

void dealloc(void *ptr)
{
	free(ptr);
}

bool convert(int codepage1, const unsigned char * src, int srcbytes, int codepage2, buffer * dest)
{
	UNICODESET unicoding[2];
	int codepage[2] = {codepage1, codepage2};

	int i;
	for (i = 0; i < 2; i++)
	{
		switch (codepage[i])
		{
		case 1200:
			unicoding[i] = UCS2LE; break;
		case 1201:
			unicoding[i] = UCS2BE; break;
		case 65001:
			unicoding[i] = UTF8; break;
		default:
			unicoding[i] = NONE; break;
		}
	}

	return convert(unicoding[0], codepage1, src, srcbytes, unicoding[1], codepage2, dest);
}

/**
 * @brief Convert from one text encoding to another; return false if any lossing conversions
 */
bool convert(UNICODESET unicoding1, int codepage1, const unsigned char * src, int srcbytes, UNICODESET unicoding2, int codepage2, buffer * dest)
{
	if (unicoding1 == unicoding2 && (unicoding1 || EqualCodepages(codepage1, codepage2)))
	{
		// simple byte copy
		dest->resize(srcbytes + 2);
		CopyMemory(dest->ptr, src, srcbytes);
		dest->ptr[srcbytes] = 0;
		dest->ptr[srcbytes+1] = 0;
		dest->size = srcbytes;
		return true;
	}
	if ((unicoding1 == UCS2LE && unicoding2 == UCS2BE)
			|| (unicoding1 == UCS2BE && unicoding2 == UCS2LE))
	{
		// simple byte swap
		dest->resize(srcbytes + 2);
		for (int i = 0; i < srcbytes; i += 2)
		{
			// Byte-swap into destination
			dest->ptr[i] = src[i+1];
			dest->ptr[i+1] = src[i];
		}
		dest->ptr[srcbytes] = 0;
		dest->ptr[srcbytes+1] = 0;
		dest->size = srcbytes;
		return true;
	}
	if (unicoding1 != UCS2LE && unicoding2 != UCS2LE)
	{
		// Break problem into two simpler pieces by converting through UCS-2LE
		buffer intermed(dest->capacity + 2);
		bool step1 = convert(unicoding1, codepage1, src, srcbytes, UCS2LE, 0, &intermed);
		bool step2 = convert(UCS2LE, 0, intermed.ptr, intermed.size, unicoding2, codepage2, dest);
		return step1 && step2;
	}
	if (unicoding1 == UCS2LE)
	{
		// From UCS-2LE to 8-bit (or UTF-8)

		// WideCharToMultiByte: lpDefaultChar & lpUsedDefaultChar must be NULL when using UTF-8

		int destcp = (unicoding2 == UTF8 ? CP_UTF8 : codepage2);
		if (destcp == CP_ACP || IsValidCodePage(destcp))
		{
			DWORD flags = 0;
			int bytes = WideCharToMultiByte(destcp, flags, (LPCWSTR)src, srcbytes/2, 0, 0, NULL, NULL);
			dest->resize(bytes + 2);
			int losses = 0;
			bytes = WideCharToMultiByte(destcp, flags, (LPCWSTR)src, srcbytes/2, (char *)dest->ptr, dest->capacity, NULL, NULL);
			dest->ptr[bytes] = 0;
			dest->ptr[bytes+1] = 0;
			dest->size = bytes;
			return losses==0;
		}
		else
		{
			int srcsize = srcbytes / 2;
			int dstsize = srcbytes * 6; 
			dest->resize(dstsize + 2);
			if (!m_pexconv)
				m_pexconv = createConverterMLang();
			if (m_pexconv)
			{
				bool result = m_pexconv->convertFromUnicode(destcp, (LPWSTR)src, &srcsize, (char *)dest->ptr, &dstsize);
				dest->ptr[dstsize] = 0;
				dest->ptr[dstsize+1] = 0;
				dest->size = dstsize;
				return result;
			}
			else
				return false;
		}
	}
	else
	{
		// From 8-bit (or UTF-8) to UCS-2LE
		int srccp = (unicoding1 == UTF8 ? CP_UTF8 : codepage1);
		if (srccp == CP_ACP || IsValidCodePage(srccp))
		{
			DWORD flags = 0;
			int wchars = MultiByteToWideChar(srccp, flags, (LPCSTR)src, srcbytes, 0, 0);
			dest->resize((wchars + 1) *2);
			wchars = MultiByteToWideChar(srccp, flags, (LPCSTR)src, srcbytes, (LPWSTR)dest->ptr, dest->capacity/2);
			dest->ptr[wchars * 2] = 0;
			dest->ptr[wchars * 2 + 1] = 0;
			dest->size = wchars * 2;
			return true;
		}
		else
		{
			int srcsize = srcbytes;
			int dstsize = srcbytes; 
			dest->resize((srcbytes + 1) * sizeof(wchar_t));
			if (!m_pexconv)
				m_pexconv = createConverterMLang();
			if (m_pexconv)
			{
				bool result = m_pexconv->convertToUnicode(srccp, (LPCSTR)src, &srcsize, (LPWSTR)dest->ptr, &dstsize);
				dest->ptr[dstsize * sizeof(wchar_t)] = 0;
				dest->ptr[dstsize * sizeof(wchar_t) + 1] = 0;
				dest->size = dstsize * sizeof(wchar_t);
				return result;
			}
			else
				return false;
		}
	}
}

/**
 * @brief Determine encoding from byte buffer.
 * @param [in] pBuffer Pointer to the begin of the buffer.
 * @param [in] size Size of the buffer.
 * @param [out] pBom Returns true if buffer had BOM bytes, false otherwise.
 * @return One of UNICODESET values as encoding.
 * EF BB BF UTF-8
 * FF FE UTF-16, little endian
 * FE FF UTF-16, big endian
 * FF FE 00 00 UTF-32, little endian
 * 00 00 FE FF UTF-32, big-endian
 */
UNICODESET DetermineEncoding(PBYTE pBuffer, __int64 size, bool * pBom)
{
	UNICODESET unicoding = ucr::NONE;
	*pBom = false;

	if (size >= 2)
	{
		if (pBuffer[0] == 0xFF && pBuffer[1] == 0xFE)
		{
			unicoding = ucr::UCS2LE; //UNI little endian
			*pBom = true;
		}
		else if (pBuffer[0] == 0xFE && pBuffer[1] == 0xFF)
		{
			unicoding = ucr::UCS2BE; //UNI big endian
			*pBom = true;
		}
	}
	if (size >= 3)
	{
		if (pBuffer[0] == 0xEF && pBuffer[1] == 0xBB && pBuffer[2] == 0xBF)
		{
			unicoding = ucr::UTF8;
			*pBom = true;
		}
	}
	if (size >= 4)
	{
		if (pBuffer[0] == 0xFF && pBuffer[1] == 0xFE &&
				pBuffer[2] == 0x00 && pBuffer[3] == 0x00)
		{
			unicoding = ucr::UCS4LE; //UTF-32, little endian
			*pBom = true;
		}
		else if (pBuffer[0] == 0x00 && pBuffer[1] == 0x00 &&
				pBuffer[2] == 0xFE && pBuffer[3] == 0xFF)
		{
			unicoding = ucr::UCS4BE; //UTF-32, big endian
			*pBom = true;
		}
	}

	return unicoding;
}

} // namespace ucr

/**
 * @brief Change any special codepage constants into real codepage numbers
 */
static int NormalizeCodepage(int cp)
{
	if (cp == CP_THREAD_ACP) // should only happen on Win2000+
	{
		TCHAR buff[32];
		if (GetLocaleInfo(GetThreadLocale(), LOCALE_IDEFAULTANSICODEPAGE, buff, sizeof(buff) / sizeof(buff[0])))
			cp = _ttol(buff);
		else
			// a valid codepage is better than no codepage
			cp = GetACP();
	}
	if (cp == CP_ACP) cp = GetACP();
	if (cp == CP_OEMCP) cp = GetOEMCP();
	return cp;
}

/**
 * @brief Compare two codepages for equality
 */
bool EqualCodepages(int cp1, int cp2)
{
	return (cp1 == cp2)
			|| (NormalizeCodepage(cp1) == NormalizeCodepage(cp2));
}
