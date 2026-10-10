/**************************************************************************

	asan_shims.h

	Force-included only in AddressSanitizer builds (-p:EnableASAN=true).

	lstrcpy, lstrcat, lstrcpyn, lstrlen and wsprintf are implemented in
	kernel32/user32, which are not instrumented, so ASan cannot see buffer
	overflows made through them. In ASan builds they are replaced by the
	functions below, which are compiled with instrumentation (memcpy and
	the byte loops are checked).

**************************************************************************/

#ifndef WWWC_ASAN_SHIMS_H
#define WWWC_ASAN_SHIMS_H

/* C sources only (win32-darkmodelib and DarkMode.cpp keep their own includes) */
#ifndef __cplusplus

#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <string.h>

static __inline int asan_lstrlen(LPCSTR s)
{
	const char *p = s;

	if(s == NULL){
		return 0;
	}
	while(*p != '\0'){
		p++;
	}
	return (int)(p - s);
}

static __inline LPSTR asan_lstrcpy(LPSTR d, LPCSTR s)
{
	memcpy(d, s, asan_lstrlen(s) + 1);
	return d;
}

static __inline LPSTR asan_lstrcat(LPSTR d, LPCSTR s)
{
	memcpy(d + asan_lstrlen(d), s, asan_lstrlen(s) + 1);
	return d;
}

static __inline LPSTR asan_lstrcpyn(LPSTR d, LPCSTR s, int n)
{
	int i;

	if(n <= 0){
		return d;
	}
	for(i = 0; i < n - 1 && s[i] != '\0'; i++){
		d[i] = s[i];
	}
	d[i] = '\0';
	return d;
}

static __inline int asan_wsprintf(LPSTR d, LPCSTR fmt, ...)
{
	char tmp[1025];			/* wsprintf writes at most 1024 characters */
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = wvsprintfA(tmp, fmt, ap);
	va_end(ap);
	if(n < 0){
		n = 0;
	}
	tmp[n] = '\0';
	memcpy(d, tmp, n + 1);
	return n;
}

#undef lstrlen
#undef lstrcpy
#undef lstrcat
#undef lstrcpyn
#undef wsprintf
#define lstrlen			asan_lstrlen
#define lstrlenA		asan_lstrlen
#define lstrcpy			asan_lstrcpy
#define lstrcpyA		asan_lstrcpy
#define lstrcat			asan_lstrcat
#define lstrcatA		asan_lstrcat
#define lstrcpyn		asan_lstrcpyn
#define lstrcpynA		asan_lstrcpyn
#define wsprintf		asan_wsprintf
#define wsprintfA		asan_wsprintf

#endif	/* __cplusplus */
#endif
