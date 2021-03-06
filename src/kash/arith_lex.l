%option never-interactive
%option noyywrap
%option noinput
%option nounput
%option noyyget_out
%option noyy_push_state
%option noyy_pop_state
%option noyy_top_state
%option noyy_scan_buffer
%option noyy_scan_bytes
%option noyy_scan_string
%option noyyget_extra
%option noyyset_extra
%option noyyget_leng
%option noyyget_text
%option noyyget_lineno
%option noyyset_lineno
%option noyyget_in
%option noyyset_in
%option noyyget_out
%option noyyset_out
%option noyyget_lval
%option noyyset_lval
%option noyyget_lloc
%option noyyset_lloc
%option noyyget_debug
%option noyyset_debug
%option noyyalloc
%option noyyrealloc
%option noyyfree
/** @todo %option reentrant */
%{
/*	$NetBSD: arith_lex.l,v 1.13 2005/03/21 22:37:09 dsl Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)arith_lex.l	8.3 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: arith_lex.l,v 1.13 2005/03/21 22:37:09 dsl Exp $");
#endif /* not lint */
#endif

#include <stdio.h>
#include "arith.h"
#include "error.h"
#include "expand.h"
#include "var.h"
#include "shinstance.h"

extern int yylval;
extern shinstance *arith_psh;
extern char *arith_buf, *arith_startbuf;
#undef YY_INPUT
#define YY_INPUT(buf,result,max) \
	result = (*buf = *arith_buf++) ? 1 : YY_NULL;
#define YY_NO_UNPUT

/* Avoid unnecessary libc bits. */
#undef  ECHO
#define ECHO \
	do {} while (0)
#undef  stdin
#define stdin  \
	NULL
#undef  stdout
#define stdout \
	NULL
#undef  fprintf
#define fprintf(a, b, c) \
	((void)0)
#undef  exit
#define exit(rc) \
	do {} while (0)
#define YY_FATAL_ERROR(msg) \
	error(arith_psh, "arith: fatal error: %s", msg)
%}

%%
[ \t\n]	{ ; }
0x[0-9a-fA-F]+	{ yylval = strtol(yytext, 0, 0); return(ARITH_NUM); }
0[0-7]*		{ yylval = strtol(yytext, 0, 0); return(ARITH_NUM); }
[1-9][0-9]*	{ yylval = strtol(yytext, 0, 0); return(ARITH_NUM); }
[A-Za-z_][A-Za-z_0-9]*	{ char *v = lookupvar(arith_psh, yytext);
			if (v) {
				yylval = strtol(v, &v, 0);
				if (*v == 0)
					return ARITH_NUM;
			}
			error(arith_psh, "arith: syntax error: \"%s\"", arith_startbuf);
		}
"("	{ return(ARITH_LPAREN); }
")"	{ return(ARITH_RPAREN); }
"||"	{ return(ARITH_OR); }
"&&"	{ return(ARITH_AND); }
"|"	{ return(ARITH_BOR); }
"^"	{ return(ARITH_BXOR); }
"&"	{ return(ARITH_BAND); }
"=="	{ return(ARITH_EQ); }
"!="	{ return(ARITH_NE); }
">"	{ return(ARITH_GT); }
">="	{ return(ARITH_GE); }
"<"	{ return(ARITH_LT); }
"<="	{ return(ARITH_LE); }
"<<"	{ return(ARITH_LSHIFT); }
">>"	{ return(ARITH_RSHIFT); }
"*"	{ return(ARITH_MUL); }
"/"	{ return(ARITH_DIV); }
"%"	{ return(ARITH_REM); }
"+"	{ return(ARITH_ADD); }
"-"	{ return(ARITH_SUB); }
"~"	{ return(ARITH_BNOT); }
"!"	{ return(ARITH_NOT); }
.	{ error(arith_psh, "arith: syntax error: \"%s\"", arith_startbuf); }
%%

void
arith_lex_reset() {
#ifdef YY_NEW_FILE
	YY_NEW_FILE;
#endif
}

void *
yyalloc(yy_size_t cb)
{
	return sh_malloc(NULL, cb);
}

void *
yyrealloc(void *pv, yy_size_t cb)
{
	return sh_realloc(NULL, pv, cb);
}

void
yyfree(void *pv)
{
	sh_free(NULL, pv);
}

