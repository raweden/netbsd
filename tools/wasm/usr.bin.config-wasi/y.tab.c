/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "gram.y"

/*	$NetBSD: gram.y,v 1.56 2020/07/26 22:40:52 uwe Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
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
 *
 *	from: @(#)gram.y	8.1 (Berkeley) 6/6/93
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: gram.y,v 1.56 2020/07/26 22:40:52 uwe Exp $");

#include <sys/types.h>
#include <sys/param.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "defs.h"
#include "sem.h"

#define	FORMAT(n) (((n).fmt == 8 && (n).val != 0) ? "0%llo" : \
    ((n).fmt == 16) ? "0x%llx" : "%lld")

#define	stop(s)	cfgerror(s), exit(1)

static	struct	config conf;	/* at most one active at a time */
static	int	nowarn;		/* if warning suppression is on */


/*
 * Allocation wrapper functions
 */
static void wrap_alloc(void *ptr, unsigned code);
static void wrap_continue(void);
static void wrap_cleanup(void);

/*
 * Allocation wrapper type codes
 */
#define WRAP_CODE_nvlist	1
#define WRAP_CODE_defoptlist	2
#define WRAP_CODE_loclist	3
#define WRAP_CODE_attrlist	4
#define WRAP_CODE_condexpr	5

/*
 * The allocation wrappers themselves
 */
#define DECL_ALLOCWRAP(t)	static struct t *wrap_mk_##t(struct t *arg)

DECL_ALLOCWRAP(nvlist);
DECL_ALLOCWRAP(defoptlist);
DECL_ALLOCWRAP(loclist);
DECL_ALLOCWRAP(attrlist);
DECL_ALLOCWRAP(condexpr);

/* allow shorter names */
#define wrap_mk_loc(p) wrap_mk_loclist(p)
#define wrap_mk_cx(p) wrap_mk_condexpr(p)

/*
 * Macros for allocating new objects
 */

/* old-style for struct nvlist */
#define	new0(n,s,p,i,x)	wrap_mk_nvlist(newnv(n, s, p, i, x))
#define	new_n(n)	new0(n, NULL, NULL, 0, NULL)
#define	new_nx(n, x)	new0(n, NULL, NULL, 0, x)
#define	new_ns(n, s)	new0(n, s, NULL, 0, NULL)
#define	new_si(s, i)	new0(NULL, s, NULL, i, NULL)
#define	new_spi(s, p, i)	new0(NULL, s, p, i, NULL)
#define	new_nsi(n,s,i)	new0(n, s, NULL, i, NULL)
#define	new_np(n, p)	new0(n, NULL, p, 0, NULL)
#define	new_s(s)	new0(NULL, s, NULL, 0, NULL)
#define	new_p(p)	new0(NULL, NULL, p, 0, NULL)
#define	new_px(p, x)	new0(NULL, NULL, p, 0, x)
#define	new_sx(s, x)	new0(NULL, s, NULL, 0, x)
#define	new_nsx(n,s,x)	new0(n, s, NULL, 0, x)
#define	new_i(i)	new0(NULL, NULL, NULL, i, NULL)

/* new style, type-polymorphic; ordinary and for types with multiple flavors */
#define MK0(t)		wrap_mk_##t(mk_##t())
#define MK1(t, a0)	wrap_mk_##t(mk_##t(a0))
#define MK2(t, a0, a1)	wrap_mk_##t(mk_##t(a0, a1))
#define MK3(t, a0, a1, a2)	wrap_mk_##t(mk_##t(a0, a1, a2))

#define MKF0(t, f)		wrap_mk_##t(mk_##t##_##f())
#define MKF1(t, f, a0)		wrap_mk_##t(mk_##t##_##f(a0))
#define MKF2(t, f, a0, a1)	wrap_mk_##t(mk_##t##_##f(a0, a1))

/*
 * Data constructors
 */

static struct defoptlist *mk_defoptlist(const char *, const char *,
					const char *);
static struct loclist *mk_loc(const char *, const char *, long long);
static struct loclist *mk_loc_val(const char *, struct loclist *);
static struct attrlist *mk_attrlist(struct attrlist *, struct attr *);
static struct condexpr *mk_cx_atom(const char *);
static struct condexpr *mk_cx_not(struct condexpr *);
static struct condexpr *mk_cx_and(struct condexpr *, struct condexpr *);
static struct condexpr *mk_cx_or(struct condexpr *, struct condexpr *);

/*
 * Other private functions
 */

static	void	setmachine(const char *, const char *, struct nvlist *, int);
static	void	check_maxpart(void);

static struct loclist *present_loclist(struct loclist *ll);
static void app(struct loclist *, struct loclist *);
static struct loclist *locarray(const char *, int, struct loclist *, int);
static struct loclist *namelocvals(const char *, struct loclist *);


#line 225 "y.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "gram.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_AND = 3,                        /* AND  */
  YYSYMBOL_AT = 4,                         /* AT  */
  YYSYMBOL_ATTACH = 5,                     /* ATTACH  */
  YYSYMBOL_BLOCK = 6,                      /* BLOCK  */
  YYSYMBOL_BUILD = 7,                      /* BUILD  */
  YYSYMBOL_CHAR = 8,                       /* CHAR  */
  YYSYMBOL_COLONEQ = 9,                    /* COLONEQ  */
  YYSYMBOL_COMPILE_WITH = 10,              /* COMPILE_WITH  */
  YYSYMBOL_CONFIG = 11,                    /* CONFIG  */
  YYSYMBOL_DEFFS = 12,                     /* DEFFS  */
  YYSYMBOL_DEFINE = 13,                    /* DEFINE  */
  YYSYMBOL_DEFOPT = 14,                    /* DEFOPT  */
  YYSYMBOL_DEFPARAM = 15,                  /* DEFPARAM  */
  YYSYMBOL_DEFFLAG = 16,                   /* DEFFLAG  */
  YYSYMBOL_DEFPSEUDO = 17,                 /* DEFPSEUDO  */
  YYSYMBOL_DEFPSEUDODEV = 18,              /* DEFPSEUDODEV  */
  YYSYMBOL_DEVICE = 19,                    /* DEVICE  */
  YYSYMBOL_DEVCLASS = 20,                  /* DEVCLASS  */
  YYSYMBOL_DUMPS = 21,                     /* DUMPS  */
  YYSYMBOL_DEVICE_MAJOR = 22,              /* DEVICE_MAJOR  */
  YYSYMBOL_ENDFILE = 23,                   /* ENDFILE  */
  YYSYMBOL_XFILE = 24,                     /* XFILE  */
  YYSYMBOL_FILE_SYSTEM = 25,               /* FILE_SYSTEM  */
  YYSYMBOL_FLAGS = 26,                     /* FLAGS  */
  YYSYMBOL_IDENT = 27,                     /* IDENT  */
  YYSYMBOL_IOCONF = 28,                    /* IOCONF  */
  YYSYMBOL_LINKZERO = 29,                  /* LINKZERO  */
  YYSYMBOL_XMACHINE = 30,                  /* XMACHINE  */
  YYSYMBOL_MAJOR = 31,                     /* MAJOR  */
  YYSYMBOL_MAKEOPTIONS = 32,               /* MAKEOPTIONS  */
  YYSYMBOL_MAXUSERS = 33,                  /* MAXUSERS  */
  YYSYMBOL_MAXPARTITIONS = 34,             /* MAXPARTITIONS  */
  YYSYMBOL_MINOR = 35,                     /* MINOR  */
  YYSYMBOL_NEEDS_COUNT = 36,               /* NEEDS_COUNT  */
  YYSYMBOL_NEEDS_FLAG = 37,                /* NEEDS_FLAG  */
  YYSYMBOL_NO = 38,                        /* NO  */
  YYSYMBOL_CNO = 39,                       /* CNO  */
  YYSYMBOL_XOBJECT = 40,                   /* XOBJECT  */
  YYSYMBOL_OBSOLETE = 41,                  /* OBSOLETE  */
  YYSYMBOL_ON = 42,                        /* ON  */
  YYSYMBOL_OPTIONS = 43,                   /* OPTIONS  */
  YYSYMBOL_PACKAGE = 44,                   /* PACKAGE  */
  YYSYMBOL_PLUSEQ = 45,                    /* PLUSEQ  */
  YYSYMBOL_PREFIX = 46,                    /* PREFIX  */
  YYSYMBOL_BUILDPREFIX = 47,               /* BUILDPREFIX  */
  YYSYMBOL_PSEUDO_DEVICE = 48,             /* PSEUDO_DEVICE  */
  YYSYMBOL_PSEUDO_ROOT = 49,               /* PSEUDO_ROOT  */
  YYSYMBOL_ROOT = 50,                      /* ROOT  */
  YYSYMBOL_SELECT = 51,                    /* SELECT  */
  YYSYMBOL_SINGLE = 52,                    /* SINGLE  */
  YYSYMBOL_SOURCE = 53,                    /* SOURCE  */
  YYSYMBOL_TYPE = 54,                      /* TYPE  */
  YYSYMBOL_VECTOR = 55,                    /* VECTOR  */
  YYSYMBOL_VERSION = 56,                   /* VERSION  */
  YYSYMBOL_WITH = 57,                      /* WITH  */
  YYSYMBOL_NUMBER = 58,                    /* NUMBER  */
  YYSYMBOL_PATHNAME = 59,                  /* PATHNAME  */
  YYSYMBOL_QSTRING = 60,                   /* QSTRING  */
  YYSYMBOL_WORD = 61,                      /* WORD  */
  YYSYMBOL_EMPTYSTRING = 62,               /* EMPTYSTRING  */
  YYSYMBOL_ENDDEFS = 63,                   /* ENDDEFS  */
  YYSYMBOL_64_n_ = 64,                     /* '\n'  */
  YYSYMBOL_65_ = 65,                       /* '{'  */
  YYSYMBOL_66_ = 66,                       /* '}'  */
  YYSYMBOL_67_ = 67,                       /* ','  */
  YYSYMBOL_68_ = 68,                       /* '='  */
  YYSYMBOL_69_ = 69,                       /* ':'  */
  YYSYMBOL_70_ = 70,                       /* '['  */
  YYSYMBOL_71_ = 71,                       /* ']'  */
  YYSYMBOL_72_ = 72,                       /* '?'  */
  YYSYMBOL_73_ = 73,                       /* '*'  */
  YYSYMBOL_74_ = 74,                       /* '|'  */
  YYSYMBOL_75_ = 75,                       /* '&'  */
  YYSYMBOL_76_ = 76,                       /* '!'  */
  YYSYMBOL_77_ = 77,                       /* '('  */
  YYSYMBOL_78_ = 78,                       /* ')'  */
  YYSYMBOL_79_ = 79,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 80,                  /* $accept  */
  YYSYMBOL_configuration = 81,             /* configuration  */
  YYSYMBOL_topthings = 82,                 /* topthings  */
  YYSYMBOL_topthing = 83,                  /* topthing  */
  YYSYMBOL_machine_spec = 84,              /* machine_spec  */
  YYSYMBOL_subarches = 85,                 /* subarches  */
  YYSYMBOL_no = 86,                        /* no  */
  YYSYMBOL_definition_part = 87,           /* definition_part  */
  YYSYMBOL_definitions = 88,               /* definitions  */
  YYSYMBOL_definition = 89,                /* definition  */
  YYSYMBOL_define_file = 90,               /* define_file  */
  YYSYMBOL_define_object = 91,             /* define_object  */
  YYSYMBOL_define_device_major = 92,       /* define_device_major  */
  YYSYMBOL_define_prefix = 93,             /* define_prefix  */
  YYSYMBOL_define_buildprefix = 94,        /* define_buildprefix  */
  YYSYMBOL_define_devclass = 95,           /* define_devclass  */
  YYSYMBOL_define_filesystems = 96,        /* define_filesystems  */
  YYSYMBOL_define_attribute = 97,          /* define_attribute  */
  YYSYMBOL_define_option = 98,             /* define_option  */
  YYSYMBOL_define_flag = 99,               /* define_flag  */
  YYSYMBOL_define_obsolete_flag = 100,     /* define_obsolete_flag  */
  YYSYMBOL_define_param = 101,             /* define_param  */
  YYSYMBOL_define_obsolete_param = 102,    /* define_obsolete_param  */
  YYSYMBOL_define_device = 103,            /* define_device  */
  YYSYMBOL_define_device_attachment = 104, /* define_device_attachment  */
  YYSYMBOL_define_maxpartitions = 105,     /* define_maxpartitions  */
  YYSYMBOL_define_maxusers = 106,          /* define_maxusers  */
  YYSYMBOL_define_makeoptions = 107,       /* define_makeoptions  */
  YYSYMBOL_define_pseudo = 108,            /* define_pseudo  */
  YYSYMBOL_define_pseudodev = 109,         /* define_pseudodev  */
  YYSYMBOL_define_major = 110,             /* define_major  */
  YYSYMBOL_define_version = 111,           /* define_version  */
  YYSYMBOL_fopts = 112,                    /* fopts  */
  YYSYMBOL_fflags = 113,                   /* fflags  */
  YYSYMBOL_fflag = 114,                    /* fflag  */
  YYSYMBOL_rule = 115,                     /* rule  */
  YYSYMBOL_oflags = 116,                   /* oflags  */
  YYSYMBOL_oflag = 117,                    /* oflag  */
  YYSYMBOL_device_major_char = 118,        /* device_major_char  */
  YYSYMBOL_device_major_block = 119,       /* device_major_block  */
  YYSYMBOL_devnodes = 120,                 /* devnodes  */
  YYSYMBOL_devnodetype = 121,              /* devnodetype  */
  YYSYMBOL_devnode_dims = 122,             /* devnode_dims  */
  YYSYMBOL_devnodeflags = 123,             /* devnodeflags  */
  YYSYMBOL_deffses = 124,                  /* deffses  */
  YYSYMBOL_deffs = 125,                    /* deffs  */
  YYSYMBOL_interface_opt = 126,            /* interface_opt  */
  YYSYMBOL_loclist = 127,                  /* loclist  */
  YYSYMBOL_locdef = 128,                   /* locdef  */
  YYSYMBOL_locname = 129,                  /* locname  */
  YYSYMBOL_locdefault = 130,               /* locdefault  */
  YYSYMBOL_locdefaults = 131,              /* locdefaults  */
  YYSYMBOL_depend_list = 132,              /* depend_list  */
  YYSYMBOL_depends = 133,                  /* depends  */
  YYSYMBOL_depend = 134,                   /* depend  */
  YYSYMBOL_optdepend_list = 135,           /* optdepend_list  */
  YYSYMBOL_optdepends = 136,               /* optdepends  */
  YYSYMBOL_optdepend = 137,                /* optdepend  */
  YYSYMBOL_atlist = 138,                   /* atlist  */
  YYSYMBOL_atname = 139,                   /* atname  */
  YYSYMBOL_defopts = 140,                  /* defopts  */
  YYSYMBOL_defopt = 141,                   /* defopt  */
  YYSYMBOL_condmkopt_list = 142,           /* condmkopt_list  */
  YYSYMBOL_condmkoption = 143,             /* condmkoption  */
  YYSYMBOL_devbase = 144,                  /* devbase  */
  YYSYMBOL_devattach_opt = 145,            /* devattach_opt  */
  YYSYMBOL_majorlist = 146,                /* majorlist  */
  YYSYMBOL_majordef = 147,                 /* majordef  */
  YYSYMBOL_int32 = 148,                    /* int32  */
  YYSYMBOL_selection_part = 149,           /* selection_part  */
  YYSYMBOL_selections = 150,               /* selections  */
  YYSYMBOL_selection = 151,                /* selection  */
  YYSYMBOL_select_attr = 152,              /* select_attr  */
  YYSYMBOL_select_no_attr = 153,           /* select_no_attr  */
  YYSYMBOL_select_no_filesystems = 154,    /* select_no_filesystems  */
  YYSYMBOL_155_1 = 155,                    /* $@1  */
  YYSYMBOL_select_filesystems = 156,       /* select_filesystems  */
  YYSYMBOL_select_no_makeoptions = 157,    /* select_no_makeoptions  */
  YYSYMBOL_158_2 = 158,                    /* $@2  */
  YYSYMBOL_select_makeoptions = 159,       /* select_makeoptions  */
  YYSYMBOL_select_no_options = 160,        /* select_no_options  */
  YYSYMBOL_161_3 = 161,                    /* $@3  */
  YYSYMBOL_select_options = 162,           /* select_options  */
  YYSYMBOL_select_maxusers = 163,          /* select_maxusers  */
  YYSYMBOL_select_ident = 164,             /* select_ident  */
  YYSYMBOL_select_no_ident = 165,          /* select_no_ident  */
  YYSYMBOL_select_config = 166,            /* select_config  */
  YYSYMBOL_select_no_config = 167,         /* select_no_config  */
  YYSYMBOL_select_no_pseudodev = 168,      /* select_no_pseudodev  */
  YYSYMBOL_select_pseudodev = 169,         /* select_pseudodev  */
  YYSYMBOL_select_pseudoroot = 170,        /* select_pseudoroot  */
  YYSYMBOL_select_no_device_instance_attachment = 171, /* select_no_device_instance_attachment  */
  YYSYMBOL_select_no_device_attachment = 172, /* select_no_device_attachment  */
  YYSYMBOL_select_no_device_instance = 173, /* select_no_device_instance  */
  YYSYMBOL_select_device_instance = 174,   /* select_device_instance  */
  YYSYMBOL_fs_list = 175,                  /* fs_list  */
  YYSYMBOL_fsoption = 176,                 /* fsoption  */
  YYSYMBOL_no_fs_list = 177,               /* no_fs_list  */
  YYSYMBOL_no_fsoption = 178,              /* no_fsoption  */
  YYSYMBOL_mkopt_list = 179,               /* mkopt_list  */
  YYSYMBOL_mkoption = 180,                 /* mkoption  */
  YYSYMBOL_no_mkopt_list = 181,            /* no_mkopt_list  */
  YYSYMBOL_no_mkoption = 182,              /* no_mkoption  */
  YYSYMBOL_opt_list = 183,                 /* opt_list  */
  YYSYMBOL_option = 184,                   /* option  */
  YYSYMBOL_no_opt_list = 185,              /* no_opt_list  */
  YYSYMBOL_no_option = 186,                /* no_option  */
  YYSYMBOL_conf = 187,                     /* conf  */
  YYSYMBOL_root_spec = 188,                /* root_spec  */
  YYSYMBOL_dev_spec = 189,                 /* dev_spec  */
  YYSYMBOL_major_minor = 190,              /* major_minor  */
  YYSYMBOL_fs_spec = 191,                  /* fs_spec  */
  YYSYMBOL_sysparam_list = 192,            /* sysparam_list  */
  YYSYMBOL_sysparam = 193,                 /* sysparam  */
  YYSYMBOL_npseudo = 194,                  /* npseudo  */
  YYSYMBOL_device_instance = 195,          /* device_instance  */
  YYSYMBOL_attachment = 196,               /* attachment  */
  YYSYMBOL_locators = 197,                 /* locators  */
  YYSYMBOL_locator = 198,                  /* locator  */
  YYSYMBOL_device_flags = 199,             /* device_flags  */
  YYSYMBOL_condexpr = 200,                 /* condexpr  */
  YYSYMBOL_cond_or_expr = 201,             /* cond_or_expr  */
  YYSYMBOL_cond_and_expr = 202,            /* cond_and_expr  */
  YYSYMBOL_cond_prefix_expr = 203,         /* cond_prefix_expr  */
  YYSYMBOL_cond_base_expr = 204,           /* cond_base_expr  */
  YYSYMBOL_condatom = 205,                 /* condatom  */
  YYSYMBOL_mkvarname = 206,                /* mkvarname  */
  YYSYMBOL_optfile_opt = 207,              /* optfile_opt  */
  YYSYMBOL_filename = 208,                 /* filename  */
  YYSYMBOL_value = 209,                    /* value  */
  YYSYMBOL_stringvalue = 210,              /* stringvalue  */
  YYSYMBOL_values = 211,                   /* values  */
  YYSYMBOL_signed_number = 212,            /* signed_number  */
  YYSYMBOL_on_opt = 213                    /* on_opt  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   337

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  80
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  134
/* YYNRULES -- Number of rules.  */
#define YYNRULES  263
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  411

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   318


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      64,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    76,     2,     2,     2,     2,    75,     2,
      77,    78,    73,     2,    67,    79,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    69,     2,
       2,    68,     2,    72,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    70,     2,    71,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    65,    74,    66,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   265,   265,   269,   271,   276,   277,   278,   283,   284,
     285,   286,   287,   292,   293,   297,   298,   309,   317,   319,
     320,   321,   322,   327,   328,   329,   330,   331,   332,   333,
     334,   335,   336,   337,   338,   339,   340,   341,   342,   343,
     344,   345,   346,   347,   348,   353,   358,   363,   372,   373,
     377,   378,   379,   383,   387,   391,   396,   401,   406,   411,
     416,   421,   426,   431,   435,   440,   445,   450,   455,   459,
     464,   465,   470,   471,   476,   477,   482,   483,   488,   489,
     494,   499,   500,   505,   506,   511,   512,   513,   518,   519,
     524,   525,   536,   541,   542,   547,   552,   553,   554,   564,
     565,   574,   575,   576,   577,   578,   580,   586,   587,   592,
     597,   602,   603,   608,   609,   614,   619,   620,   625,   626,
     631,   637,   638,   643,   644,   649,   650,   655,   656,   657,
     658,   663,   664,   669,   674,   679,   680,   686,   687,   692,
     696,   712,   716,   718,   719,   720,   725,   726,   727,   728,
     729,   730,   731,   732,   733,   734,   735,   736,   737,   738,
     739,   740,   741,   742,   743,   744,   745,   749,   753,   757,
     757,   761,   765,   765,   769,   773,   773,   777,   781,   785,
     789,   793,   798,   802,   806,   810,   814,   819,   823,   827,
     833,   834,   839,   844,   845,   850,   856,   857,   862,   863,
     868,   869,   875,   880,   881,   886,   887,   892,   893,   898,
     903,   914,   915,   920,   923,   926,   929,   934,   939,   940,
     944,   946,   951,   956,   957,   962,   963,   968,   969,   970,
     975,   976,   981,   982,   987,   988,  1006,  1010,  1011,  1015,
    1016,  1020,  1026,  1027,  1028,  1033,  1044,  1045,  1050,  1051,
    1056,  1057,  1062,  1063,  1064,  1065,  1075,  1076,  1082,  1083,
    1088,  1089,  1093,  1095
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "AND", "AT", "ATTACH",
  "BLOCK", "BUILD", "CHAR", "COLONEQ", "COMPILE_WITH", "CONFIG", "DEFFS",
  "DEFINE", "DEFOPT", "DEFPARAM", "DEFFLAG", "DEFPSEUDO", "DEFPSEUDODEV",
  "DEVICE", "DEVCLASS", "DUMPS", "DEVICE_MAJOR", "ENDFILE", "XFILE",
  "FILE_SYSTEM", "FLAGS", "IDENT", "IOCONF", "LINKZERO", "XMACHINE",
  "MAJOR", "MAKEOPTIONS", "MAXUSERS", "MAXPARTITIONS", "MINOR",
  "NEEDS_COUNT", "NEEDS_FLAG", "NO", "CNO", "XOBJECT", "OBSOLETE", "ON",
  "OPTIONS", "PACKAGE", "PLUSEQ", "PREFIX", "BUILDPREFIX", "PSEUDO_DEVICE",
  "PSEUDO_ROOT", "ROOT", "SELECT", "SINGLE", "SOURCE", "TYPE", "VECTOR",
  "VERSION", "WITH", "NUMBER", "PATHNAME", "QSTRING", "WORD",
  "EMPTYSTRING", "ENDDEFS", "'\\n'", "'{'", "'}'", "','", "'='", "':'",
  "'['", "']'", "'?'", "'*'", "'|'", "'&'", "'!'", "'('", "')'", "'-'",
  "$accept", "configuration", "topthings", "topthing", "machine_spec",
  "subarches", "no", "definition_part", "definitions", "definition",
  "define_file", "define_object", "define_device_major", "define_prefix",
  "define_buildprefix", "define_devclass", "define_filesystems",
  "define_attribute", "define_option", "define_flag",
  "define_obsolete_flag", "define_param", "define_obsolete_param",
  "define_device", "define_device_attachment", "define_maxpartitions",
  "define_maxusers", "define_makeoptions", "define_pseudo",
  "define_pseudodev", "define_major", "define_version", "fopts", "fflags",
  "fflag", "rule", "oflags", "oflag", "device_major_char",
  "device_major_block", "devnodes", "devnodetype", "devnode_dims",
  "devnodeflags", "deffses", "deffs", "interface_opt", "loclist", "locdef",
  "locname", "locdefault", "locdefaults", "depend_list", "depends",
  "depend", "optdepend_list", "optdepends", "optdepend", "atlist",
  "atname", "defopts", "defopt", "condmkopt_list", "condmkoption",
  "devbase", "devattach_opt", "majorlist", "majordef", "int32",
  "selection_part", "selections", "selection", "select_attr",
  "select_no_attr", "select_no_filesystems", "$@1", "select_filesystems",
  "select_no_makeoptions", "$@2", "select_makeoptions",
  "select_no_options", "$@3", "select_options", "select_maxusers",
  "select_ident", "select_no_ident", "select_config", "select_no_config",
  "select_no_pseudodev", "select_pseudodev", "select_pseudoroot",
  "select_no_device_instance_attachment", "select_no_device_attachment",
  "select_no_device_instance", "select_device_instance", "fs_list",
  "fsoption", "no_fs_list", "no_fsoption", "mkopt_list", "mkoption",
  "no_mkopt_list", "no_mkoption", "opt_list", "option", "no_opt_list",
  "no_option", "conf", "root_spec", "dev_spec", "major_minor", "fs_spec",
  "sysparam_list", "sysparam", "npseudo", "device_instance", "attachment",
  "locators", "locator", "device_flags", "condexpr", "cond_or_expr",
  "cond_and_expr", "cond_prefix_expr", "cond_base_expr", "condatom",
  "mkvarname", "optfile_opt", "filename", "value", "stringvalue", "values",
  "signed_number", "on_opt", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-309)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-248)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -309,    85,    54,  -309,  -309,     9,    37,    43,     9,  -309,
    -309,  -309,  -309,  -309,    46,    55,   -35,    90,  -309,   133,
    -309,  -309,    99,  -309,  -309,  -309,    32,    97,   116,   121,
     143,     9,     9,     9,   116,   116,   116,   164,   167,  -309,
       9,   129,    82,   168,   168,     9,   120,     9,   152,   168,
    -309,  -309,   134,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,  -309,  -309,   111,   169,   170,
     171,   157,   110,   168,  -309,  -309,   173,   174,   175,   176,
     156,  -309,    81,  -309,   177,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,   234,  -309,  -309,   235,  -309,
      30,  -309,   178,   179,  -309,   179,   179,   178,   178,   178,
    -309,   236,    82,   116,  -309,   181,    82,   180,  -309,   159,
     172,   182,  -309,  -309,  -309,  -309,   168,  -309,    82,     9,
       9,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
     195,  -309,   183,  -309,  -309,  -309,  -309,  -309,   -27,   184,
    -309,    -6,   168,   185,   187,  -309,   168,  -309,  -309,  -309,
     188,   244,  -309,  -309,  -309,  -309,   191,   194,   252,  -309,
     -23,   -19,   197,  -309,  -309,   -36,   190,    -1,    62,  -309,
      62,    62,   190,   190,   190,   168,   254,  -309,  -309,   193,
     155,  -309,  -309,   186,    82,  -309,   217,    82,    82,   168,
    -309,   179,   179,   221,  -309,   171,   159,   130,   130,   130,
     173,  -309,  -309,  -309,   -23,   204,   205,   206,  -309,  -309,
     -23,  -309,   196,  -309,  -309,  -309,    48,  -309,  -309,   202,
    -309,  -309,  -309,  -309,   163,   207,   203,   137,   210,  -309,
     130,   130,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
     168,    82,    84,   168,  -309,   116,  -309,  -309,   130,   182,
    -309,  -309,   237,   179,   179,  -309,    29,   251,  -309,  -309,
    -309,  -309,  -309,  -309,   218,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,   208,  -309,  -309,   212,  -309,  -309,   213,  -309,
    -309,  -309,    -3,   216,   -19,   190,   197,   140,  -309,   108,
     130,   168,  -309,  -309,   214,  -309,  -309,   273,  -309,   151,
     157,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
     225,  -309,  -309,  -309,   230,  -309,   221,  -309,  -309,   204,
     205,   206,   168,   123,  -309,  -309,  -309,  -309,  -309,  -309,
     168,   215,  -309,  -309,   219,   210,   130,  -309,   220,  -309,
     222,  -309,   250,    15,  -309,    29,  -309,  -309,  -309,  -309,
    -309,   224,  -309,   223,  -309,   227,  -309,  -309,   229,   263,
     238,  -309,  -309,  -309,   130,   227,   228,  -309,   231,  -309,
    -309,  -309,  -309,  -309,   226,   130,   240,  -309,   233,  -309,
    -309
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     0,     1,    12,     0,     0,     0,     0,     5,
       4,    18,   251,   250,     0,     0,     0,     0,   142,     0,
       7,    11,     0,     8,     6,     2,     0,     0,     0,     0,
       0,   248,   248,   248,     0,     0,     0,     0,     0,    22,
       0,     0,     0,     0,     0,     0,     0,    49,    52,     0,
      17,    19,     0,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    13,     9,     0,     0,     0,
       0,     0,     0,     0,    15,    16,     0,     0,     0,     0,
     225,   143,     0,   146,     0,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,     0,    21,   134,     0,    95,
     116,    93,    96,     0,   249,     0,     0,    96,    96,    96,
      53,    81,    70,     0,   245,     0,     0,    65,   131,     0,
     236,   237,   239,   241,   242,   140,     0,    63,    70,   248,
     248,    48,    51,    50,    69,    20,    14,    10,   145,   210,
       0,   192,   171,   190,   256,   257,   179,   246,   245,   174,
     196,     0,   178,   205,   177,   203,   223,   185,   167,   226,
       0,     0,   169,   180,   172,   175,     0,     0,   188,   144,
       0,     0,     0,    94,    54,     0,   111,   127,   116,   125,
     116,   116,   111,   111,   111,     0,    83,    72,    71,     0,
       0,   137,   243,     0,     0,   247,     0,     0,     0,     0,
      78,     0,     0,   262,   220,     0,     0,     0,     0,     0,
       0,   224,   184,   182,     0,     0,     0,     0,   183,   168,
       0,   227,   228,   230,   124,   123,   135,   121,   120,   117,
     118,   108,   107,    97,     0,     0,    99,   102,     0,    55,
       0,     0,    56,   126,    59,    57,    66,    67,    61,    82,
       0,    70,    76,     0,    68,     0,   244,   132,     0,   238,
     240,    64,    46,    60,    58,   263,     0,   181,   191,   197,
     260,   252,   253,   254,     0,   199,   255,   198,   206,   204,
     187,   195,   170,   193,   202,   173,   200,   209,   176,   207,
     186,   229,   234,     0,     0,   111,     0,     0,    98,     0,
       0,     0,   101,   115,   112,   113,   129,   128,    84,    85,
       0,    74,    75,    73,    45,   139,   138,   133,    80,    79,
       0,   214,   215,   213,   211,   216,   262,   221,   261,     0,
       0,     0,     0,     0,   231,   189,   136,   122,    62,   119,
       0,     0,   100,   109,     0,     0,     0,    88,     0,    47,
      87,    77,     0,     0,   212,     0,   194,   201,   208,   235,
     232,   258,   233,     0,   103,   104,   114,   130,     0,     0,
       0,   219,   218,   222,     0,     0,     0,   105,    90,    89,
      92,    86,   217,   259,     0,     0,     0,   106,     0,    91,
     110
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,   275,
    -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -141,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,   192,    87,   -17,  -309,    49,
     -13,   -90,  -190,  -309,   -59,   -61,  -309,    -9,  -309,    -4,
    -110,  -181,  -309,    94,   165,  -309,  -309,    36,   -43,  -309,
    -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,  -309,
    -309,  -309,  -309,  -309,  -309,  -309,    88,  -309,   -34,  -309,
      92,  -309,   -31,  -309,    86,  -309,   -37,  -309,  -309,   -55,
    -309,  -309,  -309,  -309,  -309,    34,  -212,  -309,  -309,  -309,
    -127,  -309,   104,   105,  -309,   189,   198,   -22,    69,  -225,
      -8,  -308,  -309,   -21
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    10,    11,    77,    92,    18,    19,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,   207,   272,   333,   334,   282,   339,   206,   271,
     369,   370,   399,   401,   120,   121,   196,   255,   256,   257,
     322,   397,   259,   324,   325,   194,   249,   250,   246,   247,
     198,   199,   137,   138,   209,   315,   210,   211,   219,    25,
      26,    94,    95,    96,    97,   235,    98,    99,   236,   100,
     101,   237,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   162,   163,   302,   303,   169,
     170,   305,   306,   174,   175,   308,   309,   160,   224,   344,
     345,   374,   287,   347,   232,   115,   243,   312,   354,   355,
     139,   140,   141,   142,   143,   144,   171,   123,   124,   381,
     166,   382,   296,   286
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     146,   147,   295,   297,   298,   208,   154,   220,   260,   213,
     125,   126,   266,   267,   268,   200,   201,   263,  -247,   263,
     263,   208,   300,   352,   251,   252,    22,   241,   310,    23,
     253,   244,  -141,    78,   254,   326,   327,    28,   242,   227,
     172,  -247,   245,    79,    29,    30,    31,    32,    33,    34,
      35,    36,    37,   337,    38,     4,    40,    80,   353,    81,
     340,     5,   228,    41,    82,    83,    44,   261,    12,    13,
      84,    85,    45,    46,    14,    86,   391,    17,    47,    48,
      87,    88,     6,    89,     7,     3,   403,   392,    49,   341,
     342,   119,   180,    90,   330,   363,    91,   408,    15,   192,
     181,   343,   263,   263,    16,   313,   182,     8,   183,   132,
      20,   283,   284,   184,   148,   314,   151,   153,     9,    21,
     331,   332,   177,   197,   185,   358,   188,   221,   222,   186,
     329,   192,   187,   231,    27,   149,   150,   262,    28,   264,
     265,   387,    90,   134,   208,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    24,    38,    39,    40,   135,   136,
      75,   116,   269,    76,    41,    42,    43,    44,   251,   252,
     167,   168,   156,    45,    46,   157,   281,   117,   254,    47,
      48,   290,   119,   291,   292,   293,   135,   136,   290,    49,
     291,   292,   293,   118,   133,   380,    50,    51,   155,   127,
     128,   129,   294,   367,   122,   320,   368,   321,   320,   294,
     360,    12,    13,   152,   202,   203,   204,   164,   165,   167,
     215,   274,   275,   251,   252,   130,   145,   328,   131,   179,
     335,   159,   161,   158,   173,   176,    90,   178,   190,   191,
     197,   189,   134,   195,   205,   223,   217,   214,   234,   233,
     225,   226,   238,   229,   230,   239,   240,   218,   248,   258,
     270,   273,   278,   285,   276,   301,   304,   307,   311,   316,
     319,   323,   346,   318,   338,   349,   348,   356,   364,   350,
     351,   365,   366,   372,   373,   390,   384,   398,   388,   389,
     385,   394,   400,   405,   395,   396,   402,   407,   409,   410,
     406,    93,   362,   317,   361,   404,   386,   359,   277,   379,
     357,   336,   193,   288,   378,   376,   299,   383,   289,   377,
     393,   279,   371,   280,   212,   375,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   216
};

static const yytype_int16 yycheck[] =
{
      43,    44,   227,   228,   229,   132,    49,   148,     9,   136,
      32,    33,   202,   203,   204,   125,   126,   198,    45,   200,
     201,   148,   234,    26,    60,    61,    61,    50,   240,    64,
      66,    50,     0,     1,    70,   260,   261,     5,    61,    45,
      83,    68,    61,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,   278,    22,     1,    24,    25,    61,    27,
      31,     7,    68,    31,    32,    33,    34,    68,    59,    60,
      38,    39,    40,    41,     5,    43,    61,     8,    46,    47,
      48,    49,    28,    51,    30,     0,   394,    72,    56,    60,
      61,    61,    11,    61,    10,   320,    64,   405,    61,    69,
      19,    72,   283,   284,    61,    57,    25,    53,    27,    40,
      64,   221,   222,    32,    45,    67,    47,    48,    64,    64,
      36,    37,    88,    61,    43,   315,    92,   149,   150,    48,
     271,    69,    51,   176,     1,    15,    16,   198,     5,   200,
     201,   366,    61,    61,   271,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    64,    22,    23,    24,    76,    77,
      61,    64,   205,    64,    31,    32,    33,    34,    60,    61,
      60,    61,    61,    40,    41,    64,   219,    61,    70,    46,
      47,    58,    61,    60,    61,    62,    76,    77,    58,    56,
      60,    61,    62,    28,    65,    72,    63,    64,    64,    34,
      35,    36,    79,    52,    61,    68,    55,    70,    68,    79,
      70,    59,    60,    61,   127,   128,   129,    60,    61,    60,
      61,    66,    67,    60,    61,    61,    58,   270,    61,    73,
     273,    61,    61,    64,    61,    61,    61,    61,     4,     4,
      61,    64,    61,    65,     8,    50,    74,    67,     4,    61,
      67,    67,    61,    68,    67,    61,     4,    75,    61,    69,
       6,    68,    45,    42,    78,    61,    61,    61,    72,    67,
      67,    61,    21,    66,    37,    67,    58,    61,   321,    67,
      67,    67,     9,    58,    54,    35,    71,    58,    68,    67,
      71,    67,    29,    65,    71,    68,    58,    71,    58,    66,
      69,    26,   319,   254,   317,   395,   365,   316,   214,   352,
     314,   275,   120,   225,   351,   349,   230,   360,   226,   350,
     375,   217,   330,   218,   135,   346,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   139
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    81,    82,     0,     1,     7,    28,    30,    53,    64,
      83,    84,    59,    60,   208,    61,    61,   208,    87,    88,
      64,    64,    61,    64,    64,   149,   150,     1,     5,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    22,    23,
      24,    31,    32,    33,    34,    40,    41,    46,    47,    56,
      63,    64,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,    61,    64,    85,     1,    11,
      25,    27,    32,    33,    38,    39,    43,    48,    49,    51,
      61,    64,    86,    89,   151,   152,   153,   154,   156,   157,
     159,   160,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   195,    64,    61,   144,    61,
     124,   125,    61,   207,   208,   207,   207,   144,   144,   144,
      61,    61,   208,    65,    61,    76,    77,   142,   143,   200,
     201,   202,   203,   204,   205,    58,   148,   148,   208,    15,
      16,   208,    61,   208,   148,    64,    61,    64,    64,    61,
     187,    61,   175,   176,    60,    61,   210,    60,    61,   179,
     180,   206,   148,    61,   183,   184,    61,   195,    61,    73,
      11,    19,    25,    27,    32,    43,    48,    51,   195,    64,
       4,     4,    69,   125,   135,    65,   126,    61,   140,   141,
     140,   140,   126,   126,   126,     8,   118,   112,   200,   144,
     146,   147,   205,   200,    67,    61,   206,    74,    75,   148,
     112,   207,   207,    50,   188,    67,    67,    45,    68,    68,
      67,   148,   194,    61,     4,   155,   158,   161,    61,    61,
       4,    50,    61,   196,    50,    61,   138,   139,    61,   136,
     137,    60,    61,    66,    70,   127,   128,   129,    69,   132,
       9,    68,   135,   141,   135,   135,   132,   132,   132,   148,
       6,   119,   113,    68,    66,    67,    78,   143,    45,   202,
     203,   148,   116,   140,   140,    42,   213,   192,   176,   180,
      58,    60,    61,    62,    79,   209,   212,   209,   209,   184,
     196,    61,   177,   178,    61,   181,   182,    61,   185,   186,
     196,    72,   197,    57,    67,   145,    67,   129,    66,    67,
      68,    70,   130,    61,   133,   134,   209,   209,   148,   112,
      10,    36,    37,   114,   115,   148,   147,   209,    37,   117,
      31,    60,    61,    72,   189,   190,    21,   193,    58,    67,
      67,    67,    26,    61,   198,   199,    61,   139,   132,   137,
      70,   130,   127,   209,   148,    67,     9,    52,    55,   120,
     121,   210,    58,    54,   191,   213,   178,   182,   186,   148,
      72,   209,   211,   148,    71,    71,   134,   209,    68,    67,
      35,    61,    72,   189,    67,    71,    68,   131,    58,   122,
      29,   123,    58,   211,   131,    65,    69,    71,   211,    58,
      66
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    80,    81,    82,    82,    83,    83,    83,    84,    84,
      84,    84,    84,    85,    85,    86,    86,    87,    88,    88,
      88,    88,    88,    89,    89,    89,    89,    89,    89,    89,
      89,    89,    89,    89,    89,    89,    89,    89,    89,    89,
      89,    89,    89,    89,    89,    90,    91,    92,    93,    93,
      94,    94,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   112,   113,   113,   114,   114,   115,   115,   116,   116,
     117,   118,   118,   119,   119,   120,   120,   120,   121,   121,
     122,   122,   123,   124,   124,   125,   126,   126,   126,   127,
     127,   128,   128,   128,   128,   128,   128,   129,   129,   130,
     131,   132,   132,   133,   133,   134,   135,   135,   136,   136,
     137,   138,   138,   139,   139,   140,   140,   141,   141,   141,
     141,   142,   142,   143,   144,   145,   145,   146,   146,   147,
     148,   149,   150,   150,   150,   150,   151,   151,   151,   151,
     151,   151,   151,   151,   151,   151,   151,   151,   151,   151,
     151,   151,   151,   151,   151,   151,   151,   152,   153,   155,
     154,   156,   158,   157,   159,   161,   160,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   175,   176,   177,   177,   178,   179,   179,   180,   180,
     181,   181,   182,   183,   183,   184,   184,   185,   185,   186,
     187,   188,   188,   189,   189,   189,   189,   190,   191,   191,
     192,   192,   193,   194,   194,   195,   195,   196,   196,   196,
     197,   197,   198,   198,   199,   199,   200,   201,   201,   202,
     202,   203,   204,   204,   204,   205,   206,   206,   207,   207,
     208,   208,   209,   209,   209,   209,   210,   210,   211,   211,
     212,   212,   213,   213
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     4,     0,     2,     1,     3,     3,     3,     4,
       5,     3,     1,     1,     2,     1,     1,     2,     0,     2,
       3,     3,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     5,     4,     6,     2,     1,
       2,     2,     1,     2,     3,     4,     4,     4,     4,     4,
       4,     4,     6,     2,     4,     2,     4,     4,     4,     2,
       0,     1,     0,     2,     1,     1,     0,     2,     0,     2,
       1,     0,     2,     0,     2,     0,     3,     1,     1,     3,
       1,     3,     1,     1,     2,     1,     0,     2,     3,     1,
       3,     2,     1,     4,     4,     5,     7,     1,     1,     2,
       4,     0,     2,     1,     3,     1,     0,     2,     1,     3,
       1,     1,     3,     1,     1,     1,     2,     1,     3,     3,
       5,     1,     3,     4,     1,     0,     2,     1,     3,     3,
       1,     1,     0,     2,     3,     3,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     3,     0,
       4,     2,     0,     4,     2,     0,     4,     2,     2,     2,
       2,     4,     3,     3,     3,     2,     4,     4,     2,     5,
       1,     3,     1,     1,     3,     1,     1,     3,     3,     3,
       1,     3,     1,     1,     3,     1,     3,     1,     3,     1,
       1,     3,     4,     1,     1,     1,     1,     4,     2,     2,
       0,     2,     3,     0,     1,     1,     2,     1,     1,     2,
       0,     2,     2,     2,     0,     2,     1,     1,     3,     1,
       3,     1,     1,     2,     3,     1,     1,     1,     0,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       1,     2,     0,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 6: /* topthing: SOURCE filename '\n'  */
#line 277 "gram.y"
                                        { if (!srcdir) srcdir = (yyvsp[-1].str); }
#line 1752 "y.tab.c"
    break;

  case 7: /* topthing: BUILD filename '\n'  */
#line 278 "gram.y"
                                        { if (!builddir) builddir = (yyvsp[-1].str); }
#line 1758 "y.tab.c"
    break;

  case 8: /* machine_spec: XMACHINE WORD '\n'  */
#line 283 "gram.y"
                                                { setmachine((yyvsp[-1].str),NULL,NULL,0); }
#line 1764 "y.tab.c"
    break;

  case 9: /* machine_spec: XMACHINE WORD WORD '\n'  */
#line 284 "gram.y"
                                                { setmachine((yyvsp[-2].str),(yyvsp[-1].str),NULL,0); }
#line 1770 "y.tab.c"
    break;

  case 10: /* machine_spec: XMACHINE WORD WORD subarches '\n'  */
#line 285 "gram.y"
                                                { setmachine((yyvsp[-3].str),(yyvsp[-2].str),(yyvsp[-1].list),0); }
#line 1776 "y.tab.c"
    break;

  case 11: /* machine_spec: IOCONF WORD '\n'  */
#line 286 "gram.y"
                                                { setmachine((yyvsp[-1].str),NULL,NULL,1); }
#line 1782 "y.tab.c"
    break;

  case 12: /* machine_spec: error  */
#line 287 "gram.y"
                { stop("cannot proceed without machine or ioconf specifier"); }
#line 1788 "y.tab.c"
    break;

  case 13: /* subarches: WORD  */
#line 292 "gram.y"
                                        { (yyval.list) = new_n((yyvsp[0].str)); }
#line 1794 "y.tab.c"
    break;

  case 14: /* subarches: subarches WORD  */
#line 293 "gram.y"
                                        { (yyval.list) = new_nx((yyvsp[0].str), (yyvsp[-1].list)); }
#line 1800 "y.tab.c"
    break;

  case 15: /* no: NO  */
#line 297 "gram.y"
                { (yyval.i32) = 0; }
#line 1806 "y.tab.c"
    break;

  case 16: /* no: CNO  */
#line 298 "gram.y"
                { (yyval.i32) = 1; }
#line 1812 "y.tab.c"
    break;

  case 17: /* definition_part: definitions ENDDEFS  */
#line 309 "gram.y"
                                        {
		CFGDBG(1, "ENDDEFS");
		check_maxpart();
		check_version();
	}
#line 1822 "y.tab.c"
    break;

  case 20: /* definitions: definitions definition '\n'  */
#line 320 "gram.y"
                                        { wrap_continue(); }
#line 1828 "y.tab.c"
    break;

  case 21: /* definitions: definitions error '\n'  */
#line 321 "gram.y"
                                        { wrap_cleanup(); }
#line 1834 "y.tab.c"
    break;

  case 22: /* definitions: definitions ENDFILE  */
#line 322 "gram.y"
                                        { enddefs(); checkfiles(); }
#line 1840 "y.tab.c"
    break;

  case 45: /* define_file: XFILE filename fopts fflags rule  */
#line 353 "gram.y"
                                                { addfile((yyvsp[-3].str), (yyvsp[-2].condexpr), (yyvsp[-1].flag), (yyvsp[0].str)); }
#line 1846 "y.tab.c"
    break;

  case 46: /* define_object: XOBJECT filename fopts oflags  */
#line 358 "gram.y"
                                        { addfile((yyvsp[-2].str), (yyvsp[-1].condexpr), (yyvsp[0].flag), NULL); }
#line 1852 "y.tab.c"
    break;

  case 47: /* define_device_major: DEVICE_MAJOR WORD device_major_char device_major_block fopts devnodes  */
#line 364 "gram.y"
                                        {
		adddevm((yyvsp[-4].str), (yyvsp[-3].devmajor), (yyvsp[-2].devmajor), (yyvsp[-1].condexpr), (yyvsp[0].list));
		do_devsw = 1;
	}
#line 1861 "y.tab.c"
    break;

  case 48: /* define_prefix: PREFIX filename  */
#line 372 "gram.y"
                                        { prefix_push((yyvsp[0].str)); }
#line 1867 "y.tab.c"
    break;

  case 49: /* define_prefix: PREFIX  */
#line 373 "gram.y"
                                        { prefix_pop(); }
#line 1873 "y.tab.c"
    break;

  case 50: /* define_buildprefix: BUILDPREFIX filename  */
#line 377 "gram.y"
                                        { buildprefix_push((yyvsp[0].str)); }
#line 1879 "y.tab.c"
    break;

  case 51: /* define_buildprefix: BUILDPREFIX WORD  */
#line 378 "gram.y"
                                        { buildprefix_push((yyvsp[0].str)); }
#line 1885 "y.tab.c"
    break;

  case 52: /* define_buildprefix: BUILDPREFIX  */
#line 379 "gram.y"
                                        { buildprefix_pop(); }
#line 1891 "y.tab.c"
    break;

  case 53: /* define_devclass: DEVCLASS WORD  */
#line 383 "gram.y"
                                        { (void)defdevclass((yyvsp[0].str), NULL, NULL, 1); }
#line 1897 "y.tab.c"
    break;

  case 54: /* define_filesystems: DEFFS deffses optdepend_list  */
#line 387 "gram.y"
                                        { deffilesystem((yyvsp[-1].list), (yyvsp[0].list)); }
#line 1903 "y.tab.c"
    break;

  case 55: /* define_attribute: DEFINE WORD interface_opt depend_list  */
#line 392 "gram.y"
                                        { (void)defattr0((yyvsp[-2].str), (yyvsp[-1].loclist), (yyvsp[0].attrlist), 0); }
#line 1909 "y.tab.c"
    break;

  case 56: /* define_option: DEFOPT optfile_opt defopts optdepend_list  */
#line 397 "gram.y"
                                        { defoption((yyvsp[-2].str), (yyvsp[-1].defoptlist), (yyvsp[0].list)); }
#line 1915 "y.tab.c"
    break;

  case 57: /* define_flag: DEFFLAG optfile_opt defopts optdepend_list  */
#line 402 "gram.y"
                                        { defflag((yyvsp[-2].str), (yyvsp[-1].defoptlist), (yyvsp[0].list), 0); }
#line 1921 "y.tab.c"
    break;

  case 58: /* define_obsolete_flag: OBSOLETE DEFFLAG optfile_opt defopts  */
#line 407 "gram.y"
                                        { defflag((yyvsp[-1].str), (yyvsp[0].defoptlist), NULL, 1); }
#line 1927 "y.tab.c"
    break;

  case 59: /* define_param: DEFPARAM optfile_opt defopts optdepend_list  */
#line 412 "gram.y"
                                        { defparam((yyvsp[-2].str), (yyvsp[-1].defoptlist), (yyvsp[0].list), 0); }
#line 1933 "y.tab.c"
    break;

  case 60: /* define_obsolete_param: OBSOLETE DEFPARAM optfile_opt defopts  */
#line 417 "gram.y"
                                        { defparam((yyvsp[-1].str), (yyvsp[0].defoptlist), NULL, 1); }
#line 1939 "y.tab.c"
    break;

  case 61: /* define_device: DEVICE devbase interface_opt depend_list  */
#line 422 "gram.y"
                                        { defdev((yyvsp[-2].devb), (yyvsp[-1].loclist), (yyvsp[0].attrlist), 0); }
#line 1945 "y.tab.c"
    break;

  case 62: /* define_device_attachment: ATTACH devbase AT atlist devattach_opt depend_list  */
#line 427 "gram.y"
                                        { defdevattach((yyvsp[-1].deva), (yyvsp[-4].devb), (yyvsp[-2].list), (yyvsp[0].attrlist)); }
#line 1951 "y.tab.c"
    break;

  case 63: /* define_maxpartitions: MAXPARTITIONS int32  */
#line 431 "gram.y"
                                        { maxpartitions = (yyvsp[0].i32); }
#line 1957 "y.tab.c"
    break;

  case 64: /* define_maxusers: MAXUSERS int32 int32 int32  */
#line 436 "gram.y"
                                        { setdefmaxusers((yyvsp[-2].i32), (yyvsp[-1].i32), (yyvsp[0].i32)); }
#line 1963 "y.tab.c"
    break;

  case 66: /* define_pseudo: DEFPSEUDO devbase interface_opt depend_list  */
#line 446 "gram.y"
                                        { defdev((yyvsp[-2].devb), (yyvsp[-1].loclist), (yyvsp[0].attrlist), 1); }
#line 1969 "y.tab.c"
    break;

  case 67: /* define_pseudodev: DEFPSEUDODEV devbase interface_opt depend_list  */
#line 451 "gram.y"
                                        { defdev((yyvsp[-2].devb), (yyvsp[-1].loclist), (yyvsp[0].attrlist), 2); }
#line 1975 "y.tab.c"
    break;

  case 69: /* define_version: VERSION int32  */
#line 459 "gram.y"
                                { setversion((yyvsp[0].i32)); }
#line 1981 "y.tab.c"
    break;

  case 70: /* fopts: %empty  */
#line 464 "gram.y"
                                        { (yyval.condexpr) = NULL; }
#line 1987 "y.tab.c"
    break;

  case 71: /* fopts: condexpr  */
#line 465 "gram.y"
                                        { (yyval.condexpr) = (yyvsp[0].condexpr); }
#line 1993 "y.tab.c"
    break;

  case 72: /* fflags: %empty  */
#line 470 "gram.y"
                                        { (yyval.flag) = 0; }
#line 1999 "y.tab.c"
    break;

  case 73: /* fflags: fflags fflag  */
#line 471 "gram.y"
                                        { (yyval.flag) = (yyvsp[-1].flag) | (yyvsp[0].flag); }
#line 2005 "y.tab.c"
    break;

  case 74: /* fflag: NEEDS_COUNT  */
#line 476 "gram.y"
                                        { (yyval.flag) = FI_NEEDSCOUNT; }
#line 2011 "y.tab.c"
    break;

  case 75: /* fflag: NEEDS_FLAG  */
#line 477 "gram.y"
                                        { (yyval.flag) = FI_NEEDSFLAG; }
#line 2017 "y.tab.c"
    break;

  case 76: /* rule: %empty  */
#line 482 "gram.y"
                                        { (yyval.str) = NULL; }
#line 2023 "y.tab.c"
    break;

  case 77: /* rule: COMPILE_WITH stringvalue  */
#line 483 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2029 "y.tab.c"
    break;

  case 78: /* oflags: %empty  */
#line 488 "gram.y"
                                        { (yyval.flag) = 0; }
#line 2035 "y.tab.c"
    break;

  case 79: /* oflags: oflags oflag  */
#line 489 "gram.y"
                                        { (yyval.flag) = (yyvsp[-1].flag) | (yyvsp[0].flag); }
#line 2041 "y.tab.c"
    break;

  case 80: /* oflag: NEEDS_FLAG  */
#line 494 "gram.y"
                                        { (yyval.flag) = FI_NEEDSFLAG; }
#line 2047 "y.tab.c"
    break;

  case 81: /* device_major_char: %empty  */
#line 499 "gram.y"
                                        { (yyval.devmajor) = -1; }
#line 2053 "y.tab.c"
    break;

  case 82: /* device_major_char: CHAR int32  */
#line 500 "gram.y"
                                        { (yyval.devmajor) = (yyvsp[0].i32); }
#line 2059 "y.tab.c"
    break;

  case 83: /* device_major_block: %empty  */
#line 505 "gram.y"
                                        { (yyval.devmajor) = -1; }
#line 2065 "y.tab.c"
    break;

  case 84: /* device_major_block: BLOCK int32  */
#line 506 "gram.y"
                                        { (yyval.devmajor) = (yyvsp[0].i32); }
#line 2071 "y.tab.c"
    break;

  case 85: /* devnodes: %empty  */
#line 511 "gram.y"
                                        { (yyval.list) = new_s("DEVNODE_DONTBOTHER"); }
#line 2077 "y.tab.c"
    break;

  case 86: /* devnodes: devnodetype ',' devnodeflags  */
#line 512 "gram.y"
                                        { (yyval.list) = nvcat((yyvsp[-2].list), (yyvsp[0].list)); }
#line 2083 "y.tab.c"
    break;

  case 87: /* devnodes: devnodetype  */
#line 513 "gram.y"
                                        { (yyval.list) = (yyvsp[0].list); }
#line 2089 "y.tab.c"
    break;

  case 88: /* devnodetype: SINGLE  */
#line 518 "gram.y"
                                        { (yyval.list) = new_s("DEVNODE_SINGLE"); }
#line 2095 "y.tab.c"
    break;

  case 89: /* devnodetype: VECTOR '=' devnode_dims  */
#line 519 "gram.y"
                                   { (yyval.list) = nvcat(new_s("DEVNODE_VECTOR"), (yyvsp[0].list)); }
#line 2101 "y.tab.c"
    break;

  case 90: /* devnode_dims: NUMBER  */
#line 524 "gram.y"
                                        { (yyval.list) = new_i((yyvsp[0].num).val); }
#line 2107 "y.tab.c"
    break;

  case 91: /* devnode_dims: NUMBER ':' NUMBER  */
#line 525 "gram.y"
                                        {
		struct nvlist *__nv1, *__nv2;

		__nv1 = new_i((yyvsp[-2].num).val);
		__nv2 = new_i((yyvsp[0].num).val);
		(yyval.list) = nvcat(__nv1, __nv2);
	  }
#line 2119 "y.tab.c"
    break;

  case 92: /* devnodeflags: LINKZERO  */
#line 536 "gram.y"
                                        { (yyval.list) = new_s("DEVNODE_FLAG_LINKZERO");}
#line 2125 "y.tab.c"
    break;

  case 93: /* deffses: deffs  */
#line 541 "gram.y"
                                        { (yyval.list) = new_n((yyvsp[0].str)); }
#line 2131 "y.tab.c"
    break;

  case 94: /* deffses: deffses deffs  */
#line 542 "gram.y"
                                        { (yyval.list) = new_nx((yyvsp[0].str), (yyvsp[-1].list)); }
#line 2137 "y.tab.c"
    break;

  case 95: /* deffs: WORD  */
#line 547 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2143 "y.tab.c"
    break;

  case 96: /* interface_opt: %empty  */
#line 552 "gram.y"
                                        { (yyval.loclist) = NULL; }
#line 2149 "y.tab.c"
    break;

  case 97: /* interface_opt: '{' '}'  */
#line 553 "gram.y"
                                        { (yyval.loclist) = present_loclist(NULL); }
#line 2155 "y.tab.c"
    break;

  case 98: /* interface_opt: '{' loclist '}'  */
#line 554 "gram.y"
                                        { (yyval.loclist) = present_loclist((yyvsp[-1].loclist)); }
#line 2161 "y.tab.c"
    break;

  case 99: /* loclist: locdef  */
#line 564 "gram.y"
                                        { (yyval.loclist) = (yyvsp[0].loclist); }
#line 2167 "y.tab.c"
    break;

  case 100: /* loclist: locdef ',' loclist  */
#line 565 "gram.y"
                                        { (yyval.loclist) = (yyvsp[-2].loclist); app((yyvsp[-2].loclist), (yyvsp[0].loclist)); }
#line 2173 "y.tab.c"
    break;

  case 101: /* locdef: locname locdefault  */
#line 574 "gram.y"
                                        { (yyval.loclist) = MK3(loc, (yyvsp[-1].str), (yyvsp[0].str), 0); }
#line 2179 "y.tab.c"
    break;

  case 102: /* locdef: locname  */
#line 575 "gram.y"
                                        { (yyval.loclist) = MK3(loc, (yyvsp[0].str), NULL, 0); }
#line 2185 "y.tab.c"
    break;

  case 103: /* locdef: '[' locname locdefault ']'  */
#line 576 "gram.y"
                                        { (yyval.loclist) = MK3(loc, (yyvsp[-2].str), (yyvsp[-1].str), 1); }
#line 2191 "y.tab.c"
    break;

  case 104: /* locdef: locname '[' int32 ']'  */
#line 577 "gram.y"
                                { (yyval.loclist) = locarray((yyvsp[-3].str), (yyvsp[-1].i32), NULL, 0); }
#line 2197 "y.tab.c"
    break;

  case 105: /* locdef: locname '[' int32 ']' locdefaults  */
#line 579 "gram.y"
                                        { (yyval.loclist) = locarray((yyvsp[-4].str), (yyvsp[-2].i32), (yyvsp[0].loclist), 0); }
#line 2203 "y.tab.c"
    break;

  case 106: /* locdef: '[' locname '[' int32 ']' locdefaults ']'  */
#line 581 "gram.y"
                                        { (yyval.loclist) = locarray((yyvsp[-5].str), (yyvsp[-3].i32), (yyvsp[-1].loclist), 1); }
#line 2209 "y.tab.c"
    break;

  case 107: /* locname: WORD  */
#line 586 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2215 "y.tab.c"
    break;

  case 108: /* locname: QSTRING  */
#line 587 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2221 "y.tab.c"
    break;

  case 109: /* locdefault: '=' value  */
#line 592 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2227 "y.tab.c"
    break;

  case 110: /* locdefaults: '=' '{' values '}'  */
#line 597 "gram.y"
                                        { (yyval.loclist) = (yyvsp[-1].loclist); }
#line 2233 "y.tab.c"
    break;

  case 111: /* depend_list: %empty  */
#line 602 "gram.y"
                                        { (yyval.attrlist) = NULL; }
#line 2239 "y.tab.c"
    break;

  case 112: /* depend_list: ':' depends  */
#line 603 "gram.y"
                                        { (yyval.attrlist) = (yyvsp[0].attrlist); }
#line 2245 "y.tab.c"
    break;

  case 113: /* depends: depend  */
#line 608 "gram.y"
                                        { (yyval.attrlist) = MK2(attrlist, NULL, (yyvsp[0].attr)); }
#line 2251 "y.tab.c"
    break;

  case 114: /* depends: depends ',' depend  */
#line 609 "gram.y"
                                        { (yyval.attrlist) = MK2(attrlist, (yyvsp[-2].attrlist), (yyvsp[0].attr)); }
#line 2257 "y.tab.c"
    break;

  case 115: /* depend: WORD  */
#line 614 "gram.y"
                                        { (yyval.attr) = refattr((yyvsp[0].str)); }
#line 2263 "y.tab.c"
    break;

  case 116: /* optdepend_list: %empty  */
#line 619 "gram.y"
                                        { (yyval.list) = NULL; }
#line 2269 "y.tab.c"
    break;

  case 117: /* optdepend_list: ':' optdepends  */
#line 620 "gram.y"
                                        { (yyval.list) = (yyvsp[0].list); }
#line 2275 "y.tab.c"
    break;

  case 118: /* optdepends: optdepend  */
#line 625 "gram.y"
                                        { (yyval.list) = new_n((yyvsp[0].str)); }
#line 2281 "y.tab.c"
    break;

  case 119: /* optdepends: optdepends ',' optdepend  */
#line 626 "gram.y"
                                        { (yyval.list) = new_nx((yyvsp[0].str), (yyvsp[-2].list)); }
#line 2287 "y.tab.c"
    break;

  case 120: /* optdepend: WORD  */
#line 631 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2293 "y.tab.c"
    break;

  case 121: /* atlist: atname  */
#line 637 "gram.y"
                                        { (yyval.list) = new_n((yyvsp[0].str)); }
#line 2299 "y.tab.c"
    break;

  case 122: /* atlist: atlist ',' atname  */
#line 638 "gram.y"
                                        { (yyval.list) = new_nx((yyvsp[0].str), (yyvsp[-2].list)); }
#line 2305 "y.tab.c"
    break;

  case 123: /* atname: WORD  */
#line 643 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2311 "y.tab.c"
    break;

  case 124: /* atname: ROOT  */
#line 644 "gram.y"
                                        { (yyval.str) = NULL; }
#line 2317 "y.tab.c"
    break;

  case 125: /* defopts: defopt  */
#line 649 "gram.y"
                                        { (yyval.defoptlist) = (yyvsp[0].defoptlist); }
#line 2323 "y.tab.c"
    break;

  case 126: /* defopts: defopts defopt  */
#line 650 "gram.y"
                                        { (yyval.defoptlist) = defoptlist_append((yyvsp[0].defoptlist), (yyvsp[-1].defoptlist)); }
#line 2329 "y.tab.c"
    break;

  case 127: /* defopt: WORD  */
#line 655 "gram.y"
                                        { (yyval.defoptlist) = MK3(defoptlist, (yyvsp[0].str), NULL, NULL); }
#line 2335 "y.tab.c"
    break;

  case 128: /* defopt: WORD '=' value  */
#line 656 "gram.y"
                                        { (yyval.defoptlist) = MK3(defoptlist, (yyvsp[-2].str), (yyvsp[0].str), NULL); }
#line 2341 "y.tab.c"
    break;

  case 129: /* defopt: WORD COLONEQ value  */
#line 657 "gram.y"
                                        { (yyval.defoptlist) = MK3(defoptlist, (yyvsp[-2].str), NULL, (yyvsp[0].str)); }
#line 2347 "y.tab.c"
    break;

  case 130: /* defopt: WORD '=' value COLONEQ value  */
#line 658 "gram.y"
                                        { (yyval.defoptlist) = MK3(defoptlist, (yyvsp[-4].str), (yyvsp[-2].str), (yyvsp[0].str)); }
#line 2353 "y.tab.c"
    break;

  case 133: /* condmkoption: condexpr mkvarname PLUSEQ value  */
#line 669 "gram.y"
                                        { appendcondmkoption((yyvsp[-3].condexpr), (yyvsp[-2].str), (yyvsp[0].str)); }
#line 2359 "y.tab.c"
    break;

  case 134: /* devbase: WORD  */
#line 674 "gram.y"
                                        { (yyval.devb) = getdevbase((yyvsp[0].str)); }
#line 2365 "y.tab.c"
    break;

  case 135: /* devattach_opt: %empty  */
#line 679 "gram.y"
                                        { (yyval.deva) = NULL; }
#line 2371 "y.tab.c"
    break;

  case 136: /* devattach_opt: WITH WORD  */
#line 680 "gram.y"
                                        { (yyval.deva) = getdevattach((yyvsp[0].str)); }
#line 2377 "y.tab.c"
    break;

  case 139: /* majordef: devbase '=' int32  */
#line 692 "gram.y"
                                        { setmajor((yyvsp[-2].devb), (yyvsp[0].i32)); }
#line 2383 "y.tab.c"
    break;

  case 140: /* int32: NUMBER  */
#line 696 "gram.y"
                {
		if ((yyvsp[0].num).val > INT_MAX || (yyvsp[0].num).val < INT_MIN)
			cfgerror("overflow %" PRId64, (yyvsp[0].num).val);
		else
			(yyval.i32) = (int32_t)(yyvsp[0].num).val;
	}
#line 2394 "y.tab.c"
    break;

  case 144: /* selections: selections selection '\n'  */
#line 719 "gram.y"
                                        { wrap_continue(); }
#line 2400 "y.tab.c"
    break;

  case 145: /* selections: selections error '\n'  */
#line 720 "gram.y"
                                        { wrap_cleanup(); }
#line 2406 "y.tab.c"
    break;

  case 167: /* select_attr: SELECT WORD  */
#line 749 "gram.y"
                                        { addattr((yyvsp[0].str)); }
#line 2412 "y.tab.c"
    break;

  case 168: /* select_no_attr: no SELECT WORD  */
#line 753 "gram.y"
                                        { delattr((yyvsp[0].str), (yyvsp[-2].i32)); }
#line 2418 "y.tab.c"
    break;

  case 169: /* $@1: %empty  */
#line 757 "gram.y"
                       { nowarn = (yyvsp[-1].i32); }
#line 2424 "y.tab.c"
    break;

  case 170: /* select_no_filesystems: no FILE_SYSTEM $@1 no_fs_list  */
#line 757 "gram.y"
                                                   { nowarn = 0; }
#line 2430 "y.tab.c"
    break;

  case 172: /* $@2: %empty  */
#line 765 "gram.y"
                       { nowarn = (yyvsp[-1].i32); }
#line 2436 "y.tab.c"
    break;

  case 173: /* select_no_makeoptions: no MAKEOPTIONS $@2 no_mkopt_list  */
#line 765 "gram.y"
                                                      { nowarn = 0; }
#line 2442 "y.tab.c"
    break;

  case 175: /* $@3: %empty  */
#line 773 "gram.y"
                   { nowarn = (yyvsp[-1].i32); }
#line 2448 "y.tab.c"
    break;

  case 176: /* select_no_options: no OPTIONS $@3 no_opt_list  */
#line 773 "gram.y"
                                                { nowarn = 0; }
#line 2454 "y.tab.c"
    break;

  case 178: /* select_maxusers: MAXUSERS int32  */
#line 781 "gram.y"
                                        { setmaxusers((yyvsp[0].i32)); }
#line 2460 "y.tab.c"
    break;

  case 179: /* select_ident: IDENT stringvalue  */
#line 785 "gram.y"
                                        { setident((yyvsp[0].str)); }
#line 2466 "y.tab.c"
    break;

  case 180: /* select_no_ident: no IDENT  */
#line 789 "gram.y"
                                        { setident(NULL); }
#line 2472 "y.tab.c"
    break;

  case 181: /* select_config: CONFIG conf root_spec sysparam_list  */
#line 794 "gram.y"
                                        { addconf(&conf); }
#line 2478 "y.tab.c"
    break;

  case 182: /* select_no_config: no CONFIG WORD  */
#line 798 "gram.y"
                                        { delconf((yyvsp[0].str), (yyvsp[-2].i32)); }
#line 2484 "y.tab.c"
    break;

  case 183: /* select_no_pseudodev: no PSEUDO_DEVICE WORD  */
#line 802 "gram.y"
                                        { delpseudo((yyvsp[0].str), (yyvsp[-2].i32)); }
#line 2490 "y.tab.c"
    break;

  case 184: /* select_pseudodev: PSEUDO_DEVICE WORD npseudo  */
#line 806 "gram.y"
                                        { addpseudo((yyvsp[-1].str), (yyvsp[0].i32)); }
#line 2496 "y.tab.c"
    break;

  case 185: /* select_pseudoroot: PSEUDO_ROOT device_instance  */
#line 810 "gram.y"
                                        { addpseudoroot((yyvsp[0].str)); }
#line 2502 "y.tab.c"
    break;

  case 186: /* select_no_device_instance_attachment: no device_instance AT attachment  */
#line 815 "gram.y"
                                        { deldevi((yyvsp[-2].str), (yyvsp[0].str), (yyvsp[-3].i32)); }
#line 2508 "y.tab.c"
    break;

  case 187: /* select_no_device_attachment: no DEVICE AT attachment  */
#line 819 "gram.y"
                                        { deldeva((yyvsp[0].str), (yyvsp[-3].i32)); }
#line 2514 "y.tab.c"
    break;

  case 188: /* select_no_device_instance: no device_instance  */
#line 823 "gram.y"
                                        { deldev((yyvsp[0].str), (yyvsp[-1].i32)); }
#line 2520 "y.tab.c"
    break;

  case 189: /* select_device_instance: device_instance AT attachment locators device_flags  */
#line 828 "gram.y"
                                        { adddev((yyvsp[-4].str), (yyvsp[-2].str), (yyvsp[-1].loclist), (yyvsp[0].i32)); }
#line 2526 "y.tab.c"
    break;

  case 192: /* fsoption: WORD  */
#line 839 "gram.y"
                                        { addfsoption((yyvsp[0].str)); }
#line 2532 "y.tab.c"
    break;

  case 195: /* no_fsoption: WORD  */
#line 850 "gram.y"
                                        { delfsoption((yyvsp[0].str), nowarn); }
#line 2538 "y.tab.c"
    break;

  case 198: /* mkoption: mkvarname '=' value  */
#line 862 "gram.y"
                                        { addmkoption((yyvsp[-2].str), (yyvsp[0].str)); }
#line 2544 "y.tab.c"
    break;

  case 199: /* mkoption: mkvarname PLUSEQ value  */
#line 863 "gram.y"
                                        { appendmkoption((yyvsp[-2].str), (yyvsp[0].str)); }
#line 2550 "y.tab.c"
    break;

  case 202: /* no_mkoption: WORD  */
#line 875 "gram.y"
                                        { delmkoption((yyvsp[0].str), nowarn); }
#line 2556 "y.tab.c"
    break;

  case 205: /* option: WORD  */
#line 886 "gram.y"
                                        { addoption((yyvsp[0].str), NULL); }
#line 2562 "y.tab.c"
    break;

  case 206: /* option: WORD '=' value  */
#line 887 "gram.y"
                                        { addoption((yyvsp[-2].str), (yyvsp[0].str)); }
#line 2568 "y.tab.c"
    break;

  case 209: /* no_option: WORD  */
#line 898 "gram.y"
                                        { deloption((yyvsp[0].str), nowarn); }
#line 2574 "y.tab.c"
    break;

  case 210: /* conf: WORD  */
#line 903 "gram.y"
                                        {
		conf.cf_name = (yyvsp[0].str);
		conf.cf_where.w_srcline = currentline();
		conf.cf_fstype = NULL;
		conf.cf_root = NULL;
		conf.cf_dump = NULL;
	}
#line 2586 "y.tab.c"
    break;

  case 211: /* root_spec: ROOT on_opt dev_spec  */
#line 914 "gram.y"
                                        { setconf(&conf.cf_root, "root", (yyvsp[0].list)); }
#line 2592 "y.tab.c"
    break;

  case 212: /* root_spec: ROOT on_opt dev_spec fs_spec  */
#line 915 "gram.y"
                                        { setconf(&conf.cf_root, "root", (yyvsp[-1].list)); }
#line 2598 "y.tab.c"
    break;

  case 213: /* dev_spec: '?'  */
#line 920 "gram.y"
                                        { (yyval.list) = new_spi(intern("?"),
					    NULL,
					    (long long)NODEV); }
#line 2606 "y.tab.c"
    break;

  case 214: /* dev_spec: QSTRING  */
#line 923 "gram.y"
                                        { (yyval.list) = new_spi((yyvsp[0].str),
					    __UNCONST("spec"),
					    (long long)NODEV); }
#line 2614 "y.tab.c"
    break;

  case 215: /* dev_spec: WORD  */
#line 926 "gram.y"
                                        { (yyval.list) = new_spi((yyvsp[0].str),
					    NULL,
					    (long long)NODEV); }
#line 2622 "y.tab.c"
    break;

  case 216: /* dev_spec: major_minor  */
#line 929 "gram.y"
                                        { (yyval.list) = new_si(NULL, (yyvsp[0].val)); }
#line 2628 "y.tab.c"
    break;

  case 217: /* major_minor: MAJOR NUMBER MINOR NUMBER  */
#line 934 "gram.y"
                                        { (yyval.val) = (int64_t)makedev((yyvsp[-2].num).val, (yyvsp[0].num).val); }
#line 2634 "y.tab.c"
    break;

  case 218: /* fs_spec: TYPE '?'  */
#line 939 "gram.y"
                                   { setfstype(&conf.cf_fstype, intern("?")); }
#line 2640 "y.tab.c"
    break;

  case 219: /* fs_spec: TYPE WORD  */
#line 940 "gram.y"
                                        { setfstype(&conf.cf_fstype, (yyvsp[0].str)); }
#line 2646 "y.tab.c"
    break;

  case 222: /* sysparam: DUMPS on_opt dev_spec  */
#line 951 "gram.y"
                                       { setconf(&conf.cf_dump, "dumps", (yyvsp[0].list)); }
#line 2652 "y.tab.c"
    break;

  case 223: /* npseudo: %empty  */
#line 956 "gram.y"
                                        { (yyval.i32) = 1; }
#line 2658 "y.tab.c"
    break;

  case 224: /* npseudo: int32  */
#line 957 "gram.y"
                                        { (yyval.i32) = (yyvsp[0].i32); }
#line 2664 "y.tab.c"
    break;

  case 225: /* device_instance: WORD  */
#line 962 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2670 "y.tab.c"
    break;

  case 226: /* device_instance: WORD '*'  */
#line 963 "gram.y"
                                        { (yyval.str) = starref((yyvsp[-1].str)); }
#line 2676 "y.tab.c"
    break;

  case 227: /* attachment: ROOT  */
#line 968 "gram.y"
                                        { (yyval.str) = NULL; }
#line 2682 "y.tab.c"
    break;

  case 228: /* attachment: WORD  */
#line 969 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2688 "y.tab.c"
    break;

  case 229: /* attachment: WORD '?'  */
#line 970 "gram.y"
                                        { (yyval.str) = wildref((yyvsp[-1].str)); }
#line 2694 "y.tab.c"
    break;

  case 230: /* locators: %empty  */
#line 975 "gram.y"
                                        { (yyval.loclist) = NULL; }
#line 2700 "y.tab.c"
    break;

  case 231: /* locators: locators locator  */
#line 976 "gram.y"
                                        { (yyval.loclist) = (yyvsp[0].loclist); app((yyvsp[0].loclist), (yyvsp[-1].loclist)); }
#line 2706 "y.tab.c"
    break;

  case 232: /* locator: WORD '?'  */
#line 981 "gram.y"
                                        { (yyval.loclist) = MK3(loc, (yyvsp[-1].str), NULL, 0); }
#line 2712 "y.tab.c"
    break;

  case 233: /* locator: WORD values  */
#line 982 "gram.y"
                                        { (yyval.loclist) = namelocvals((yyvsp[-1].str), (yyvsp[0].loclist)); }
#line 2718 "y.tab.c"
    break;

  case 234: /* device_flags: %empty  */
#line 987 "gram.y"
                                        { (yyval.i32) = 0; }
#line 2724 "y.tab.c"
    break;

  case 235: /* device_flags: FLAGS int32  */
#line 988 "gram.y"
                                        { (yyval.i32) = (yyvsp[0].i32); }
#line 2730 "y.tab.c"
    break;

  case 238: /* cond_or_expr: cond_or_expr '|' cond_and_expr  */
#line 1011 "gram.y"
                                                { (yyval.condexpr) = MKF2(cx, or, (yyvsp[-2].condexpr), (yyvsp[0].condexpr)); }
#line 2736 "y.tab.c"
    break;

  case 240: /* cond_and_expr: cond_and_expr '&' cond_prefix_expr  */
#line 1016 "gram.y"
                                                { (yyval.condexpr) = MKF2(cx, and, (yyvsp[-2].condexpr), (yyvsp[0].condexpr)); }
#line 2742 "y.tab.c"
    break;

  case 242: /* cond_base_expr: condatom  */
#line 1026 "gram.y"
                                        { (yyval.condexpr) = (yyvsp[0].condexpr); }
#line 2748 "y.tab.c"
    break;

  case 243: /* cond_base_expr: '!' condatom  */
#line 1027 "gram.y"
                                        { (yyval.condexpr) = MKF1(cx, not, (yyvsp[0].condexpr)); }
#line 2754 "y.tab.c"
    break;

  case 244: /* cond_base_expr: '(' condexpr ')'  */
#line 1028 "gram.y"
                                        { (yyval.condexpr) = (yyvsp[-1].condexpr); }
#line 2760 "y.tab.c"
    break;

  case 245: /* condatom: WORD  */
#line 1033 "gram.y"
                                        { (yyval.condexpr) = MKF1(cx, atom, (yyvsp[0].str)); }
#line 2766 "y.tab.c"
    break;

  case 246: /* mkvarname: QSTRING  */
#line 1044 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2772 "y.tab.c"
    break;

  case 247: /* mkvarname: WORD  */
#line 1045 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2778 "y.tab.c"
    break;

  case 248: /* optfile_opt: %empty  */
#line 1050 "gram.y"
                                        { (yyval.str) = NULL; }
#line 2784 "y.tab.c"
    break;

  case 249: /* optfile_opt: filename  */
#line 1051 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2790 "y.tab.c"
    break;

  case 250: /* filename: QSTRING  */
#line 1056 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2796 "y.tab.c"
    break;

  case 251: /* filename: PATHNAME  */
#line 1057 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2802 "y.tab.c"
    break;

  case 252: /* value: QSTRING  */
#line 1062 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2808 "y.tab.c"
    break;

  case 253: /* value: WORD  */
#line 1063 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2814 "y.tab.c"
    break;

  case 254: /* value: EMPTYSTRING  */
#line 1064 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2820 "y.tab.c"
    break;

  case 255: /* value: signed_number  */
#line 1065 "gram.y"
                                        {
		char bf[40];

		(void)snprintf(bf, sizeof(bf), FORMAT((yyvsp[0].num)), (long long)(yyvsp[0].num).val);
		(yyval.str) = intern(bf);
	  }
#line 2831 "y.tab.c"
    break;

  case 256: /* stringvalue: QSTRING  */
#line 1075 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2837 "y.tab.c"
    break;

  case 257: /* stringvalue: WORD  */
#line 1076 "gram.y"
                                        { (yyval.str) = (yyvsp[0].str); }
#line 2843 "y.tab.c"
    break;

  case 258: /* values: value  */
#line 1082 "gram.y"
                                        { (yyval.loclist) = MKF2(loc, val, (yyvsp[0].str), NULL); }
#line 2849 "y.tab.c"
    break;

  case 259: /* values: value ',' values  */
#line 1083 "gram.y"
                                        { (yyval.loclist) = MKF2(loc, val, (yyvsp[-2].str), (yyvsp[0].loclist)); }
#line 2855 "y.tab.c"
    break;

  case 260: /* signed_number: NUMBER  */
#line 1088 "gram.y"
                                        { (yyval.num) = (yyvsp[0].num); }
#line 2861 "y.tab.c"
    break;

  case 261: /* signed_number: '-' NUMBER  */
#line 1089 "gram.y"
                                        { (yyval.num).fmt = (yyvsp[0].num).fmt; (yyval.num).val = -(yyvsp[0].num).val; }
#line 2867 "y.tab.c"
    break;


#line 2871 "y.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 1098 "gram.y"


void
yyerror(const char *s)
{

	cfgerror("%s", s);
}

/************************************************************/

/*
 * Wrap allocations that live on the parser stack so that we can free
 * them again on error instead of leaking.
 */

#define MAX_WRAP 1000

struct wrap_entry {
	void *ptr;
	unsigned typecode;
};

static struct wrap_entry wrapstack[MAX_WRAP];
static unsigned wrap_depth;

/*
 * Remember pointer PTR with type-code CODE.
 */
static void
wrap_alloc(void *ptr, unsigned code)
{
	unsigned pos;

	if (wrap_depth >= MAX_WRAP) {
		panic("allocation wrapper stack overflow");
	}
	pos = wrap_depth++;
	wrapstack[pos].ptr = ptr;
	wrapstack[pos].typecode = code;
}

/*
 * We succeeded; commit to keeping everything that's been allocated so
 * far and clear the stack.
 */
static void
wrap_continue(void)
{
	wrap_depth = 0;
}

/*
 * We failed; destroy all the objects allocated.
 */
static void
wrap_cleanup(void)
{
	unsigned i;

	/*
	 * Destroy each item. Note that because everything allocated
	 * is entered on the list separately, lists and trees need to
	 * have their links blanked before being destroyed. Also note
	 * that strings are interned elsewhere and not handled by this
	 * mechanism.
	 */

	for (i=0; i<wrap_depth; i++) {
		switch (wrapstack[i].typecode) {
		    case WRAP_CODE_nvlist:
			nvfree(wrapstack[i].ptr);
			break;
		    case WRAP_CODE_defoptlist:
			{
				struct defoptlist *dl = wrapstack[i].ptr;

				dl->dl_next = NULL;
				defoptlist_destroy(dl);
			}
			break;
		    case WRAP_CODE_loclist:
			{
				struct loclist *ll = wrapstack[i].ptr;

				ll->ll_next = NULL;
				loclist_destroy(ll);
			}
			break;
		    case WRAP_CODE_attrlist:
			{
				struct attrlist *al = wrapstack[i].ptr;

				al->al_next = NULL;
				al->al_this = NULL;
				attrlist_destroy(al);
			}
			break;
		    case WRAP_CODE_condexpr:
			{
				struct condexpr *cx = wrapstack[i].ptr;

				cx->cx_type = CX_ATOM;
				cx->cx_atom = NULL;
				condexpr_destroy(cx);
			}
			break;
		    default:
			panic("invalid code %u on allocation wrapper stack",
			      wrapstack[i].typecode);
		}
	}

	wrap_depth = 0;
}

/*
 * Instantiate the wrapper functions.
 *
 * Each one calls wrap_alloc to save the pointer and then returns the
 * pointer again; these need to be generated with the preprocessor in
 * order to be typesafe.
 */
#define DEF_ALLOCWRAP(t) \
	static struct t *				\
	wrap_mk_##t(struct t *arg)			\
	{						\
		wrap_alloc(arg, WRAP_CODE_##t);		\
		return arg;				\
	}

DEF_ALLOCWRAP(nvlist);
DEF_ALLOCWRAP(defoptlist);
DEF_ALLOCWRAP(loclist);
DEF_ALLOCWRAP(attrlist);
DEF_ALLOCWRAP(condexpr);

/************************************************************/

/*
 * Data constructors
 *
 * (These are *beneath* the allocation wrappers.)
 */

static struct defoptlist *
mk_defoptlist(const char *name, const char *val, const char *lintval)
{
	return defoptlist_create(name, val, lintval);
}

static struct loclist *
mk_loc(const char *name, const char *str, long long num)
{
	return loclist_create(name, str, num);
}

static struct loclist *
mk_loc_val(const char *str, struct loclist *next)
{
	struct loclist *ll;

	ll = mk_loc(NULL, str, 0);
	ll->ll_next = next;
	return ll;
}

static struct attrlist *
mk_attrlist(struct attrlist *next, struct attr *a)
{
	return attrlist_cons(next, a);
}

static struct condexpr *
mk_cx_atom(const char *s)
{
	struct condexpr *cx;

	cx = condexpr_create(CX_ATOM);
	cx->cx_atom = s;
	return cx;
}

static struct condexpr *
mk_cx_not(struct condexpr *sub)
{
	struct condexpr *cx;

	cx = condexpr_create(CX_NOT);
	cx->cx_not = sub;
	return cx;
}

static struct condexpr *
mk_cx_and(struct condexpr *left, struct condexpr *right)
{
	struct condexpr *cx;

	cx = condexpr_create(CX_AND);
	cx->cx_and.left = left;
	cx->cx_and.right = right;
	return cx;
}

static struct condexpr *
mk_cx_or(struct condexpr *left, struct condexpr *right)
{
	struct condexpr *cx;

	cx = condexpr_create(CX_OR);
	cx->cx_or.left = left;
	cx->cx_or.right = right;
	return cx;
}

/************************************************************/

static void
setmachine(const char *mch, const char *mcharch, struct nvlist *mchsubarches,
	int isioconf)
{
	char buf[MAXPATHLEN];
	struct nvlist *nv;

	if (isioconf) {
		if (include(_PATH_DEVNULL, ENDDEFS, 0, 0) != 0)
			exit(1);
		ioconfname = mch;
		return;
	}

	machine = mch;
	machinearch = mcharch;
	machinesubarches = mchsubarches;

	/*
	 * Define attributes for all the given names
	 */
	if (defattr(machine, NULL, NULL, 0) != 0 ||
	    (machinearch != NULL &&
	     defattr(machinearch, NULL, NULL, 0) != 0))
		exit(1);
	for (nv = machinesubarches; nv != NULL; nv = nv->nv_next) {
		if (defattr(nv->nv_name, NULL, NULL, 0) != 0)
			exit(1);
	}

	/*
	 * Set up the file inclusion stack.  This empty include tells
	 * the parser there are no more device definitions coming.
	 */
	if (include(_PATH_DEVNULL, ENDDEFS, 0, 0) != 0)
		exit(1);

	/* Include arch/${MACHINE}/conf/files.${MACHINE} */
	(void)snprintf(buf, sizeof(buf), "arch/%s/conf/files.%s",
	    machine, machine);
	if (include(buf, ENDFILE, 0, 0) != 0)
		exit(1);

	/* Include any arch/${MACHINE_SUBARCH}/conf/files.${MACHINE_SUBARCH} */
	for (nv = machinesubarches; nv != NULL; nv = nv->nv_next) {
		(void)snprintf(buf, sizeof(buf), "arch/%s/conf/files.%s",
		    nv->nv_name, nv->nv_name);
		if (include(buf, ENDFILE, 0, 0) != 0)
			exit(1);
	}

	/* Include any arch/${MACHINE_ARCH}/conf/files.${MACHINE_ARCH} */
	if (machinearch != NULL)
		(void)snprintf(buf, sizeof(buf), "arch/%s/conf/files.%s",
		    machinearch, machinearch);
	else
		strlcpy(buf, _PATH_DEVNULL, sizeof(buf));
	if (include(buf, ENDFILE, 0, 0) != 0)
		exit(1);

	/*
	 * Include the global conf/files.  As the last thing
	 * pushed on the stack, it will be processed first.
	 */
	if (include("conf/files", ENDFILE, 0, 0) != 0)
		exit(1);

	oktopackage = 1;
}

static void
check_maxpart(void)
{

	if (maxpartitions <= 0 && ioconfname == NULL) {
		stop("cannot proceed without maxpartitions specifier");
	}
}

void
check_version(void)
{
	/*
	 * In essence, version is 0 and is not supported anymore
	 */
	if (version < CONFIG_MINVERSION)
		stop("your sources are out of date -- please update.");
}

/*
 * Prepend a blank entry to the locator definitions so the code in
 * sem.c can distinguish "empty locator list" from "no locator list".
 * XXX gross.
 */
static struct loclist *
present_loclist(struct loclist *ll)
{
	struct loclist *ret;

	ret = MK3(loc, "", NULL, 0);
	ret->ll_next = ll;
	return ret;
}

static void
app(struct loclist *p, struct loclist *q)
{
	while (p->ll_next)
		p = p->ll_next;
	p->ll_next = q;
}

static struct loclist *
locarray(const char *name, int count, struct loclist *adefs, int opt)
{
	struct loclist *defs = adefs;
	struct loclist **p;
	char buf[200];
	int i;

	if (count <= 0) {
		fprintf(stderr, "config: array with <= 0 size: %s\n", name);
		exit(1);
	}
	p = &defs;
	for(i = 0; i < count; i++) {
		if (*p == NULL)
			*p = MK3(loc, NULL, "0", 0);
		snprintf(buf, sizeof(buf), "%s%c%d", name, ARRCHR, i);
		(*p)->ll_name = i == 0 ? name : intern(buf);
		(*p)->ll_num = i > 0 || opt;
		p = &(*p)->ll_next;
	}
	*p = 0;
	return defs;
}


static struct loclist *
namelocvals(const char *name, struct loclist *vals)
{
	struct loclist *p;
	char buf[200];
	int i;

	for (i = 0, p = vals; p; i++, p = p->ll_next) {
		snprintf(buf, sizeof(buf), "%s%c%d", name, ARRCHR, i);
		p->ll_name = i == 0 ? name : intern(buf);
	}
	return vals;
}

