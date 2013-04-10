#ifndef INCLUDE_BREGDEF_H
#define INCLUDE_BREGDEF_H

#include <stddef.h>

#define BREGCONST	const


#ifdef UNICODE
#define BMatch		BMatchW
#define BSubst		BSubstW
#define BMatchEx	BMatchExW
#define BSubstEx	BSubstExW
#define BTrans		BTransW
#define BSplit		BSplitW
#define BRegfree	BRegfreeW
#define BRegexpVersion	BRegexpVersionW
#define BoMatch		BoMatchW
#define BoSubst		BoSubstW
#define BREG_NAME_SUFFIX	"W"
#else
#define BREG_NAME_SUFFIX	""
#endif

#define BMatch_NAME		"BMatch" BREG_NAME_SUFFIX
#define BSubst_NAME		"BSubst" BREG_NAME_SUFFIX
#define BMatchEx_NAME	"BMatchEx" BREG_NAME_SUFFIX
#define BSubstEx_NAME	"BSubstEx" BREG_NAME_SUFFIX
#define BTrans_NAME		"BTrans" BREG_NAME_SUFFIX
#define BSplit_NAME		"BSplit" BREG_NAME_SUFFIX
#define BRegfree_NAME	"BRegfree" BREG_NAME_SUFFIX
#define BRegexpVersion_NAME	"BRegexpVersion" BREG_NAME_SUFFIX
#define BoMatch_NAME	"BoMatch" BREG_NAME_SUFFIX
#define BoSubst_NAME	"BoSubst" BREG_NAME_SUFFIX

#define BREGEXP_MAX_ERROR_MESSAGE_LEN	80


typedef struct bregexp {
	BREGCONST TCHAR *outp;		/* result string start ptr  */
	BREGCONST TCHAR *outendp;	/* result string end ptr    */
	int splitctr;				/* split result counter     */
	BREGCONST TCHAR **splitp;	/* split result pointer ptr     */
	INT_PTR rsv1;				/* reserved for external use    */
	TCHAR *parap;				/* parameter start ptr ie. "s/xxxxx/yy/gi"  */
	TCHAR *paraendp;			/* parameter end ptr     */
	TCHAR *transtblp;			/* translate table ptr   */
	TCHAR **startp;				/* match string start ptr   */
	TCHAR **endp;				/* match string end ptr     */
	int nparens;				/* number of parentheses */
} BREGEXP;

typedef BOOL (__stdcall *BCallBack)(int kind, int value, ptrdiff_t index);

#ifdef _K2REGEXP_
/* K2Editor */
typedef
int (__cdecl BMatch)(TCHAR *str, TCHAR *target, TCHAR *targetstartp, TCHAR *targetendp,
		int one_shot,
		BREGEXP **rxp, TCHAR *msg);
typedef
int (__cdecl BSubst)(TCHAR *str, TCHAR *target, TCHAR *targetstartp, TCHAR *targetendp,
		BREGEXP **rxp, TCHAR *msg, BCallBack callback);
#else
/* Original */
typedef
int (__cdecl BMatch)(TCHAR *str, TCHAR *target, TCHAR *targetendp,
		BREGEXP **rxp, TCHAR *msg);
typedef
int (__cdecl BSubst)(TCHAR *str, TCHAR *target, TCHAR *targetendp,
		BREGEXP **rxp, TCHAR *msg);

/* Sakura Editor */
typedef
int (__cdecl BMatchEx)(TCHAR *str, TCHAR *targetbegp, TCHAR *target, TCHAR *targetendp,
		BREGEXP **rxp, TCHAR *msg);
typedef
int (__cdecl BSubstEx)(TCHAR *str, TCHAR *targetbegp, TCHAR *target, TCHAR *targetendp,
		BREGEXP **rxp, TCHAR *msg);
#endif


typedef
int (__cdecl BTrans)(TCHAR *str, TCHAR *target, TCHAR *targetendp,
		BREGEXP **rxp, TCHAR *msg);
typedef
int (__cdecl BSplit)(TCHAR *str, TCHAR *target, TCHAR *targetendp,
		int limit, BREGEXP **rxp, TCHAR *msg);
typedef
void (__cdecl BRegfree)(BREGEXP *rx);

typedef
TCHAR *(__cdecl BRegexpVersion)(void);


#ifndef _K2REGEXP_
/* bregonig.dll native APIs */

typedef
int (__cdecl BoMatch)(const TCHAR *patternp, const TCHAR *optionp,
		const TCHAR *strstartp,
		const TCHAR *targetstartp, const TCHAR *targetendp,
		BOOL one_shot,
		BREGEXP **rxp, TCHAR *msg);

typedef
int (__cdecl BoSubst)(const TCHAR *patternp, const TCHAR *substp, const TCHAR *optionp,
		const TCHAR *strstartp,
		const TCHAR *targetstartp, const TCHAR *targetendp,
		BCallBack callback,
		BREGEXP **rxp, TCHAR *msg);

#endif

#endif // INCLUDE_BREGDEF_H
