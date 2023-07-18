/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_Y_TAB_H_INCLUDED
# define YY_YY_Y_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    AND = 258,                     /* AND  */
    AT = 259,                      /* AT  */
    ATTACH = 260,                  /* ATTACH  */
    BLOCK = 261,                   /* BLOCK  */
    BUILD = 262,                   /* BUILD  */
    CHAR = 263,                    /* CHAR  */
    COLONEQ = 264,                 /* COLONEQ  */
    COMPILE_WITH = 265,            /* COMPILE_WITH  */
    CONFIG = 266,                  /* CONFIG  */
    DEFFS = 267,                   /* DEFFS  */
    DEFINE = 268,                  /* DEFINE  */
    DEFOPT = 269,                  /* DEFOPT  */
    DEFPARAM = 270,                /* DEFPARAM  */
    DEFFLAG = 271,                 /* DEFFLAG  */
    DEFPSEUDO = 272,               /* DEFPSEUDO  */
    DEFPSEUDODEV = 273,            /* DEFPSEUDODEV  */
    DEVICE = 274,                  /* DEVICE  */
    DEVCLASS = 275,                /* DEVCLASS  */
    DUMPS = 276,                   /* DUMPS  */
    DEVICE_MAJOR = 277,            /* DEVICE_MAJOR  */
    ENDFILE = 278,                 /* ENDFILE  */
    XFILE = 279,                   /* XFILE  */
    FILE_SYSTEM = 280,             /* FILE_SYSTEM  */
    FLAGS = 281,                   /* FLAGS  */
    IDENT = 282,                   /* IDENT  */
    IOCONF = 283,                  /* IOCONF  */
    LINKZERO = 284,                /* LINKZERO  */
    XMACHINE = 285,                /* XMACHINE  */
    MAJOR = 286,                   /* MAJOR  */
    MAKEOPTIONS = 287,             /* MAKEOPTIONS  */
    MAXUSERS = 288,                /* MAXUSERS  */
    MAXPARTITIONS = 289,           /* MAXPARTITIONS  */
    MINOR = 290,                   /* MINOR  */
    NEEDS_COUNT = 291,             /* NEEDS_COUNT  */
    NEEDS_FLAG = 292,              /* NEEDS_FLAG  */
    NO = 293,                      /* NO  */
    CNO = 294,                     /* CNO  */
    XOBJECT = 295,                 /* XOBJECT  */
    OBSOLETE = 296,                /* OBSOLETE  */
    ON = 297,                      /* ON  */
    OPTIONS = 298,                 /* OPTIONS  */
    PACKAGE = 299,                 /* PACKAGE  */
    PLUSEQ = 300,                  /* PLUSEQ  */
    PREFIX = 301,                  /* PREFIX  */
    BUILDPREFIX = 302,             /* BUILDPREFIX  */
    PSEUDO_DEVICE = 303,           /* PSEUDO_DEVICE  */
    PSEUDO_ROOT = 304,             /* PSEUDO_ROOT  */
    ROOT = 305,                    /* ROOT  */
    SELECT = 306,                  /* SELECT  */
    SINGLE = 307,                  /* SINGLE  */
    SOURCE = 308,                  /* SOURCE  */
    TYPE = 309,                    /* TYPE  */
    VECTOR = 310,                  /* VECTOR  */
    VERSION = 311,                 /* VERSION  */
    WITH = 312,                    /* WITH  */
    NUMBER = 313,                  /* NUMBER  */
    PATHNAME = 314,                /* PATHNAME  */
    QSTRING = 315,                 /* QSTRING  */
    WORD = 316,                    /* WORD  */
    EMPTYSTRING = 317,             /* EMPTYSTRING  */
    ENDDEFS = 318                  /* ENDDEFS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define AND 258
#define AT 259
#define ATTACH 260
#define BLOCK 261
#define BUILD 262
#define CHAR 263
#define COLONEQ 264
#define COMPILE_WITH 265
#define CONFIG 266
#define DEFFS 267
#define DEFINE 268
#define DEFOPT 269
#define DEFPARAM 270
#define DEFFLAG 271
#define DEFPSEUDO 272
#define DEFPSEUDODEV 273
#define DEVICE 274
#define DEVCLASS 275
#define DUMPS 276
#define DEVICE_MAJOR 277
#define ENDFILE 278
#define XFILE 279
#define FILE_SYSTEM 280
#define FLAGS 281
#define IDENT 282
#define IOCONF 283
#define LINKZERO 284
#define XMACHINE 285
#define MAJOR 286
#define MAKEOPTIONS 287
#define MAXUSERS 288
#define MAXPARTITIONS 289
#define MINOR 290
#define NEEDS_COUNT 291
#define NEEDS_FLAG 292
#define NO 293
#define CNO 294
#define XOBJECT 295
#define OBSOLETE 296
#define ON 297
#define OPTIONS 298
#define PACKAGE 299
#define PLUSEQ 300
#define PREFIX 301
#define BUILDPREFIX 302
#define PSEUDO_DEVICE 303
#define PSEUDO_ROOT 304
#define ROOT 305
#define SELECT 306
#define SINGLE 307
#define SOURCE 308
#define TYPE 309
#define VECTOR 310
#define VERSION 311
#define WITH 312
#define NUMBER 313
#define PATHNAME 314
#define QSTRING 315
#define WORD 316
#define EMPTYSTRING 317
#define ENDDEFS 318

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 155 "gram.y"

	struct	attr *attr;
	struct	devbase *devb;
	struct	deva *deva;
	struct	nvlist *list;
	struct defoptlist *defoptlist;
	struct loclist *loclist;
	struct attrlist *attrlist;
	struct condexpr *condexpr;
	const char *str;
	struct	numconst num;
	int64_t	val;
	u_char	flag;
	devmajor_t devmajor;
	int32_t i32;

#line 210 "y.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_Y_TAB_H_INCLUDED  */
