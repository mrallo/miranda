/* Miranda steer.c */

/* Initialisation routines and assorted routines for I/O etc.  */

/**************************************************************************
 * Copyright (C) Research Software Limited 1985-90.  All rights reserved. *
 * The Miranda system is distributed as free software under the terms in  *
 * the file "COPYING" which is included in the distribution.              *
 * Revised to C11 standard and made 64bit compatible, January 2020        *
 * Modernized for C11, improved readability and structure, June 2025      *
 *------------------------------------------------------------------------*/

/* this stuff is to get the time-last-modified of files */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>   /* For creat() */
#include <stdio.h>   /* For FILE, printf, fprintf, etc. */
#include <stdlib.h>  /* For exit, getenv, malloc, free */
#include <string.h>  /* For strcpy, strcat, strcmp, strlen, strstr, rindex, index */
#include <unistd.h>  /* For isatty, chdir, fork, read, write, close, unlink, getcwd */
#include <signal.h>  /* For signal */
#include <setjmp.h>  /* For setjmp, longjmp */
#include <ctype.h>   /* For isalpha */
#include <termios.h> /* For struct winsize, TIOCGWINSZ */
#include <sys/ioctl.h> /* For ioctl */

#if defined(sparc8) || defined(sparc)
#include <ieeefp.h>
#endif
#include <float.h> /* For DBL_MAX */

/* External declarations from other Miranda system files */
/* These are placeholders; actual content depends on your system. */
#include "data.h"
#include "big.h"
#include "lex.h"

/* It is assumed that 'word' is a type capable of holding pointers and
 * integral values, and is correctly sized for the target architecture (e.g., uintptr_t).
 * Without data.h, its exact definition is unknown. */
extern struct stat buf; /* see man(2) stat - gets file status */

word nill, Void;
word main_id; /* change to magic scripts 19.11.2013 */
word message, standardout;
word diagonalise, concat, indent_fn, outdent_fn, listdiff_fn;
word shownum1, showbool, showchar, showlist, showstring, showparen, showpair,
    showvoid, showfunction, showabstract, showwhat;

/* These buffers might need dynamic allocation or larger static sizes if
 * 'pnlim' (pathname limit) or 'BUFSIZE' (general buffer size) is very large.
 * Consider using dynamic allocation with error checking for robust path handling. */
char PRELUDE[pnlim + 10];
char STDENV[pnlim + 9];

#define DFLTSPACE 2500000L
#define DFLTDICSPACE 100000L
/* default values for size of heap, dictionary */
word SPACELIMIT = DFLTSPACE;
word DICSPACE = DFLTDICSPACE;

#ifdef CYGWIN
#define DEFAULT_EDITOR "joe +!"
#else
#define DEFAULT_EDITOR "vi +!"
#endif
/* The name of whatever is locally considered to be the default editor - the
   user will be able to override this using the `/editor' command.
   It is also overridden by shell/environment variable EDITOR if present */

extern FILE *s_out;
int UTF8 = 0;
int UTF8OUT = 0;
extern char *vdate, *host;
extern word version, ND;
extern word *dstack, *stackp;

/* Forward declarations for static functions */
static void allnamescom(void);
static void announce(void);
static int badeditor(void);
static int checkversion(char *);
static void command(void);
static void commandloop(char *);
static void diagnose(const char *);
static void editfile(const char *, int);
static void ed_warn(void);
static void filecopy(const char *);
static void filecp(const char *, const char *);
static void finger(const char *);
static void fixeditor(void);
static void fixexports(void);
static int getln(FILE *, word, char *);
static word isfreeid(word);
static void libfails(void);
static void loadfile(char *);
static void makedump(void);
static void manaction(void);
static void mira_setup(void);
static void missparam(const char *);
static char *mkabsolute(char *);
static word mkincludes(word);
static word mktiny(void);
static void namescom(word);
static void primlib(void);
static word privatise(word);
static void privlib(void);
static word publicise(word);
static word rc_read(char *);
static void rc_write(void);
static int src_update(void);
static void stdlib(void);
static const char *strvers(int);
static int twidth(void);
static void undump(char *);
static int utf8test(void);
static void unfixexports(void);
static void unlinkx(char *);
static void unload(void);
static void v_info(int);
static void xschars(void);

/* Global Variables (consider reducing their scope if possible) */
char *editor = NULL;
word okprel = 0; /* set to 1 when prelude loaded */
word nostdenv = 0; /* if set to 1 mira does not load stdenv at startup */

enum BadEditorStatus {
    EDITOR_OK = 0,
    EDITOR_BAD = 1
};
enum BadEditorStatus baded = EDITOR_OK; /* see fixeditor() */

char *miralib = NULL;
char *mirahdr, *lmirahdr;
const char *promptstr = "Miranda> ";
const char *obsuffix = "x";
FILE *s_in = NULL;
extern word commandmode; /* true only when reading command-level expressions */
int atobject = 0, atgc = 0, atcount = 0, debug = 0;
word magic = 0; /* set to 1 means script will start with UNIX magic string */
word making = 0; /* set only for mira -make */
word mkexports = 0; /* set only for mira -exports */
word mksources = 0; /* set only for mira -sources */
word make_status = 0; /* exit status of -make */
int compiling = 1;
/* there are two types of MIRANDA process - compiling (the main process) and
subsidiary processes launched for each evaluation - the above flag tells
us which kind of process we are in */
int ideep = 0; /* depth of %include we are at, see mkincludes() */
word SYNERR = 0;
word initialising = 1;
word primenv = NIL;
char *current_script;
word lastexp = UNDEF; /* value of `$$' */
word echoing = 0, listing = 0, verbosity;
word strictif = 1, rechecking = 0;
word errline = 0; /* records position of last error, for editor */
word errs = 0; /* secondary error location, in inserted script, if relevant */
word *cstack;
extern word c;
extern char *dicp, *dicq;
char linebuf[BUFSIZE]; /* used for assorted purposes */
/* NB cannot share with linebuf in lex.c, or !! goes wrong */
static char ebuf[pnlim];
extern word col;
char home_rc[pnlim + 8];
char lib_rc[pnlim + 8];
char *rc_error = NULL;
#define badval(x) (x < 1 || x > 478000000)

jmp_buf env;

#ifdef sparc8
fp_except commonmask = FP_X_INV | FP_X_OFL | FP_X_DZ; /* invalid|ovflo|divzero */
#endif

/* Use _Noreturn for functions that do not return */
_Noreturn void dieclean(void);
_Noreturn void fpe_error(void);
_Noreturn void missparam(const char *s);

void printver(void);

/* Main program entry point */
int main(int argc, char *argv[]) {
    word manonly = 0;
    char *home, *prs;
    int okhome_rc; /* flags valid HOME/.mirarc file present */
    char *argv0 = argv[0];
    char *initscript;
    int badlib = 0;
    extern int ARGC;
    extern char **ARGV;
    extern word newtyps, algshfns;
    char *progname = strrchr(argv[0], '/');
    if (!progname) {
        progname = argv[0];
    } else {
        progname++;
    }

    cstack = &manonly;
    /* used to indicate the base of the C stack for garbage collection purposes */
    verbosity = isatty(0);
    /* if(isatty(1)) */
    setbuf(stdout, NULL); /* for unbuffered tty output */

    if ((home = getenv("HOME"))) {
        strcpy(home_rc, home);
        if (strcmp(home_rc, "/") == 0) home_rc[0] = 0; /* root is special case */
        strcat(home_rc, "/.mirarc");
        okhome_rc = rc_read(home_rc);
    }
    /*setup policy:
      if valid HOME/.mirarc found look no further, otherwise try
        <miralib>/.mirarc
      Complaints - if any .mirarc contained bad data, `announce' complains about
      the last such looked at.  */
    UTF8OUT = UTF8 = utf8test();

    while (argc > 1 && argv[1][0] == '-') /* strip off flags */
    {
        if (strcmp(argv[1], "-stdenv") == 0) nostdenv = 1;
        else if (strcmp(argv[1], "-count") == 0) atcount = 1;
        else if (strcmp(argv[1], "-list") == 0) listing = 1;
        else if (strcmp(argv[1], "-nolist") == 0) listing = 0;
        else if (strcmp(argv[1], "-nostrictif") == 0) strictif = 0;
        else if (strcmp(argv[1], "-gc") == 0) atgc = 1;
        else if (strcmp(argv[1], "-object") == 0) atobject = 1;
        else if (strcmp(argv[1], "-lib") == 0) {
            argc--, argv++;
            if (argc == 1) missparam("lib");
            else miralib = argv[1];
        } else if (strcmp(argv[1], "-dic") == 0) {
            argc--, argv++;
            if (argc == 1) missparam("dic");
            else if (sscanf(argv[1], "%ld", &DICSPACE) != 1 || badval(DICSPACE))
                fprintf(stderr, "mira: bad value after flag \"-dic\"\n"), exit(1);
        } else if (strcmp(argv[1], "-heap") == 0) {
            argc--, argv++;
            if (argc == 1) missparam("heap");
            else if (sscanf(argv[1], "%ld", &SPACELIMIT) != 1 || badval(SPACELIMIT))
                fprintf(stderr, "mira: bad value after flag \"-heap\"\n"), exit(1);
        } else if (strcmp(argv[1], "-editor") == 0) {
            argc--, argv++;
            if (argc == 1) missparam("editor");
            else editor = argv[1], fixeditor();
        } else if (strcmp(argv[1], "-hush") == 0) verbosity = 0;
        else if (strcmp(argv[1], "-nohush") == 0) verbosity = 1;
        else if (strcmp(argv[1], "-exp") == 0 || strcmp(argv[1], "-log") == 0)
            fprintf(stderr, "mira: obsolete flag \"%s\"\n"
                            "use \"-exec\" or \"-exec2\", see manual\n",
                    argv[1]), exit(1);
        else if (strcmp(argv[1], "-exec") == 0) { /* replaces -exp 26.11.2019 */
            ARGC = argc - 2, ARGV = argv + 2, magic = 1, verbosity = 0;
        } else if (strcmp(argv[1], "-exec2") == 0) { /* version of -exec for debugging CGI scripts */
            if (argc <= 2) fprintf(stderr, "incorrect use of -exec2 flag, missing filename\n"), exit(1);
            char *logfilname, *p = strrchr(argv[2], '/');
            FILE *fil = NULL;
            if (!p) p = argv[2]; /* p now holds last component of prog name */
            // Allocate enough space for "miralog/" + filename + null terminator
            if ((logfilname = (char *)malloc(strlen(p) + 9))) {
                sprintf(logfilname, "miralog/%s", p);
                fil = fopen(logfilname, "a");
            } else {
                /* Error handling for malloc fail, assuming 'mallocfail' is external */
                /* mallocfail("logfile name"); */
                 fprintf(stderr, "mira: failed to allocate memory for logfile name\n");
                 exit(1);
            }
            /* process requires write permission on local directory "miralog" */
            if (fil) dup2(fileno(fil), 2); /* redirect stderr to log file */
            else fprintf(stderr, "could not open %s\n", logfilname);
            ARGC = argc - 2, ARGV = argv + 2, magic = 1, verbosity = 0;
            if (logfilname) free(logfilname); /* Free dynamically allocated memory */
        } else if (strcmp(argv[1], "-man") == 0) {
            manonly = 1;
            break;
        } else if (strcmp(argv[1], "-v") == 0) {
            printver();
            exit(0);
        } else if (strcmp(argv[1], "-version") == 0) {
            v_info(0);
            exit(0);
        } else if (strcmp(argv[1], "-V") == 0) {
            v_info(1);
            exit(0);
        } else if (strcmp(argv[1], "-make") == 0) {
            making = 1;
            verbosity = 0;
        } else if (strcmp(argv[1], "-exports") == 0) {
            making = mkexports = 1;
            verbosity = 0;
        } else if (strcmp(argv[1], "-sources") == 0) {
            making = mksources = 1;
            verbosity = 0;
        } else if (strcmp(argv[1], "-UTF-8") == 0) UTF8 = 1;
        else if (strcmp(argv[1], "-noUTF-8") == 0) UTF8 = 0;
        else
            fprintf(stderr, "mira: unknown flag \"%s\"\n", argv[1]), exit(1);
        argc--, argv++;
    }

    if (argc > 2 && !magic && !making) fprintf(stderr, "mira: too many args\n"), exit(1);
    if (!miralib) /* no -lib flag */
    {
        char *m;
        /* note search order */
        if ((m = getenv("MIRALIB"))) miralib = m;
        else if (checkversion(m = "/usr/lib/miralib")) miralib = m;
        else if (checkversion(m = "/usr/local/lib/miralib")) miralib = m;
        else if (checkversion(m = "miralib")) miralib = m;
        else badlib = 1;
    }

    if (badlib) {
        fprintf(stderr, "fatal error: miralib version %s not found\n",
                strvers(version));
        libfails();
        exit(1);
    }

    if (!okhome_rc) {
        if (rc_error == lib_rc) rc_error = NULL;
        (void)strcpy(lib_rc, miralib);
        (void)strcat(lib_rc, "/.mirarc");
        rc_read(lib_rc);
    }

    if (editor == NULL) { /* .mirarc was absent or unreadable */
        editor = getenv("EDITOR");
        if (editor == NULL) editor = (char *)DEFAULT_EDITOR; /* Cast for const string */
        else strcpy(ebuf, editor), editor = ebuf, fixeditor();
    }

    if ((prs = getenv("MIRAPROMPT"))) promptstr = prs;
    if (getenv("RECHECKMIRA") && !rechecking) rechecking = 1;
    if (getenv("NOSTRICTIF")) strictif = 0;

    setupdic(); /* used by mkabsolute */
    s_in = stdin;
    s_out = stdout;
    miralib = mkabsolute(miralib); /* protection against "/cd" */

    if (manonly) manaction(), exit(0);

    (void)strcpy(PRELUDE, miralib);
    (void)strcat(PRELUDE, "/prelude");
    /* convention - change spelling of "prelude" at each release */
    (void)strcpy(STDENV, miralib);
    (void)strcat(STDENV, "/stdenv.m");

    mira_setup();

    if (verbosity) announce();
    files = NIL;
    undump(PRELUDE);
    okprel = 1;
    mkprivate(fil_defs(hd[files]));
    files = NIL; /* don't wish unload() to unsetids on prelude */

    if (!nostdenv) {
        undump(STDENV);
        while (files != NIL) { /* stdenv may have %include structure */
            primenv = alfasort(append1(primenv, fil_defs(hd[files])));
            files = tl[files];
        }
        primenv = alfasort(primenv);
        newtyps = files = NIL; /* don't wish unload() to unsetids */
    }

    if (!magic) rc_write();
    echoing = verbosity & listing;
    initialising = 0;

    if (mkexports) {
        /* making=1, to say if recompiling, also to undump as for %include */
        word f, argcount = argc - 1;
        extern word exports, freeids;
        char *s;
        setjmp(env); /* will return here on blankerr (via reset) */
        while (--argc) { /* where do error messages go?? */
            word x = NIL;
            s = addextn(1, *++argv);
            if (s == dicp) keep(dicp);
            undump(s); /* bug, recompile messages goto stdout - FIX LATER */
            if (files == NIL || ND != NIL) continue;
            if (argcount != 1) printf("%s\n", s);
            if (exports != NIL) x = exports;
            /* true (if ever) only if just recompiled */
            else
                for (f = files; f != NIL; f = tl[f]) x = append1(fil_defs(hd[f]), x);
            /* method very clumsy, because exports not saved in dump */
            if (freeids != NIL) {
                word current_free_id = freeids;
                while (current_free_id != NIL) {
                    word n = findid((char *)hd[hd[tl[hd[current_free_id]]]]);
                    id_type(n) = tl[tl[hd[current_free_id]]];
                    id_val(n) = the_val(hd[hd[current_free_id]]);
                    hd[current_free_id] = n;
                    current_free_id = tl[current_free_id];
                }
                current_free_id = freeids = typesfirst(freeids);
                printf("\t%%free {\n");
                while (current_free_id != NIL) {
                    putchar('\t');
                    report_type(hd[current_free_id]);
                    putchar('\n');
                    current_free_id = tl[current_free_id];
                }
                printf("\t}\n");
            }
            for (x = typesfirst(alfasort(x)); x != NIL; x = tl[x]) {
                putchar('\t');
                report_type(hd[x]);
                putchar('\n');
            }
        }
        exit(0);
    }

    if (mksources) {
        extern word oldfiles;
        char *s;
        word f, x = NIL;
        setjmp(env); /* will return here on blankerr (via reset) */
        while (--argc)
            if (stat((s = addextn(1, *++argv)), &buf) == 0) {
                if (s == dicp) keep(dicp);
                undump(s);
                for (f = files == NIL ? oldfiles : files; f != NIL; f = tl[f])
                    if (!member(x, (word)get_fil(hd[f])))
                        x = cons((word)get_fil(hd[f]), x),
                        printf("%s\n", get_fil(hd[f]));
            }
        exit(0);
    }

    if (making) {
        extern word oldfiles;
        char *s;
        setjmp(env); /* will return here on blankerr (via reset) */
        while (--argc) { /* where do error messages go?? */
            s = addextn(1, *++argv);
            if (s == dicp) keep(dicp);
            undump(s);
            if (ND != NIL || (files == NIL && oldfiles != NIL)) {
                if (make_status == 1) make_status = 0;
                make_status = strcons(s, make_status);
            }
            /* keep list of source files with error-dumps */
        }
        if (tag[make_status] == STRCONS) {
            word h = 0, maxw = 0, w, n;
            printf("errors or undefined names found in:-\n");
            while (make_status) { /* reverse to get original order */
                h = strcons(hd[make_status], h);
                w = strlen((char *)hd[h]);
                if (w > maxw) maxw = w;
                make_status = tl[make_status];
            }
            maxw++;
            n = 78 / maxw;
            w = 0;
            while (h) {
                printf("%*s%s", (int)maxw, (char *)hd[h], (++w % n) ? "" : "\n");
                h = tl[h];
            }
            if (w % n) printf("\n");
            make_status = 1;
        }
        exit(make_status);
    }

    initscript = (argc == 1) ? "script.m" : (magic ? argv[1] : addextn(1, argv[1]));
    if (initscript == dicp) keep(dicp);

#if defined(sparc8)
    fpsetmask(commonmask);
#elif defined(sparc)
    ieee_handler("set", "common", (sighandler)fpe_error);
#endif

#if !defined(sparc) || defined(sparc8)
    (void)signal(SIGFPE, (sighandler)fpe_error); /* catch arithmetic overflow */
#endif
    (void)signal(SIGTERM, (sighandler)exit); /* flush buffers if killed */

    commandloop(initscript);
    /* parameter is file given as argument */
    return 0; /* Should not be reached, commandloop contains infinite loop or exit */
}

int vstack[4];  /* record of miralib versions looked at */
char *mstack[4]; /* and where found */
int mvp = 0;

int checkversion(char *m)
/* returns 1 iff m is directory with .version containing our version number */
{
    int v1, read_status = 0, r = 0;
    // Ensure linebuf has enough space before strcat. pnlim is max path length.
    if (strlen(m) + strlen("/.version") >= sizeof(linebuf)) {
        fprintf(stderr, "Error: Path buffer too small in checkversion.\n");
        return 0;
    }
    (void)strcpy(linebuf, m);
    (void)strcat(linebuf, "/.version");
    FILE *f = fopen(linebuf, "r");
    if (f) {
        if (fscanf(f, "%u", &v1) == 1) {
            r = (v1 == version);
            read_status = 1;
        }
        (void)fclose(f);
    }
    if (read_status && !r) {
        if (mvp < 4) { // Prevent buffer overflow for mstack/vstack
            mstack[mvp] = m;
            vstack[mvp++] = v1;
        }
    }
    return r;
}

void libfails(void) {
    word i = 0;
    fprintf(stderr, "found");
    for (; i < mvp; i++) {
        fprintf(stderr, "\tversion %s at: %s\n", strvers(vstack[i]), mstack[i]);
    }
}

const char *strvers(int v) {
    static char vbuf[12];
    if (v < 0 || v > 999999) return "???";
    snprintf(vbuf, sizeof(vbuf), "%.3f", v / 1000.0);
    return vbuf;
}

char *mkabsolute(char *m) { /* make sure m is an absolute pathname */
    if (m[0] == '/') return m;
    // Ensure dicp has enough space for CWD + '/' + m + '\0'
    // This assumes dicp is a large buffer managed externally.
    // A safer approach might be to malloc here and pass ownership.
    if (!getcwd(dicp, pnlim)) {
        fprintf(stderr, "panic: cwd too long\n");
        exit(1);
    }
    (void)strcat(dicp, "/");
    (void)strcat(dicp, m);
    m = dicp;
    dicp = dicq += strlen(dicp) + 1; /* This assumes dicq is a pointer within dicp buffer */
    dic_check(); /* Assumed to check dictionary bounds */
    return m;
}

void missparam(const char *s) {
    fprintf(stderr, "mira: missing param after flag \"-%s\"\n", s);
    exit(1);
}

int oldversion = 0;
#define COL_MAX 400

static inline void print_spaces(int s) {
    for (int j = 0; j < s; j++) {
        putchar(' ');
    }
}

void announce(void) {
    extern char *vdate;
    word w;
    /* clrscr(); /* clear screen on start up (commented out in original) */
    w = (twidth() - 50) / 2;
    printf("\n\n");
    print_spaces((int)w);
    printf("   T h e   M i r a n d a   S y s t e m\n\n");
    print_spaces((int)(w + 5 - (strlen(vdate) / 2)));
    printf("  version %s last revised %s\n\n", strvers(version), vdate);
    print_spaces((int)w);
    printf("Copyright Research Software Ltd 1985-2020\n\n");
    print_spaces((int)w);
    printf("  World Wide Web: http://miranda.org.uk\n\n\n");

    if (SPACELIMIT != DFLTSPACE) {
        printf("(%ld cells)\n", SPACELIMIT);
    }
    if (!strictif) printf("(-nostrictif : deprecated!)\n");
    /* printf("\t\t\t\t%dbit platform\n",__WORDSIZE); /* temporary */
    if (oldversion < 1999) { /* pre release two */
        printf("\
WARNING:\n\
a new release of Miranda has been installed since you last used\n\
the system - please read the `CHANGES' section of the /man pages !!!\n\n");
    } else if (version > oldversion) {
        printf("a new version of Miranda has been installed since you last\n");
        printf("used the system - see under `CHANGES' in the /man pages\n\n");
    }
    if (version < oldversion) {
        printf("warning - this is an older version of Miranda than the one\n");
        printf("you last used on this machine!!\n\n");
    }
    if (rc_error) {
        printf("warning: \"%s\" contained bad data (ignored)\n", rc_error);
    }
}

word rc_read(char *rcfile) { /* get settings of system parameters from setup file */
    FILE *in;
    char z[20];
    word h, d, v, s_val, r = 0; // Renamed 's' to 's_val' to avoid conflict with extern FILE* s_in
    oldversion = version;       /* default assumption */

    in = fopen(rcfile, "r");
    if (in == NULL || fscanf(in, "%19s", z) != 1) {
        return 0; /* file not present, or not readable */
    }

    if (strncmp(z, "hdve", 4) == 0 /* current .mirarc format */
        || strcmp(z, "lhdve") == 0) { /* alternative format used at release one */
        char *z1 = &z[3];
        if (z[0] == 'l') listing = 1, z1++;
        while (*++z1)
            if (*z1 == 'l') listing = 1;
            else if (*z1 == 's') /* ignore */;
            else if (*z1 == 'r') rechecking = 2;
            else rc_error = rcfile;

        if (fscanf(in, "%ld%ld%ld%*c", &h, &d, &v) != 3 || !getln(in, pnlim - 1, ebuf)
            || badval(h) || badval(d) || badval(v)) {
            rc_error = rcfile;
        } else {
            editor = ebuf;
            SPACELIMIT = h;
            DICSPACE = d;
            r = 1;
            oldversion = v;
        }
    } else if (strcmp(z, "ehdsv") == 0) { /* versions before 550 */
        if (fscanf(in, "%19s%ld%ld%ld%ld", ebuf, &h, &d, &s_val, &v) != 5
            || badval(h) || badval(d) || badval(v)) {
            rc_error = rcfile;
        } else {
            editor = ebuf;
            SPACELIMIT = h;
            DICSPACE = d;
            r = 1;
            oldversion = v;
        }
    } else if (strcmp(z, "ehds") == 0) { /* versions before 326, "s" was stacklimit (ignore) */
        if (fscanf(in, "%s%ld%ld%ld", ebuf, &h, &d, &s_val) != 4
            || badval(h) || badval(d)) {
            rc_error = rcfile;
        } else {
            editor = ebuf;
            SPACELIMIT = h;
            DICSPACE = d;
            r = 1;
            oldversion = 1;
        }
    } else {
        rc_error = rcfile; /* unrecognised format */
    }

    if (editor) fixeditor();
    (void)fclose(in);
    return r;
}

void fixeditor(void) {
    if (strcmp(editor, "vi") == 0) editor = "vi +!";
    else if (strcmp(editor, "pico") == 0) editor = "pico +!";
    else if (strcmp(editor, "nano") == 0) editor = "nano +!";
    else if (strcmp(editor, "joe") == 0) editor = "joe +!";
    else if (strcmp(editor, "jpico") == 0) editor = "jpico +!";
    else if (strcmp(editor, "vim") == 0) editor = "vim +!";
    else if (strcmp(editor, "gvim") == 0) editor = "gvim +! % &";
    else if (strcmp(editor, "emacs") == 0) editor = "emacs +! % &";
    else {
        char *p = strrchr(editor, '/');
        if (p == NULL) p = editor;
        else p++;
        if (strcmp(p, "vi") == 0) strcat(p, " +!");
    }
    if (strrchr(editor, '&')) rechecking = 2;
    listing = badeditor();
}

int badeditor(void) { /* does editor know how to open file at line? */
    char *p = strchr(editor, '!');
    while (p && p[-1] == '\\') p = strchr(p + 1, '!');
    return (baded = (enum BadEditorStatus)!p);
}

int getln(FILE *in, word n, char *s) { /* reads line (<=n chars) from in into s - returns 1 if ok */
    /* the newline is discarded, and the result '\0' terminated */
    for (word i = 0; i < n; i++) {
        int c = getc(in);
        if (c == EOF) {
            *s = '\0'; // Ensure null termination even on EOF
            return 0;
        }
        if (c == '\n') {
            *s = '\0';
            return 1;
        }
        *s++ = (char)c;
    }
    *s = '\0'; // Ensure null termination if n chars read without newline
    // Consume rest of line if buffer limit reached
    int c;
    while ((c = getc(in)) != '\n' && c != EOF);
    return 0; // Line too long or EOF before newline within n chars
}

void rc_write(void) {
    FILE *out = fopen(home_rc, "w");
    if (out == NULL) {
        fprintf(stderr, "warning: cannot write to \"%s\"\n", home_rc);
        return;
    }
    fprintf(out, "hdve");
    if (listing) fputc('l', out);
    if (rechecking == 2) fputc('r', out);
    fprintf(out, " %ld %ld %ld %s\n", SPACELIMIT, DICSPACE, version, editor);
    (void)fclose(out);
}

word lastid = 0; /* first inscope identifier of immediately preceding command */
word rv_expr = 0;

void commandloop(char *initscript) {
    int ch;
    extern word cook_stdin;
    extern void obey(word);
    char *lb;
    if (setjmp(env) == 0) { /* returns here if interrupted, 0 means first time thru */
        if (magic) {
            undump(initscript); /* was loadfile() changed 26.11.2019
                                   to allow dump of magic scripts in ".m"*/
            if (files == NIL || ND != NIL || id_val(main_id) == UNDEF)
            /* files==NIL=>script absent or has syntax errors
               ND!=NIL=>script has type errors or undefined names
               all reported by undump() or loadfile() on new compile */
            {
                if (files != NIL && ND == NIL && id_val(main_id) == UNDEF)
                    fprintf(stderr, "%s: main not defined\n", initscript);
                fprintf(stderr, "mira: incorrect use of \"-exec\" flag\n");
                exit(1);
            }
            magic = 0;
            obey(main_id);
            exit(0);
        }
        /* was obey(lastexp), change to magic scripts 19.11.2013 */
        (void)signal(SIGINT, (sighandler)reset);
        undump(initscript);
        if (verbosity) printf("for help type /h\n");
    }

    for (;;) {
        resetgcstats();
        if (verbosity) printf("%s", promptstr);
        ch = getchar();
        if (rechecking && src_update()) loadfile(current_script);
        /* modified behaviour for `2-window' mode */
        while (ch == ' ' || ch == '\t') ch = getchar();

        switch (ch) {
            case '?':
                ch = getchar();
                if (ch == '?') {
                    word x;
                    char *aka = NULL;
                    if (!token() && !lastid) {
                        printf("\7identifier needed after `\?\?'\n");
                        ch = getchar(); /* '\n' */
                        break;
                    }
                    if (getchar() != '\n') {
                        xschars();
                        break;
                    }
                    if (baded) {
                        ed_warn();
                        break;
                    }
                    if (dicp[0]) x = findid(dicp);
                    else printf("??%s\n", get_id(lastid)), x = lastid;
                    if (x == NIL || id_type(x) == undef_t) {
                        diagnose(dicp[0] ? dicp : get_id(lastid));
                        lastid = 0;
                        break;
                    }
                    if (id_who(x) == NIL) {
                        /* nb - primitives have NIL who field */
                        printf("%s -- primitive to Miranda\n",
                               dicp[0] ? dicp : get_id(lastid));
                        lastid = 0;
                        break;
                    }
                    lastid = x;
                    x = id_who(x); /* get here info */
                    if (tag[x] == CONS) aka = (char *)hd[hd[x]], x = tl[x];
                    if (aka) printf("originally defined as \"%s\"\n",
                                    aka);
                    editfile((char *)hd[x], tl[x]);
                    break;
                }
                ungetc(ch, stdin);
                (void)token();
                lastid = 0;
                if (dicp[0] == '\0') {
                    if (getchar() != '\n') xschars();
                    else allnamescom();
                    break;
                }
                while (dicp[0]) finger(dicp), (void)token();
                ch = getchar();
                break;
            case ':': /* add (silently) as kindness to Hugs users */
            case '/':
                (void)token();
                lastid = 0;
                command();
                break;
            case '!':
                if (!(lb = rdline())) break; /* rdline returns NULL on failure */
                lastid = 0;
                if (*lb) {
                    static char *shell = NULL;
                    sighandler oldsig;
                    pid_t pid; /* Use pid_t for process IDs */

                    if (!shell) {
                        shell = getenv("SHELL");
                        if (!shell) shell = (char *)"/bin/sh";
                    }
                    oldsig = signal(SIGINT, SIG_IGN);
                    if ((pid = fork()) == -1) { // Check for fork failure
                        perror("UNIX error - cannot create process");
                    } else if (pid == 0) { // Child process
                        execl(shell, shell, "-c", lb, (char *)0);
                        // If execl returns, it means an error occurred
                        perror("execl failed");
                        _exit(127); // Exit child process if execl fails
                    } else { // Parent process
                        int status;
                        while (pid != wait(&status)); // Wait for child to finish
                        (void)signal(SIGINT, oldsig);
                    }
                    if (src_update()) loadfile(current_script);
                } else
                    printf(
                        "No previous shell command to substitute for \"!\"\n");
                break;
            case '|': /* lines beginning "||" are comments */
                if ((ch = getchar()) != '|')
                    printf("\7unknown command - type /h for help\n");
                while (ch != '\n' && ch != EOF) ch = getchar();
            case '\n':
                break;
            case EOF:
                if (verbosity) printf("\nmiranda logout\n");
                exit(0);
            default:
                ungetc(ch, stdin);
                lastid = 0;
                tl[hd[cook_stdin]] = 0; /* unset type of $+ */
                rv_expr = 0;
                c = EVAL;
                echoing = 0;
                polyshowerror = 0; /* gets set by wrong use of $+, readvals */
                commandmode = 1;
                yyparse();
                if (SYNERR) SYNERR = 0;
                else if (c != '\n') { /* APPARENTLY NEVER TRUE */
                    printf("syntax error\n");
                    while (c != '\n' && c != EOF)
                        c = getchar(); /* swallow syntax errors */
                }
                commandmode = 0;
                echoing = verbosity & listing;
        }
    }
}

word parseline(word t, FILE *f, word fil) { /* parses next valid line of f at type t, returns EOF
		      if none found.  See READVALS in reduce.c */
    word t1, ch_val;
    lastexp = UNDEF;
    for (;;) {
        ch_val = getc(f);
        while (ch_val == ' ' || ch_val == '\t' || ch_val == '\n') ch_val = getc(f);
        if (ch_val == '|') {
            ch_val = getc(f);
            if (ch_val == '|') { /* leading comment */
                while ((ch_val = getc(f)) != '\n' && ch_val != EOF);
                if (ch_val != EOF) continue;
            } else
                ungetc(ch_val, f);
        }
        if (ch_val == EOF) return (word)EOF;
        ungetc(ch_val, f);
        c = VALUE;
        echoing = 0;
        commandmode = 1;
        s_in = f;
        yyparse();
        s_in = stdin;
        if (SYNERR) SYNERR = 0, lastexp = UNDEF;
        else if ((t1 = type_of(lastexp)) == wrong_t) lastexp = UNDEF;
        else if (!subsumes(instantiate(t1), t)) {
            printf("data has wrong type :: ");
            out_type(t1);
            printf("\nshould be :: ");
            out_type(t);
            putc('\n', stdout);
            lastexp = UNDEF;
        }
        if (lastexp != UNDEF) return codegen(lastexp);
        if (isatty(fileno(f))) printf("please re-enter data:\n");
        else {
            if (fil) fprintf(stderr, "readvals: bad data in file \"%s\"\n",
                             getstring(fil, 0));
            else fprintf(stderr, "bad data in $+ input\n");
            outstats();
            exit(1);
        }
    }
}

void ed_warn(void) {
    printf(
        "The currently installed editor command, \"%s\", does not\n\
include a facility for opening a file at a specified line number.  As a\n\
result the `\?\?' command and certain other features of the Miranda system\n\
are disabled.  See manual section 31/5 on changing the editor for more\n\
information.\n",
        editor);
}

word fm_time(const char *f) { /* time last modified of file f */
    return (word)(stat(f, &buf) == 0 ? buf.st_mtime : 0);
    /* non-existent file has conventional mtime of 0 */
} /* we assume time_t can be stored in a word */

#define same_file(x, y) (hd[fil_inodev(x)] == hd[fil_inodev(y)] && \
                         tl[fil_inodev(x)] == tl[fil_inodev(y)])
#define inodev(f) (stat(f, &buf) == 0 ? datapair(buf.st_ino, buf.st_dev) : \
                   datapair(0, (word)-1))

word oldfiles = NIL; /* most recent set of sources, in case of interrupted or
                                                       failed compilation */
int src_update(void) { /* any sources modified ? */
    word ft, f = files == NIL ? oldfiles : files;
    while (f != NIL) {
        if ((ft = fm_time(get_fil(hd[f]))) != fil_time(hd[f])) {
            if (ft == 0) unlinkx(get_fil(hd[f])); /* tidy up after eg `!rm %' */
            return 1;
        }
        f = tl[f];
    }
    return 0;
}

int loading;
char *unlinkme; /* if set, is name of partially created obfile */

void reset(void) { /* interrupt catcher - see call to signal in commandloop */
    extern word lineptr, ATNAMES, current_id;
    extern int blankerr, collecting;
    /*if(!making)  /* see note below
      (void)signal(SIGINT,SIG_IGN); /* dont interrupt me while I'm tidying up */
    /*if(magic)exit(0); *//* signal now not set to reset in magic scripts */
    if (collecting) gcpatch();
    if (loading) {
        if (!blankerr)
            printf("\n<<compilation interrupted>>\n");
        if (unlinkme) unlink(unlinkme);
        /* stackp=dstack; /* add if undump() made interruptible later*/
        oldfiles = files, unload(), current_id = ATNAMES = loading = SYNERR = lineptr = 0;
        if (blankerr) blankerr = 0, makedump();
    }
    /* magic script cannot be literate so no guard needed on makedump */
    else
        printf("<<interrupt>>\n"); /* VAX, SUN, ^C does not cause newline */
    reset_state(); /* see LEX */
    if (collecting) collecting = 0, gc(); /* to mark stdenv etc as wanted */
    if (making && !make_status) make_status = 1;
#ifdef SYSTEM5
    else (void)signal(SIGINT, (sighandler)reset); /*ready for next interrupt*//*see note*/
#endif
    /* during mira -make blankerr is only use of reset */
    longjmp(env, 1);
} /* under BSD and Linux installed signal remains installed after interrupt
    and further signals blocked until handler returns */

static inline void consume_eol(void) {
    int ch = getchar();
    if (ch != '\n') {
        xschars(); /* Assumed to handle extra chars and print message */
    }
}

int lose;

int normal(const char *f) { /* s has ".m" suffix */
    size_t n = strlen(f);
    return n >= 2 && strcmp(f + n - 2, ".m") == 0;
}

void printver(void) {
    printf("%s", strvers(version));
}

void v_info(int full) {
    printf("%s last revised %s\n", strvers(version), vdate);
    if (!full) return;
    printf("%s", host);
    printf("XVERSION %u\n", XVERSION);
}

void command(void) {
    char *t;
    int ch, ch1;
    switch (dicp[0]) {
        case 'a':
            if (is("a") || is("aux")) {
                consume_eol();
                /* if(verbosity)clrscr(); */
                (void)strcpy(linebuf, miralib);
                (void)strcat(linebuf, "/auxfile");
                filecopy(linebuf);
                return;
            }
            break;
        case 'c':
            if (is("count")) {
                consume_eol();
                atcount = 1;
                return;
            }
            if (is("cd")) {
                char *d = token();
                if (!d) d = getenv("HOME");
                else d = addextn(0, d);
                consume_eol();
                if (chdir(d) == -1) printf("cannot cd to %s\n", d);
                else if (src_update()) undump(current_script);
                /* alternative: keep old script and recompute pathname
                   wrt new directory - LOOK INTO THIS LATER */
                return;
            }
            break;
        case 'd':
            if (is("dic")) {
                extern char *dic;
                if (!token()) {
                    lose = getchar(); /* to eat \n */
                    printf("%ld chars", DICSPACE);
                    if (DICSPACE != DFLTDICSPACE)
                        printf(" (default=%ld)", DFLTDICSPACE);
                    printf(" %ld in use\n", (long)(dicq - dic));
                    return;
                }
                consume_eol();
                printf(
                    "sorry, cannot change size of dictionary while in use\n");
                printf(
                    "(/q and reinvoke with flag: mira -dic %s ... )\n", dicp);
                return;
            }
            break;
        case 'e':
            if (is("e") || is("edit")) {
                char *mf = NULL;
                if ((t = token())) t = addextn(1, t);
                else t = current_script;
                consume_eol();
                if (stat(t, &buf)) { /* new file */
                    if (!lmirahdr) { /* lazy initialisation */
                        dicp = dicq;
                        (void)strcpy(dicp, getenv("HOME"));
                        if (strcmp(dicp, "/") == 0)
                            dicp[0] = 0; /* root is special case */
                        (void)strcat(dicp, "/.mirahdr");
                        lmirahdr = dicp;
                        dicq = dicp = dicp + strlen(dicp) + 1; /* ovflo check? */
                    }
                    if (!stat(lmirahdr, &buf)) mf = lmirahdr;
                    if (!mf && !mirahdr) { /* lazy initialisation */
                        dicp = dicq;
                        (void)strcpy(dicp, miralib);
                        (void)strcat(dicp, "/.mirahdr");
                        mirahdr = dicp;
                        dicq = dicp = dicp + strlen(dicp) + 1;
                    }
                    if (!mf && !stat(mirahdr, &buf)) mf = mirahdr;
                    /*if(mf)printf("mf=%s\n",mf); /* DEBUG*/
                    if (mf && t != current_script) {
                        printf("open new script \"%s\"? [ny]", t);
                        ch1 = ch = getchar();
                        while (ch != '\n' && ch != EOF) ch = getchar();
                        /*eat rest of line */
                        if (ch1 != 'y' && ch1 != 'Y') return;
                    }
                    if (mf) filecp(mf, t);
                }
                editfile(t, strcmp(t, current_script) == 0 ? (int)errline :
                           (errs && strcmp(t, (char *)hd[errs]) == 0) ? (int)tl[errs] :
                           (int)geterrlin(t));
                return;
            }
            if (is("editor")) {
                char *hold = linebuf, *h;
                if (!getln(stdin, pnlim - 1, hold)) break; /*reject if too long*/
                if (!*hold) {
                    /* lose=getchar(); /* to eat newline */
                    printf("%s\n", editor);
                    return;
                }
                h = hold + strlen(hold); /* remove trailing white space */
                while (h > hold && (h[-1] == ' ' || h[-1] == '\t')) *--h = '\0';
                if (*hold == '"' || *hold == '\'') {
                    printf("please type name of editor without quotation marks\n");
                    return;
                }
                printf("change editor to: \"%s\"? [ny]", hold);
                ch1 = ch = getchar();
                while (ch != '\n' && ch != EOF) ch = getchar(); /* eat rest of line */
                if (ch1 != 'y' && ch1 != 'Y') {
                    printf("editor not changed\n");
                    return;
                }
                (void)strcpy(ebuf, hold);
                editor = ebuf;
                fixeditor(); /* reads "vi" as "vi +!" etc */
                echoing = verbosity & listing;
                rc_write();
                printf("editor = %s\n", editor);
                return;
            }
            break;
        case 'f':
            if (is("f") || is("file")) {
                char *t_arg = token();
                consume_eol();
                if (t_arg) t_arg = addextn(1, t_arg), keep(t_arg);
                /* could get multiple copies of filename in dictionary
                   - FIX LATER */
                if (t_arg) errs = errline = 0; /* moved here from reset() */
                if (t_arg)
                    if (strcmp(t_arg, current_script) || (files == NIL && okdump(t_arg))) {
                        extern word CLASHES;
                        CLASHES = NIL; /* normally done by load_script */
                        undump(t_arg); /* does not always call load_script */
                        if (CLASHES != NIL) /* pathological case, recompile */
                            loadfile(t_arg);
                    } else loadfile(t_arg); /* force recompilation */
                else printf("%s%s\n", current_script,
                            files == NIL ? " (not loaded)" : "");
                return;
            }
            if (is("files")) { /* info about internal state, not documented */
                word f = files;
                consume_eol();
                for (; f != NIL; f = tl[f])
                    printf("(%s,%ld,%ld)", get_fil(hd[f]), fil_time(hd[f]),
                           fil_share(hd[f])), printlist("", fil_defs(hd[f]));
                return;
            } /* DEBUG */
            if (is("find")) {
                int i = 0;
                while (token()) {
                    word x = findid(dicp), y, f;
                    i++;
                    if (x != NIL) {
                        char *n = get_id(x);
                        for (y = primenv; y != NIL; y = tl[y])
                            if (tag[hd[y]] == ID)
                                if (hd[y] == x || getaka(hd[y]) == n)
                                    finger(get_id(hd[y]));
                        for (f = files; f != NIL; f = tl[f])
                            for (y = fil_defs(hd[f]); y != NIL; y = tl[y])
                                if (tag[hd[y]] == ID)
                                    if (hd[y] == x || getaka(hd[y]) == n)
                                        finger(get_id(hd[y]));
                    }
                }
                ch = getchar(); /* '\n' */
                if (i == 0) printf("\7identifier needed after `/find'\n");
                return;
            }
            break;
        case 'g':
            if (is("gc")) {
                consume_eol();
                atgc = 1;
                return;
            }
            break;
        case 'h':
            if (is("h") || is("help")) {
                consume_eol();
                /* if(verbosity)clrscr(); */
                (void)strcpy(linebuf, miralib);
                (void)strcat(linebuf, "/helpfile");
                filecopy(linebuf);
                return;
            }
            if (is("heap")) {
                word x_val;
                if (!token()) {
                    lose = getchar(); /* to eat \n */
                    printf("%ld cells", SPACELIMIT);
                    if (SPACELIMIT != DFLTSPACE)
                        printf(" (default=%ld)", DFLTSPACE);
                    printf("\n");
                    return;
                }
                consume_eol();
                if (sscanf(dicp, "%ld", &x_val) != 1 || badval(x_val)) {
                    printf("illegal value (heap unchanged)\n");
                    return;
                }
                if (x_val < trueheapsize())
                    printf("sorry, cannot shrink heap to %ld at this time\n", x_val);
                else {
                    if (x_val != SPACELIMIT) {
                        SPACELIMIT = x_val;
                        resetheap();
                    }
                    printf("heaplimit = %ld cells\n", SPACELIMIT);
                    rc_write();
                }
                return;
            }
            if (is("hush")) {
                consume_eol();
                echoing = verbosity = 0;
                return;
            }
            break;
        case 'l':
            if (is("list")) {
                consume_eol();
                listing = 1;
                echoing = verbosity & listing;
                rc_write();
                return;
            }
            break;
        case 'm':
            if (is("m") || is("man")) {
                consume_eol();
                manaction();
                return;
            }
            if (is("miralib")) {
                consume_eol();
                printf("%s\n", miralib);
                return;
            }
            break;
        case 'n':
            /* if(is("namebuckets"))
               { int i,x;
                 extern word namebucket[];
                 consume_eol();
                 for(i=0;i<128;i++)
                 if(x=namebucket[i])
                   { printf("%d:",i);
                     while(x)
                          putchar(' '),out(stdout,hd[x]),x=tl[x];
                     putchar('\n'); }
                 return; }              /* DEBUG */
            if (is("nocount")) {
                consume_eol();
                atcount = 0;
                return;
            }
            if (is("nogc")) {
                consume_eol();
                atgc = 0;
                return;
            }
            if (is("nohush")) {
                consume_eol();
                echoing = listing;
                verbosity = 1;
                return;
            }
            if (is("nolist")) {
                consume_eol();
                echoing = listing = 0;
                rc_write();
                return;
            }
            if (is("norecheck")) {
                consume_eol();
                rechecking = 0;
                rc_write();
                return;
            }
            break;
        /* case 'o': if(is("object"))
                       { consume_eol(); atobject=1; return; } /* now done by flag -object */
        case 'q':
            if (is("q") || is("quit")) {
                consume_eol();
                if (verbosity) printf("miranda logout\n");
                exit(0);
            }
            break;
        case 'r':
            if (is("recheck")) {
                consume_eol();
                rechecking = 2;
                rc_write();
                return;
            }
            break;
        case 's':
            if (is("s") || is("settings")) {
                consume_eol();
                printf("*\theap %ld\n", SPACELIMIT);
                printf("*\tdic %ld\n", DICSPACE);
                printf("*\teditor = %s\n", editor);
                printf("*\t%slist\n", listing ? "" : "no");
                printf("*\t%srecheck\n", rechecking ? "" : "no");
                if (!strictif)
                    printf("\t-nostrictif (deprecated!)\n");
                if (atcount) printf("\tcount\n");
                if (atgc) printf("\tgc\n");
                if (UTF8) printf("\tUTF-8 i/o\n");
                if (!verbosity) printf("\thush\n");
                if (debug) printf("\tdebug 0%o\n", debug);
                printf("\n* items remembered between sessions\n");
                return;
            }
            break;
        case 'v':
            if (is("v") || is("version")) {
                consume_eol();
                v_info(0);
                return;
            }
            break;
        case 'V':
            if (is("V")) {
                consume_eol();
                v_info(1);
                return;
            }
            break;
        default:
            printf("\7unknown command \"%c%s\"\n", (char)c, dicp);
            printf("type /h for help\n");
            while ((ch = getchar()) != '\n' && ch != EOF);
            return;
    } /* end of switch statement */
    xschars();
}

void manaction(void) {
    // Ensure linebuf has enough space. miralib can be long.
    // +2 for quotes, +1 for space, +1 for null, total +4 (approx).
    // Using snprintf for safety.
    if (snprintf(linebuf, sizeof(linebuf), "\"%s/menudriver\" \"%s/manual\"", miralib, miralib) >= (int)sizeof(linebuf)) {
        fprintf(stderr, "Error: Buffer too small for manaction command.\n");
        return;
    }
    system(linebuf);
} /* put quotes around both pathnames to allow for spaces in miralib 8.5.06 */

void editfile(const char *t, int line) {
    char *ebuf = linebuf;
    char *p = ebuf, *q = editor;
    int tdone = 0;
    if (line == 0) line = 1; /* avoids warnings in some versions of vi */

    while (*q != '\0') {
        if (*q == '\\' && (q[1] == '!' || q[1] == '%')) {
            *p++ = *++q; /* copy escaped character */
            q++;
        } else if (*q == '!') {
            p += snprintf(p, sizeof(linebuf) - (p - ebuf), "%d", line);
            q++;
        } else if (*q == '%') {
            *p++ = '"'; /* quote filename 9.5.06 */
            size_t remaining_space = sizeof(linebuf) - (p - ebuf);
            size_t copied_len = strncat(p, t, remaining_space - 1) ? strlen(t) : 0; // Use strncat to prevent overflow
            p += copied_len;
            *p++ = '"';
            *p = '\0';
            tdone = 1;
            q++;
        } else {
            *p++ = *q++;
        }
        if ((p - ebuf) >= sizeof(linebuf) - 1) { // Check for buffer overflow
            fprintf(stderr, "Error: Editor command buffer overflow.\n");
            *p = '\0'; // Ensure termination
            return;
        }
    }
    *p = '\0'; // Null-terminate the string

    if (!tdone) {
        // Append filename if '%' was not used in editor string
        p = ebuf + strlen(ebuf); // Move p to the end of the current string
        *p++ = ' ';
        *p++ = '"';
        *p = '\0';
        size_t remaining_space = sizeof(linebuf) - (p - ebuf);
        strncat(p, t, remaining_space - 1);
        p += strlen(p);
        *p++ = '"';
        *p = '\0';
    }

    /* printf("%s\n",ebuf); /* DEBUG */
    system(ebuf);
    if (src_update()) loadfile(current_script);
    return;
}

void xschars(void) {
    int ch;
    printf("\7extra characters at end of command\n");
    while ((ch = getchar()) != '\n' && ch != EOF);
}

word reverse(word x) { /* x is a cons list */
    word y = NIL;
    while (x != NIL) y = cons(hd[x], y), x = tl[x];
    return y;
}

word shunt(word x, word y) { /* equivalent to append(reverse(x),y) */
    while (x != NIL) y = cons(hd[x], y), x = tl[x];
    return y;
}

const char *presym[] =
    {"abstype", "div", "if", "mod", "otherwise", "readvals", "show", "type", "where",
     "with", NULL};
const int presym_n[] =
    {21, 8, 15, 8, 15, 31, 23, 22, 15, 21};

void filequote(const char *p) { /* write p to stdout with <quotes> if appropriate */
    static int mlen = 0;
    if (!mlen) {
        char *slash = strrchr(PRELUDE, '/');
        if (slash) {
            mlen = (int)(slash - PRELUDE) + 1;
        } else {
            mlen = 0; // Should not happen for a valid path, but defensive
        }
    }

    if (strncmp(p, PRELUDE, mlen) == 0) {
        printf("<%s>", p + mlen);
    } else {
        printf("\"%s\"", p);
    }
} /* PRELUDE is a convenient string with the miralib prefix */

void finger(const char *n) { /* find info about name stored at dicp */
    word x;
    int line;
    char *s;
    x = findid(n);
    if (x != NIL && id_type(x) != undef_t) {
        if (id_who(x) != NIL) {
            word here_info = get_here(x);
            s = (char *)hd[here_info];
            line = (int)tl[here_info];
        } else {
            s = NULL; // Primitive, no source file
            line = 0;
        }

        if (!lastid) lastid = x;
        report_type(x);

        if (id_who(x) == NIL) {
            printf(" ||primitive to Miranda\n");
        } else {
            char *aka = getaka(x);
            if (aka == get_id(x)) aka = NULL; /* don't report alias to self */

            if (id_val(x) == UNDEF && id_type(x) != wrong_t)
                printf(" ||(UNDEFINED) specified in ");
            else if (id_val(x) == FREE)
                printf(" ||(FREE) specified in ");
            else if (id_type(x) == type_t && t_class(x) == free_t)
                printf(" ||(free type) specified in ");
            else
                printf(" ||%sdefined in ",
                       id_type(x) == type_t && t_class(x) == abstract_t ? "(abstract type) " :
                       id_type(x) == type_t && t_class(x) == algebraic_t ? "(algebraic type) " :
                       id_type(x) == type_t && t_class(x) == placeholder_t ? "(placeholder type) " :
                       id_type(x) == type_t && t_class(x) == synonym_t ? "(synonym type) " :
                       "");
            if (s) filequote(s);
            if (baded || rechecking) printf(" line %d", line);
            if (aka) printf(" (as \"%s\")\n", aka);
            else putchar('\n');
        }
        if (atobject) printf("%s = ", get_id(x)),
                      out(stdout, id_val(x)), putchar('\n');
        return;
    }
    diagnose(n);
}

void diagnose(const char *n) {
    int i = 0;
    if (isalpha((unsigned char)n[0]))
        while (n[i] && okid(n[i])) i++;
    if (n[i]) {
        printf("\"%s\" -- not an identifier\n", n);
        return;
    }
    for (i = 0; presym[i]; i++)
        if (strcmp(n, presym[i]) == 0) {
            printf("%s -- keyword (see manual, section %d)\n", n, presym_n[i]);
            return;
        }
    printf("identifier \"%s\" not in scope\n", n);
}

int sorted = 0; /* flag to avoid repeatedly sorting fil_defs */
int leftist; /* flag to alternate bias of padding in justification */
word words[COL_MAX]; /* max plausible size of screen */

void allnamescom(void) {
    word s;
    word x = ND;
    word y = x, z = 0;
    leftist = 0;
    namescom(make_fil(nostdenv ? 0 : (word)STDENV, 0, 0, primenv));
    if (files == NIL) return;
    else s = tl[files];
    while (s != NIL) namescom(hd[s]), s = tl[s];
    namescom(hd[files]);
    sorted = 1;
    /* now print warnings, if any */
    /*if(ND!=NIL&&id_type(hd[ND])==type_t)
      { printf("ILLEGAL EXPORT LIST - MISSING TYPENAME%s: ",tl[ND]==NIL?"":"S");
        printlist("",ND);
        return; } /* install if incomplete export list is escalated to error */
    while (x != NIL && id_type(hd[x]) == undef_t) x = tl[x];
    while (y != NIL && id_type(hd[y]) != undef_t) y = tl[y];
    if (x != NIL) {
        printf("WARNING, SCRIPT CONTAINS TYPE ERRORS: ");
        for (; x != NIL; x = tl[x])
            if (id_type(hd[x]) != undef_t) {
                if (!z) z = 1;
                else putchar(',');
                out(stdout, hd[x]);
            }
        printf(";\n");
    }
    if (y != NIL) {
        printf("%s UNDEFINED NAMES: ", z ? "AND" : "WARNING, SCRIPT CONTAINS");
        z = 0;
        for (; y != NIL; y = tl[y])
            if (id_type(hd[y]) == undef_t) {
                if (!z) z = 1;
                else putchar(',');
                out(stdout, hd[y]);
            }
        printf(";\n");
    }
}
/* There are two kinds of entry in ND
   undefined names: val=UNDEF, type=undef_t
   type errors: val=UNDEF, type=wrong_t
*/

#define TOLERANCE 3
/* max number of extra spaces we are willing to insert */

void namescom(word l) { /* l is an element of `files' */
    word n = fil_defs(l), col_width = 0, undefs = NIL, wp = 0;
    word scrwd = twidth();
    if (!sorted && n != primenv) /* primenv already sorted */
        fil_defs(l) = n = alfasort(n); /* also removes pnames */
    if (n == NIL) return; /* skip empty files */
    if (get_fil(l)) filequote(get_fil(l));
    else printf("primitive:");
    printf("\n");
    while (n != NIL) {
        if (id_type(hd[n]) == wrong_t || id_val(hd[n]) != UNDEF) {
            word w = (word)strlen(get_id(hd[n]));
            if (col_width + w < scrwd) col_width += (col_width != 0);
            else if (wp && col_width + w >= scrwd) {
                word i, r;
                if (wp > 1) i = (scrwd - col_width) / (wp - 1), r = (scrwd - col_width) % (wp - 1);
                else i = r = 0;
                if (i + (r > 0) > TOLERANCE) i = r = 0;
                if (leftist) {
                    for (col_width = 0; col_width < wp;) {
                        printf("%s", get_id(words[col_width]));
                        if (++col_width < wp)
                            print_spaces((int)(1 + i + (r-- > 0)));
                    }
                } else {
                    for (r = wp - 1 - r, col_width = 0; col_width < wp;) {
                        printf("%s", get_id(words[col_width]));
                        if (++col_width < wp)
                            print_spaces((int)(1 + i + (r-- <= 0)));
                    }
                }
                leftist = !leftist, wp = 0, col_width = 0, putchar('\n');
            }
            col_width += w;
            words[wp++] = hd[n];
        } else
            undefs = cons(hd[n], undefs); /* undefined but have good types */
        n = tl[n];
    }
    if (wp)
        for (col_width = 0; col_width < wp;)
            printf("%s", get_id(words[col_width])), putc(++col_width == wp ? '\n' : ' ', stdout);
    if (undefs == NIL) return;
    undefs = reverse(undefs);
    printlist("SPECIFIED BUT NOT DEFINED: ", undefs);
}

word detrop = NIL; /* list of unused local definitions */
word rfl = NIL; /* list of include components containing type orphans */
word bereaved; /* typenames referred to in exports and not exported */
word ld_stuff = NIL;
/* list of list of files, to be unloaded if mkincludes interrupted */

void loadfile(char *t) {
    extern word fileq;
    extern word current_id, includees, embargoes, exportfiles, freeids, exports;
    extern word fnts, FBS, nextpn;
    word h = NIL; /* location of %export directive, if present */

    loading = 1;
    errs = errline = 0;
    current_script = t;
    oldfiles = NIL;
    unload();

    if (stat(t, &buf)) {
        if (initialising) {
            fprintf(stderr, "panic: %s not found\n", t);
            exit(1);
        }
        if (verbosity) printf("new file %s\n", t);
        if (magic) fprintf(stderr, "mira -exec %s%s\n", t, ": no such file"), exit(1);
        if (making && ideep == 0) printf("mira -make %s%s\n", t, ": no such file");
        else oldfiles = cons(make_fil(t, 0, 0, NIL), NIL);
        /* for correct record of sources */
        loading = 0;
        return;
    }
    if (!openfile(t)) {
        if (initialising) {
            fprintf(stderr, "panic: cannot open %s\n", t);
            exit(1);
        }
        printf("cannot open %s\n", t);
        oldfiles = cons(make_fil(t, 0, 0, NIL), NIL);
        loading = 0;
        return;
    }
    files = cons(make_fil(t, fm_time(t), 1, NIL), NIL);
    current_file = hd[files];
    tl[hd[fileq]] = current_file;

    if (initialising && strcmp(t, PRELUDE) == 0) privlib();
    else if (initialising || nostdenv == 1)
        if (strcmp(t, STDENV) == 0) stdlib();

    c = ' ';
    col = 0;
    s_in = (FILE *)hd[hd[fileq]];
    adjust_prefix(t);

    commandmode = 0;
    if (verbosity || making) printf("compiling %s\n", t);
    nextpn = 0; /* lose pnames */
    embargoes = detrop =
    fnts = rfl = bereaved = ld_stuff = exportfiles = freeids = exports = includees = FBS = NIL;
    yyparse();

    if (!SYNERR && exportfiles != NIL) {
        /* check pathnames in exportfiles have unique bindings */
        word s_list, i_val, count;
        for (s_list = exportfiles; s_list != NIL; s_list = tl[s_list]) {
            if (hd[s_list] == PLUS) { /* add current script (less freeids) to exports */
                for (i_val = fil_defs(hd[files]); i_val != NIL; i_val = tl[i_val])
                    if (isvariable(hd[i_val]) && !isfreeid(hd[i_val]))
                        tl[exports] = add1(hd[i_val], tl[exports]);
            } else {
                /* pathnames are expanded to their contents in mkincludes */
                for (count = 0, i_val = includees; i_val != NIL; i_val = tl[i_val])
                    if (!strcmp((char *)hd[hd[hd[i_val]]], (char *)hd[s_list]))
                        hd[s_list] = hd[hd[hd[i_val]]] /*sharing*/ , count++;
                if (count != 1) {
                    SYNERR = 1;
                    printf("illegal fileid \"%s\" in export list (%s)\n",
                           (char *)hd[s_list],
                           count ? "ambiguous" : "not %included in script");
                }
            }
        }
        if (SYNERR) {
            sayhere(hd[exports], 1);
            printf("compilation abandoned\n");
        }
    }

    if (!SYNERR && includees != NIL) {
        files = append1(files, mkincludes(includees));
        includees = NIL;
    }
    ld_stuff = NIL;

    if (!SYNERR) {
        if (verbosity || (making && !mkexports && !mksources))
            printf("checking types in %s\n", t);
        checktypes();
        /* printf("typecheck complete\n"); /* DEBUG */
    }

    if (!SYNERR && exports != NIL)
        if (ND != NIL) exports = NIL; /* skip check, cannot be %included */
        else {
            /* check exports all present and close under type info */
            word e_item, u_list = NIL, n_list = NIL, c_list = NIL;
            h = hd[exports];
            exports = tl[exports];
            for (e_item = embargoes; e_item != NIL; e_item = tl[e_item]) {
                if (id_type(hd[e_item]) == undef_t) u_list = cons(hd[e_item], u_list), ND = add1(hd[e_item], ND);
                else if (!member(exports, hd[e_item])) n_list = cons(hd[e_item], n_list);
            }
            if (embargoes != NIL)
                exports = setdiff(exports, embargoes);
            exports = alfasort(exports);
            for (e_item = exports; e_item != NIL; e_item = tl[e_item])
                if (id_type(hd[e_item]) == undef_t) u_list = cons(hd[e_item], u_list), ND = add1(hd[e_item], ND);
                else if (id_type(hd[e_item]) == type_t && t_class(hd[e_item]) == algebraic_t)
                    c_list = shunt(t_info(hd[e_item]), c_list); /* constructors */
            if (exports == NIL) printf("warning, export list has void contents\n");
            else exports = append1(alfasort(c_list), exports);

            if (n_list != NIL) {
                printf("redundant entr%s in export list:", tl[n_list] == NIL ? "y" : "ies");
                while (n_list != NIL) printf(" -%s", get_id(hd[n_list])), n_list = tl[n_list];
                /* n_list = 1; */ /* flag (original code set n=1) */
                putchar('\n');
            }
            if (u_list != NIL) exports = NIL,
                               printlist("undefined names in export list: ", u_list);
            if (u_list != NIL) sayhere(h, 1), h = NIL;
            else if (exports == NIL || n_list != NIL) out_here(stderr, h, 1), h = NIL;
            /* for warnings call out_here not sayhere, so errinfo not saved in dump */
        }

    if (!SYNERR && ND == NIL && (exports != NIL || tl[files] != NIL)) {
        /* find out if script can create type orphans when %included */
        word e1, t_val;
        word r_list = NIL; /* collect list of referenced typenames */
        word e_list = NIL; /* and list of exported typenames */
        if (exports != NIL)
            for (e1 = exports; e1 != NIL; e1 = tl[e1]) {
                if ((t_val = id_type(hd[e1])) == type_t)
                    if (t_class(hd[e1]) == synonym_t)
                        r_list = UNION(r_list, deps(t_info(hd[e1])));
                    else e_list = cons(hd[e1], e_list);
                else r_list = UNION(r_list, deps(t_val));
            }
        else
            for (e1 = fil_defs(hd[files]); e1 != NIL; e1 = tl[e1]) {
                if ((t_val = id_type(hd[e1])) == type_t)
                    if (t_class(hd[e1]) == synonym_t)
                        r_list = UNION(r_list, deps(t_info(hd[e1])));
                    else e_list = cons(hd[e1], e_list);
                else r_list = UNION(r_list, deps(t_val));
            }
        for (e1 = freeids; e1 != NIL; e1 = tl[e1])
            if ((t_val = id_type(hd[hd[e1]])) == type_t)
                if (t_class(hd[hd[e1]]) == synonym_t)
                    r_list = UNION(r_list, deps(t_info(hd[hd[e1]])));
                else e_list = cons(hd[hd[e1]], e_list);
            else r_list = UNION(r_list, deps(t_val));
        /*printlist("r: ",r_list); /* DEBUG */
        for (; r_list != NIL; r_list = tl[r_list])
            if (!member(e_list, hd[r_list])) bereaved = cons(hd[r_list], bereaved);
        /*printlist("bereaved: ",bereaved); /* DEBUG */
    }

    if (exports != NIL && bereaved != NIL) {
        extern word newtyps;
        word b = intersection(bereaved, newtyps);
        /*printlist("newtyps",newtyps); /* DEBUG */
        if (b != NIL) {
            /*ND=b; /* to escalate to type error, see also allnamescom */
            printf("warning, export list is incomplete - missing typename%s: ",
                   tl[b] == NIL ? "" : "s");
            printlist("", b);
        }
        if (b != NIL && h != NIL) out_here(stdout, h, 1); /* sayhere(h,1) for error */
    }

    if (!SYNERR && detrop != NIL) {
        word gd = detrop;
        while (detrop != NIL && tag[dval(hd[detrop])] == LABEL) detrop = tl[detrop];
        if (detrop != NIL)
            printf("warning, script contains unused local definitions:-\n");
        while (detrop != NIL) {
            out_here(stdout, hd[hd[tl[dval(hd[detrop])]]], 0), putchar('\t');
            out_pattern(stdout, dlhs(hd[detrop])), putchar('\n');
            detrop = tl[detrop];
            while (detrop != NIL && tag[dval(hd[detrop])] == LABEL)
                detrop = tl[detrop];
        }
        while (gd != NIL && tag[dval(hd[gd])] != LABEL) gd = tl[gd];
        if (gd != NIL)
            printf("warning, grammar contains unused nonterminals:-\n");
        while (gd != NIL) {
            out_here(stdout, hd[dval(hd[gd])], 0), putchar('\t');
            out_pattern(stdout, dlhs(hd[gd])), putchar('\n');
            gd = tl[gd];
            while (gd != NIL && tag[dval(hd[gd])] != LABEL) gd = tl[gd];
        }
        /* note, usual rhs is tries(pat,list(label(here,exp)))
                   grammar rhs is label(here,...) */
    }

    if (!SYNERR) {
        word x_item;
        extern int lfrule;
        /* we invoke the code generator */
        lfrule = 0;
        for (x_item = fil_defs(hd[files]); x_item != NIL; x_item = tl[x_item])
            if (id_type(hd[x_item]) != type_t) {
                current_id = hd[x_item];
                polyshowerror = 0;
                id_val(hd[x_item]) = codegen(id_val(hd[x_item]));
                if (polyshowerror) id_val(hd[x_item]) = UNDEF;
                /* nb - one remaining class of typerrs trapped in codegen,
                   namely polymorphic show or readvals */
            }
        current_id = 0;
        if (lfrule && (verbosity || making))
            printf("grammar optimisation: %d common left factors found\n", lfrule);
        if (initialising && ND != NIL) {
            fprintf(stderr, "panic: %s contains errors\n", okprel ? "stdenv" : "prelude");
            exit(1);
        }
        if (initialising) makedump();
        else if (normal(t)) /* file ends ".m", formerly if(!magic) */
            fixexports(), makedump(), unfixexports();
        /* changed 26.11.2019 to allow dump of magic scripts ending ".m" */
        if (!errline && errs && (char *)hd[errs] == current_script)
            errline = tl[errs]; /* soft error (posn not saved in dump) */
        ND = alfasort(ND);
        /* we could sort and remove pnames from each defs component immediately
           after makedump(), instead of doing this in namescom */
        loading = 0;
        return;
    }
    /* otherwise syntax error found */
    if (initialising) {
        fprintf(stderr, "panic: cannot compile %s\n", okprel ? "stdenv" : "prelude");
        exit(1);
    }
    oldfiles = files;
    unload();
    if (normal(t) && SYNERR != 2) makedump(); /* make syntax error dump */
    /* allow dump of magic script in ".m", was if(!magic&&) 26.11.2019 */
    SYNERR = 0;
    loading = 0;
}

word isfreeid(word x) {
    return (id_type(x) == type_t ? t_class(x) == free_t : id_val(x) == FREE);
}

word internals = NIL; /* used by fix/unfixexports, list of names not exported */
#define paint(x) id_val(x) = ap(EXPORT, id_val(x))
#define unpainted(x) (tag[id_val(x)] != AP || hd[id_val(x)] != EXPORT)
#define unpaint(x) id_val(x) = tl[id_val(x)]

void fixexports(void) {
    extern word exports, exportfiles, embargoes, freeids;
    word e = exports, f_item;
    /* printlist("exports: ",e); /* DEBUG */
    for (; e != NIL; e = tl[e]) paint(hd[e]);
    internals = NIL;
    if (exports == NIL && exportfiles == NIL && embargoes == NIL) { /*no %export in script*/
        for (e = freeids; e != NIL; e = tl[e])
            internals = cons(privatise(hd[hd[e]]), internals);
        for (f_item = tl[files]; f_item != NIL; f_item = tl[f_item])
            for (e = fil_defs(hd[f_item]); e != NIL; e = tl[e]) {
                if (tag[hd[e]] == ID)
                    internals = cons(privatise(hd[e]), internals);
            }
    } else
        for (f_item = files; f_item != NIL; f_item = tl[f_item])
            for (e = fil_defs(hd[f_item]); e != NIL; e = tl[e]) {
                if (tag[hd[e]] == ID && unpainted(hd[e]))
                    internals = cons(privatise(hd[e]), internals);
            }
    /* optimisation, need not do this to `silent' components - fix later */
    /*printlist("internals: ",internals); /* DEBUG */
    for (e = exports; e != NIL; e = tl[e]) unpaint(hd[e]);
} /* may not be interrupt safe, re unload() */

void unfixexports(void) {
    /*printlist("internals: ",internals); /* DEBUG */
    word i = internals;
    if (mkexports) return; /* in this case don't want internals restored */
    while (i != NIL) /* lose */
    {
        publicise(hd[i]), i = tl[i];
    }
    internals = NIL;
} /* may not be interrupt safe, re unload() */

word privatise(word x) { /* change id to pname, and return new id holding it as value */
    extern word namebucket[], *pnvec;
    word n = make_pn(x), h = namebucket[hash(get_id(x))], i;
    if (id_type(x) == type_t)
        t_info(x) = cons(datapair(getaka(x), 0), get_here(x));
    /* to assist identification of danging type refs - see typesharing code
       in mkincludes */
    /* assumption - nothing looks at the t_info after compilation */
    if (id_val(x) == UNDEF) { /* name specified but not defined */
        id_val(x) = ap(datapair(getaka(x), 0), get_here(x));
        /* this will generate sensible error message on attempt to use value
           see reduction rule for DATAPAIR */
    }
    pnvec[i = hd[n]] = x;
    tag[n] = ID;
    hd[n] = hd[x];
    tag[x] = STRCONS;
    hd[x] = i;
    while (hd[h] != x) h = tl[h];
    hd[h] = n;
    return n;
} /* WARNING - dependent on internal representation of ids and pnames */
/* nasty problem - privatisation can screw AKA's */

word publicise(word x) { /* converse of the above, applied to the new id */
    extern word namebucket[];
    word i = id_val(x), h = namebucket[hash(get_id(x))];
    tag[i] = ID, hd[i] = hd[x];
    /* WARNING - USES FACT THAT tl HOLDS VALUE FOR BOTH ID AND PNAME */
    if (tag[tl[i]] == AP && tag[hd[tl[i]]] == DATAPAIR)
        tl[i] = UNDEF; /* undo kludge, see above */
    while (hd[h] != x) h = tl[h];
    hd[h] = i;
    return i;
}

int sigflag = 0;

void sigdefer(void) {
    /* printf("sigdefer()\n"); /* DEBUG */
    sigflag = 1;
} /* delayed signal handler, installed during load_script() */

word mkincludes(word includees_list) {
    extern word FBS, BAD_DUMP, CLASHES, exportfiles, exports, TORPHANS;
    pid_t pid; /* Use pid_t for process IDs */
    word result = NIL, tclashes = NIL;
    includees_list = reverse(includees_list); /* process in order of occurrence in script */

    if ((pid = fork()) == -1) { // Check for fork failure
        perror("UNIX error - cannot create process"); /* will say why */
        if (ideep > 6) /* perhaps cyclic %include */
            fprintf(stderr, "error occurs %d deep in %%include files\n", ideep);
        if (ideep) exit(2);
        SYNERR = 2; /* special code to prevent makedump() */
        printf("compilation of \"%s\" abandoned\n", current_script);
        return NIL;
    } else if (pid == 0) { /* child does equivalent of `mira -make' on each includee */
        extern word oldfiles;
        (void)signal(SIGINT, SIG_DFL); /* don't trap interrupts */
        ideep++;
        making = 1;
        make_status = 0;
        echoing = listing = verbosity = magic = 0;
        setjmp(env); /* will return here on blankerr (via reset) */
        while (includees_list != NIL && !make_status) { /* stop at first bad includee */
            undump((char *)hd[hd[hd[includees_list]]]);
            if (ND != NIL || (files == NIL && oldfiles != NIL)) make_status = 1;
            /* any errors in dump? */
            includees_list = tl[includees_list];
        } /* obscure bug - undump above can reinvoke compiler, which
           side effects compiler variable `includees' - to fix this
           had to make sure child is holding local copy of includees*/
        exit((int)make_status);
    } else { /* parent */
        int status;
        while (pid != wait(&status));
        if ((WEXITSTATUS(status)) == 2) { /* child aborted */
            if (ideep) exit(2); /* recursive abortion of parent process */
            else {
                SYNERR = 2;
                printf("compilation of \"%s\" abandoned\n", current_script);
                return NIL;
            }
        }
        /* if we get to here child completed normally, so carry on */
    }

    sigflag = 0;
    for (; includees_list != NIL; includees_list = tl[includees_list]) {
        word x = NIL;
        sighandler oldsig;
        FILE *f;
        char *fn = (char *)hd[hd[hd[includees_list]]];
        extern word DETROP, MISSING, ALIASES, TSUPPRESSED;
        (void)strcpy(dicp, fn);
        (void)strcpy(dicp + strlen(dicp) - 1, obsuffix);
        if (!making) /* cannot interrupt load_script() */
            oldsig = signal(SIGINT, (sighandler)sigdefer);
        if ((f = fopen(dicp, "r"))) {
            x = load_script(f, fn, hd[tl[hd[includees_list]]], tl[tl[hd[includees_list]]], 0);
            (void)fclose(f);
        }
        ld_stuff = cons(x, ld_stuff);
        if (!making) (void)signal(SIGINT, oldsig);
        if (sigflag) sigflag = 0, (*oldsig)(); /* take deferred interrupt */
        if (f && !BAD_DUMP && x != NIL && ND == NIL && CLASHES == NIL && ALIASES == NIL &&
            TSUPPRESSED == NIL && DETROP == NIL && MISSING == NIL)
        /* i.e. if load_script worked ok */
        {
            /* stuff here is to share repeated file components
             * issues:
             * Consider only includees (fil_share=1), not insertees.
             * Effect of sharing is to replace value fields in later copies
             * by (pointers to) corresponding ids in first copy - so sharing
             * transmitted thru dumps.  It is illegal to have more than one
             * copy of a (non-synonym) type in the same scope, even under
             * different names. */
            word y, z;
            /* printf("start share analysis\n");  /* DEBUG */
            if (TORPHANS) rfl = shunt(x, rfl); /* file has type orphans */
            for (y = x; y != NIL; y = tl[y]) fil_inodev(hd[y]) = inodev(get_fil(hd[y]));
            for (y = x; y != NIL; y = tl[y])
                if (fil_share(hd[y]))
                    for (z = result; z != NIL; z = tl[z])
                        if (fil_share(hd[z]) && same_file(hd[y], hd[z])
                            && fil_time(hd[y]) == fil_time(hd[z])) {
                            word p = fil_defs(hd[y]), q = fil_defs(hd[z]);
                            for (; p != NIL && q != NIL; p = tl[p], q = tl[q])
                                if (tag[hd[p]] == ID)
                                    if (id_type(hd[p]) == type_t &&
                                        (tag[hd[q]] == ID || tag[pn_val(hd[q])] == ID)) {
                                        /* typeclash - record in tclashes */
                                        word w = tclashes;
                                        word orig = tag[hd[q]] == ID ? hd[q] : pn_val(hd[q]);
                                        if (t_class(hd[p]) == synonym_t) continue;
                                        while (w != NIL && ((char *)hd[hd[w]] != get_fil(hd[z])
                                                             || hd[tl[hd[w]]] != orig))
                                            w = tl[w];
                                        if (w == NIL)
                                            w = tclashes = cons(strcons(get_fil(hd[z]),
                                                                        cons(orig, NIL)), tclashes);
                                        tl[tl[hd[w]]] = cons(hd[p], tl[tl[hd[w]]]);
                                    } else
                                        the_val(hd[q]) = hd[p];
                                else
                                    the_val(hd[p]) = hd[q];
                            /*following test redundant - remove when sure is ok*/
                            if (p != NIL || q != NIL)
                                fprintf(stderr, "impossible event in mkincludes\n");
                            /*break; /* z loop -- NO! (see liftbug) */
                        }
            if (member(exportfiles, (word)fn)) {
                /* move ids of x onto exports */
                for (y = x; y != NIL; y = tl[y])
                    for (z = fil_defs(hd[y]); z != NIL; z = tl[z])
                        if (isvariable(hd[z]))
                            tl[exports] = add1(hd[z], tl[exports]);
                /* skip pnames, constructors (expanded later) */
            }
            result = append1(result, x);
            /* keep `result' in front-first order */
            if (hd[FBS] == NIL) FBS = tl[FBS];
            else hd[FBS] = cons(tl[hd[hd[includees_list]]], hd[FBS]); /* hereinfo */
            /* printf("share analysis finished\n");  /* DEBUG */
            continue;
        }
        /* something wrong - find out what */
        if (!f) result = cons(make_fil(hd[hd[hd[includees_list]]],
                                       fm_time(fn), 0, NIL), result);
        else if (x == NIL && BAD_DUMP != (word)-2) result = append1(result, oldfiles), oldfiles = NIL;
        else result = append1(result, x);
        /* above for benefit of `oldfiles' */
        /* BAD_DUMP -2 is nameclashes due to aliasing */
        SYNERR = 1;
        printf("unsuccessful %%include directive ");
        sayhere(tl[hd[hd[includees_list]]], 1);
        /* if(!f)printf("\"%s\" non-existent or unreadable\n",fn), */
        if (!f) printf("\"%s\" cannot be loaded\n", fn),
            CLASHES = DETROP = MISSING = NIL;
            /* just in case not cleared from a previous load_script() */
        else if (BAD_DUMP == (word)-2)
            printlist("aliasing causes nameclashes: ", CLASHES),
            CLASHES = NIL;
        else if (ALIASES != NIL || TSUPPRESSED != NIL) {
            if (ALIASES != NIL) {
                printf("alias fails (name%s not found in file",
                       tl[ALIASES] == NIL ? "" : "s");
                printlist("): ", ALIASES), ALIASES = NIL;
            }
            if (TSUPPRESSED != NIL) {
                printf("illegal alias (cannot suppress typename%s):",
                       tl[TSUPPRESSED] == NIL ? "" : "s");
                while (TSUPPRESSED != NIL) {
                    printf(" -%s", get_id(hd[TSUPPRESSED]));
                    TSUPPRESSED = tl[TSUPPRESSED];
                }
                putchar('\n');
            }
            /* if -typename allowed, remember to look for type orphans */
        } else if (BAD_DUMP) printf("\"%s\" has bad data in dump file\n", fn);
        else if (x == NIL) printf("\"%s\" contains syntax error\n", fn);
        else if (ND != NIL)
            printf("\"%s\" contains undefined names or type errors\n", fn);
        if (ND == NIL && CLASHES != NIL) /* can have this and failed aliasing */
            printf("\"%s\" ", fn), printlist("causes nameclashes: ", CLASHES);
        while (DETROP != NIL && tag[hd[DETROP]] == CONS) {
            word fa = hd[tl[hd[DETROP]]], ta = tl[tl[hd[DETROP]]];
            char *pn = get_id(hd[hd[DETROP]]);
            if (fa == (word)-1 || ta == (word)-1)
                printf("`%s' has binding of wrong kind ", pn),
                printf(fa == (word)-1 ? "(should be \"= value\" not \"== type\")\n"
                                     : "(should be \"== type\" not \"= value\")\n");
            else
                printf("`%s' has == binding of wrong arity ", pn),
                printf("(formal has arity %ld, actual has arity %ld)\n", (long)fa, (long)ta);
            DETROP = tl[DETROP];
        }
        if (DETROP != NIL)
            printf("illegal parameter binding (name%s not %%free in file",
                   tl[DETROP] == NIL ? "" : "s"),
            printlist("): ", DETROP), DETROP = NIL;
        if (MISSING != NIL)
            printf("missing parameter binding%s: ", tl[MISSING] == NIL ? "" : "s");
        while (MISSING != NIL) {
            printf("%s%s", (char *)hd[hd[MISSING]], tl[MISSING] == NIL ? ";\n" : ",");
            MISSING = tl[MISSING];
        }
        printf("compilation abandoned\n");
        stackp = dstack; /* in case of BAD_DUMP */
        return result;
    } /* for unload() */
    if (tclashes != NIL) {
        printf("TYPECLASH - the following type%s multiply named:\n",
               tl[tclashes] == NIL ? " is" : "s are");
        /* structure of tclashes is list of strcons(filname,list-of-ids) */
        for (; tclashes != NIL; tclashes = tl[tclashes]) {
            printf("\'%s\' of file \"%s\", as: ",
                   getaka(hd[tl[hd[tclashes]]]),
                   (char *)hd[hd[tclashes]]);
            printlist("", alfasort(tl[hd[tclashes]]));
        }
        printf("typecheck cannot proceed - compilation abandoned\n");
        SYNERR = 1;
        return result;
    } /* for unload */
    return result;
}

word tlost = NIL;
word pfrts = NIL; /* list of private free types bound in this script */

void readoption(void) { /* readopt type orphans */
    word f, t_val;
    extern word TYPERRS, FBS;
    pfrts = tlost = NIL;
    /* exclude anonymous free types, these dealt with later by mcheckfbs() */
    if (FBS != NIL)
        for (f = FBS; f != NIL; f = tl[f])
            for (t_val = tl[hd[f]]; t_val != NIL; t_val = tl[t_val])
                if (tag[hd[hd[t_val]]] == STRCONS && tl[tl[hd[t_val]]] == type_t)
                    pfrts = cons(hd[hd[t_val]], pfrts);
    /* this may needlessly scan `silent' files - fix later */
    for (; rfl != NIL; rfl = tl[rfl])
        for (f = fil_defs(hd[rfl]); f != NIL; f = tl[f])
            if (tag[hd[f]] == ID)
                if ((t_val = id_type(hd[f])) == type_t) {
                    if (t_class(hd[f]) == synonym_t)
                        t_info(hd[f]) = fixtype(t_info(hd[f]), hd[f]);
                } else
                    id_type(hd[f]) = fixtype(t_val, hd[f]);
    if (tlost == NIL) return;
    TYPERRS++;
    printf("MISSING TYPENAME%s\n", tl[tlost] == NIL ? "" : "S");
    printf("the following type%s no name in this scope:\n",
           tl[tlost] == NIL ? " is needed but has" : "s are needed but have");
    /* structure of tlost is list of cons(losttype,list-of-ids) */
    for (; tlost != NIL; tlost = tl[tlost]) {
        /* printf("tinfo_tlost=");out(stdout,t_info(hd[hd[tlost]]));
           putchar(';'); /*DEBUG */
        printf("\'%s\' of file \"%s\", needed by: ",
               (char *)hd[hd[t_info(hd[hd[tlost]])]],
               (char *)hd[tl[t_info(hd[hd[tlost]])]]);
        printlist("", alfasort(tl[hd[tlost]]));
    }
}

word fixtype(word t, word x) { /* substitute out any indirected typenames in t */
    switch (tag[t]) {
        case AP:
        case CONS:
            tl[t] = fixtype(tl[t], x);
            hd[t] = fixtype(hd[t], x);
            return t; // Explicit return for these cases
        default:
            return t;
        case STRCONS:
            if (member(pfrts, t)) return t; /* see jrcfree.bug */
            while (tag[pn_val(t)] != CONS) t = pn_val(t); /*at most twice*/
            if (tag[t] != ID) {
                /* lost type - record in tlost */
                word w = tlost;
                while (w != NIL && hd[hd[w]] != t) w = tl[w];
                if (w == NIL)
                    w = tlost = cons(cons(t, cons(x, NIL)), tlost);
                tl[hd[w]] = add1(x, tl[hd[w]]);
            }
            return t;
    }
}

#define MASK(c) ((c) & 0xDF)
/* masks out lower case bit, which is 0x20  */
word alfa_ls(const char *a, const char *b) { /* 'DICTIONARY ORDER' - not currently used */
    while (*a && MASK((unsigned char)*a) == MASK((unsigned char)*b)) a++, b++;
    if (MASK((unsigned char)*a) == MASK((unsigned char)*b)) return (word)(strcmp(a, b) < 0); /* lower case before upper */
    return (word)(MASK((unsigned char)*a) < MASK((unsigned char)*b));
}

word alfasort(word x) { /* also removes non_IDs from result */
    word a = NIL, b = NIL, hold = NIL;
    if (x == NIL) return NIL;
    if (tl[x] == NIL) return (tag[hd[x]] != ID ? NIL : x);
    while (x != NIL) { /* split x */
        if (tag[hd[x]] == ID) hold = a, a = cons(hd[x], b), b = hold;
        x = tl[x];
    }
    a = alfasort(a), b = alfasort(b);
    /* now merge two halves back together */
    while (a != NIL && b != NIL)
        if (strcmp(get_id(hd[a]), get_id(hd[b])) < 0) x = cons(hd[a], x), a = tl[a];
        else x = cons(hd[b], x), b = tl[b];
    if (a == NIL) a = b;
    while (a != NIL) x = cons(hd[a], x), a = tl[a];
    return reverse(x);
}

void unsetids(word d) { /* d is a list of identifiers */
    while (d != NIL) {
        if (tag[hd[d]] == ID) id_val(hd[d]) = UNDEF,
                              id_who(hd[d]) = NIL,
                              id_type(hd[d]) = undef_t;
        d = tl[d];
    } /* should we remove from namebucket ? */
}

void unload(void) { /* clear out current script in preparation for reloading */
    extern word TABSTRS, SGC, speclocs, newtyps, rv_script, algshfns, nextpn, nolib,
        includees, freeids;
    word x;
    sorted = 0;
    speclocs = NIL;
    nextpn = 0; /* lose pnames */
    rv_script = 0;
    algshfns = NIL;
    unsetids(newtyps);
    newtyps = NIL;
    unsetids(freeids);
    freeids = includees = SGC = freeids = TABSTRS = ND = NIL; /* Note: freeids listed twice, likely a copy-paste error in original */
    unsetids(internals);
    internals = NIL;
    while (files != NIL) {
        unsetids(fil_defs(hd[files]));
        fil_defs(hd[files]) = NIL;
        files = tl[files];
    }
    for (; ld_stuff != NIL; ld_stuff = tl[ld_stuff])
        for (x = hd[ld_stuff]; x != NIL; x = tl[x]) unsetids(fil_defs(hd[x]));
}

void yyerror(const char *s) { /* called by YACC in the event of a syntax error */
    extern int yychar;
    if (SYNERR) return; /* error already reported, so shut up */
    if (echoing) printf("\n");
    printf("%s - unexpected ", s);
    if (yychar == OFFSIDE && (c == EOF || c == '|')) {
        if (c == EOF) /* special case introduced by fix for dtbug */
            printf("end of file");
        else
            printf("token '|'");
        /* special case introduced by sreds fix to offside rule */
    } else {
        printf(yychar == 0 ? (commandmode ? "newline" : "end of file") : "token ");
        if (yychar >= 256) putchar('\"');
        if (yychar != 0) out2(stdout, yychar);
        if (yychar >= 256) putchar('\"');
    }
    printf("\n");
    SYNERR = 1;
    reset_lex();
}

void syntax(const char *s) { /* called by actions after discovering a (context sensitive) syntax
              error */
    if (SYNERR) return;
    if (echoing) printf("\n");
    printf("syntax error: %s", s);
    SYNERR = 1; /* this will stop YACC at its next call to yylex() */
    reset_lex();
}

void acterror(void) { /* likewise, but assumes error message output by caller */
    if (SYNERR) return;
    SYNERR = 1; /* to stop YACC at next symbol */
    reset_lex();
}

void mira_setup(void) {
    extern word common_stdin, common_stdinb, cook_stdin;
    setupheap();
    tsetup();
    reset_pns();
    bigsetup();
    common_stdin = ap(READ, 0);
    common_stdinb = ap(READBIN, 0);
    cook_stdin = ap(readvals(0, 0), OFFSIDE);
    nill = cons(CONST, NIL);
    Void = make_id("()");
    id_type(Void) = void_t;
    id_val(Void) = constructor(0, Void);
    message = make_id("sys_message");
    main_id = make_id("main"); /* change to magic scripts 19.11.2013 */
    concat = make_id("concat");
    diagonalise = make_id("diagonalise");
    standardout = constructor(0, "Stdout");
    indent_fn = make_id("indent");
    outdent_fn = make_id("outdent");
    listdiff_fn = make_id("listdiff");
    shownum1 = make_id("shownum1");
    showbool = make_id("showbool");
    showchar = make_id("showchar");
    showlist = make_id("showlist");
    showstring = make_id("showstring");
    showparen = make_id("showparen");
    showpair = make_id("showpair");
    showvoid = make_id("showvoid");
    showfunction = make_id("showfunction");
    showabstract = make_id("showabstract");
    showwhat = make_id("showwhat");
    primlib();
} /* sets up predefined ids, not referred to by rules.y */

void dieclean(void) { /* called if evaluation is interrupted - see rules.y */
    printf("<<...interrupt>>\n");
#ifndef NOSTATSONINT
    outstats(); /* suppress in presence of segfault on ^C with /count */
#endif
    exit(0);
}

/* the function process() creates a process and waits for it to die -
   returning 1 in the child and 0 in the parent - it is used in the
   evaluation command (see rules.y) */
word process(void) {
    pid_t pid;
    sighandler oldsig;
    oldsig = signal(SIGINT, SIG_IGN);
    /* do not let parent receive interrupts intended for child */
    if ((pid = fork()) == -1) {
        /* parent */
        perror("UNIX error - cannot create process");
        return 0;
    } else if (pid == 0) {
        /* child */
        return 1;
    } else {
        int status; /* see man 2 exit, wait, signal */
        while (pid != wait(&status));
        /* low byte of status is termination state of child, next byte is the
           (low order byte of the) exit status */
        if (WIFSIGNALED(status)) { /* abnormal termination status */
            const char *cd = (status & 0200) ? " (core dumped)" : "";
            const char *pc = ""; /* "probably caused by stack overflow\n";*/
            switch (WTERMSIG(status)) {
                case SIGBUS:
                    printf("\n<<...bus error%s>>\n%s", cd, pc);
                    break;
                case SIGSEGV:
                    printf("\n<<...segmentation fault%s>>\n%s", cd, pc);
                    break;
                default:
                    printf("\n<<...uncaught signal %d>>\n", WTERMSIG(status));
            }
        }
        /*if(status >>= 8)printf("\n(exit status %d)\n",status); */
        (void)signal(SIGINT, oldsig); /* restore interrupt status */
        return 0;
    }
}

/* Notice that the Miranda system has a two-level interrupt structure.
   1) Each evaluation (see rules.y) is an interruptible process.
   2) If the command loop is interrupted outside an evaluation or during
      compilation it reverts to the top level prompt - see set_jmp and
      signal(reset) in commandloop() */

void primdef(const char *n, word v, word t) { /* used by "primlib", see below  */
    word x;
    x = make_id(n);
    primenv = cons(x, primenv);
    id_val(x) = v;
    id_type(x) = t;
}

void predef(const char *n, word v, word t) { /* used by "privlib" and "stdlib", see below  */
    word x;
    x = make_id(n);
    addtoenv(x);
    id_val(x) = isconstructor(x) ? constructor(v, x) : v;
    id_type(x) = t;
}

void primlib(void) { /* called by "mira_setup", this routine enters
                the primitive identifiers into the primitive environment  */
    extern word ltchar;
    primdef("num", make_typ(0, 0, synonym_t, num_t), type_t);
    primdef("char", make_typ(0, 0, synonym_t, char_t), type_t);
    primdef("bool", make_typ(0, 0, synonym_t, bool_t), type_t);
    primdef("True", 1, bool_t); /* accessible only to 'finger' */
    primdef("False", 0, bool_t); /* likewise - FIX LATER */
}

void privlib(void) { /* called when compiling <prelude>, adds some
                internally defined identifiers to the environment  */
    extern word ltchar;
    predef("offside", OFFSIDE, ltchar); /* used by `indent' in prelude */
    predef("changetype", I, wrong_t); /* wrong_t to prevent being typechecked */
    predef("first", HD, wrong_t);
    predef("rest", TL, wrong_t);
/* the following added to make prelude compilable without stdenv */
    predef("code", CODE, undef_t);
    predef("concat", ap2(FOLDR, APPEND, NIL), undef_t);
    predef("decode", DECODE, undef_t);
    predef("drop", DROP, undef_t);
    predef("error", ERROR, undef_t);
    predef("filter", FILTER, undef_t);
    predef("foldr", FOLDR, undef_t);
    predef("hd", HD, undef_t);
    predef("map", MAP, undef_t);
    predef("shownum", SHOWNUM, undef_t);
    predef("take", TAKE, undef_t);
    predef("tl", TL, undef_t);
}

void stdlib(void) { /* called when compiling <stdenv>, adds some
             internally defined identifiers to the environment  */
    predef("arctan", ARCTAN_FN, undef_t);
    predef("code", CODE, undef_t);
    predef("cos", COS_FN, undef_t);
    predef("decode", DECODE, undef_t);
    predef("drop", DROP, undef_t);
    predef("entier", ENTIER_FN, undef_t);
    predef("error", ERROR, undef_t);
    predef("exp", EXP_FN, undef_t);
    predef("filemode", FILEMODE, undef_t);
    predef("filestat", FILESTAT, undef_t); /* added Feb 91 */
    predef("foldl", FOLDL, undef_t);
    predef("foldl1", FOLDL1, undef_t); /* new at release 2 */
    predef("hugenum", sto_dbl(DBL_MAX), undef_t);
    predef("last", LIST_LAST, undef_t);
    predef("foldr", FOLDR, undef_t);
    predef("force", FORCE, undef_t);
    predef("getenv", GETENV, undef_t);
    predef("integer", INTEGER, undef_t);
    predef("log", LOG_FN, undef_t);
    predef("log10", LOG10_FN, undef_t); /* new at release 2 */
    predef("merge", MERGE, undef_t); /* new at release 2 */
    predef("numval", NUMVAL, undef_t);
    predef("read", STARTREAD, undef_t);
    predef("readb", STARTREADBIN, undef_t);
    predef("seq", SEQ, undef_t);
    predef("shownum", SHOWNUM, undef_t);
    predef("showhex", SHOWHEX, undef_t);
    predef("showoct", SHOWOCT, undef_t);
    predef("showfloat", SHOWFLOAT, undef_t); /* new at release 2 */
    predef("showscaled", SHOWSCALED, undef_t); /* new at release 2 */
    predef("sin", SIN_FN, undef_t);
    predef("sqrt", SQRT_FN, undef_t);
    predef("system", EXEC, undef_t); /* new at release 2 */
    predef("take", TAKE, undef_t);
    predef("tinynum", mktiny(), undef_t); /* new at release 2 */
    predef("zip2", ZIP, undef_t); /* new at release 2 */
}

word mktiny(void) {
    volatile
    double x = 1.0, x1 = x / 2.0;
    while (x1 > 0.0) x = x1, x1 /= 2.0;
    return sto_dbl(x);
}

word size(word x) { /* measures the size of a compiled expression   */
    word s;
    s = 0;
    while (tag[x] == CONS || tag[x] == AP) {
        s = s + 1 + size(hd[x]);
        x = tl[x];
    }
    return s;
}

void makedump(void) {
    char obf[pnlim];
    FILE *f;
    // Ensure obf has enough space
    if (strlen(current_script) + strlen(obsuffix) >= sizeof(obf)) {
        fprintf(stderr, "WARNING: Pathname buffer too small in makedump.\n");
        if (making && !make_status) make_status = 1;
        return;
    }
    (void)strcpy(obf, current_script);
    (void)strcpy(obf + strlen(obf) - 1, obsuffix);
    f = fopen(obf, "w");
    if (!f) {
        printf("WARNING: CANNOT WRITE TO %s\n", obf);
        if (strcmp(current_script, PRELUDE) == 0 ||
            strcmp(current_script, STDENV) == 0)
            printf(
                "TO FIX THIS PROBLEM PLEASE GET SUPER-USER TO EXECUTE `mira'\n");
        if (making && !make_status) make_status = 1;
        return;
    }
    /* printf("dumping to %s\n",obf); /* DEBUG */
    unlinkme = obf;
    /* fchmod(fileno(f),0666); /* to make dumps writeable by all */ /* no! */
    setprefix(current_script);
    dump_script(files, f);
    unlinkme = NULL;
    (void)fclose(f);
}

void undump(char *t) { /* restore t from dump, or recompile if necessary */
    extern word BAD_DUMP, CLASHES;
    if (!normal(t) && !initialising) return loadfile(t);
    /* except for prelude, only .m files have dumps */
    char obf[pnlim];
    FILE *f;
    sighandler oldsig;
    size_t flen = strlen(t);
    time_t t1 = fm_time(t), t2;
    if (flen >= sizeof(obf)) { // Use >= for buffer checks
        printf("sorry, pathname too long (limit=%zu): %s\n", sizeof(obf), t);
        return;
    }
    (void)strcpy(obf, t);
    (void)strcpy(obf + flen - 1, obsuffix);
    t2 = fm_time(obf);
    if (t2 && !t1) t2 = 0, unlink(obf); /* dump is orphan - remove */
    if (!t2 || t2 < t1) { /* dump is nonexistent or older than source - ignore */
        loadfile(t);
        return;
    }
    f = fopen(obf, "r");
    if (!f) {
        printf("cannot open %s\n", obf);
        loadfile(t);
        return;
    }
    current_script = t;
    loading = 1;
    oldfiles = NIL;
    unload();
    /*if(!initialising)printf("undumping from %s\n",obf); /* DEBUG */
    if (!initialising && !making) /* ie this is the main script */
        sigflag = 0,
        oldsig = signal(SIGINT, (sighandler)sigdefer);
    /* can't take interrupt during load_script */
    files = load_script(f, t, NIL, NIL, !making & !initialising);
    (void)fclose(f);
    if (BAD_DUMP) {
        unlink(obf);
        unload();
        CLASHES = NIL;
        stackp = dstack;
        printf("warning: %s contains incorrect data (file removed)\n", obf);
        if (BAD_DUMP == (word)-1) printf("(unrecognised dump format)\n");
        else if (BAD_DUMP == 1) printf("(wrong source file)\n");
        else
            printf("(error %ld)\n", (long)BAD_DUMP);
    }
    if (!initialising && !making) /* restore interrupt handler */
        (void)signal(SIGINT, oldsig);
    if (sigflag) sigflag = 0, (*oldsig)(); /* take deferred interrupt */
    /*if(!initialising)printf("%s undumped\n",obf); /* DEBUG */
    if (CLASHES != NIL) {
        if (ideep == 0) printf("cannot load %s ", obf),
                        printlist("due to name clashes: ", alfasort(CLASHES));
        unload();
        loading = 0;
        return;
    }
    if (BAD_DUMP || src_update()) loadfile(t); /* any sources modified since dump? */
    else if (initialising) {
        if (ND != NIL || files == NIL) /* error in dump of PRELUDE */
            fprintf(stderr, "panic: %s contains errors\n", obf),
            exit(1);
    } /* beware of dangling else ! (whence {}) */
    else if (verbosity || magic || mkexports) /* for less silent making s/mkexports/making/ */
        if (files == NIL) printf("%s contains syntax error\n", t);
        else if (ND != NIL) printf("%s contains undefined names or type errors\n", t);
        else if (!making && !magic) printf("%s\n", t); /* added &&!magic 26.11.2019 */
    if (files != NIL && !making & !initialising) unfixexports();
    loading = 0;
}

void unlinkx(char *t) { /* remove orphaned .x file */
    char obf[pnlim];
    // Ensure obf has enough space
    if (strlen(t) + strlen(obsuffix) >= sizeof(obf)) {
        fprintf(stderr, "WARNING: Pathname buffer too small in unlinkx.\n");
        return;
    }
    (void)strcpy(obf, t);
    (void)strcpy(obf + strlen(t) - 1, obsuffix);
    if (!stat(obf, &buf)) unlink(obf);
}

void fpe_error(void) {
    if (compiling) {
        (void)signal(SIGFPE, (sighandler)fpe_error); /* reset SIGFPE trap */
#ifdef sparc8
        fpsetmask(commonmask); /* to clear sticky bits */
#endif
        syntax("floating point number out of range\n");
        SYNERR = 0;
        longjmp(env, 1);
        /* go straight back to commandloop - necessary because decoding very
           large numbers can cause huge no. of repeated SIGFPE exceptions */
    } else {
        printf("\nFLOATING POINT OVERFLOW\n");
        exit(1);
    }
}

char fbuf[512];

void filecopy(const char *fil) { /* copy the file "fil" to standard out */
    int in = open(fil, O_RDONLY); // Use O_RDONLY
    ssize_t n;
    if (in == -1) return;
    while ((n = read(in, fbuf, sizeof(fbuf))) > 0) write(1, fbuf, (size_t)n);
    close(in);
}

void filecp(const char *fil1, const char *fil2) { /* copy file "fil1" to "fil2" (like `cp') */
    int in = open(fil1, O_RDONLY);
    ssize_t n;
    int out = creat(fil2, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); // Use symbolic permissions
    if (in == -1 || out == -1) return;
    while ((n = read(in, fbuf, sizeof(fbuf))) > 0) write(out, fbuf, (size_t)n);
    close(in);
    close(out);
}

int twidth(void) { /* returns width (in columns) of current window, less 2 */
#ifdef TIOCGWINSZ
    static struct winsize tsize;
    if (ioctl(fileno(stdout), TIOCGWINSZ, &tsize) == 0) {
        return (tsize.ws_col == 0) ? 78 : (int)tsize.ws_col - 2;
    }
#else
/* porting note: if you cannot find how to enable use of TIOCGWINSZ
   comment out the above #error line */
#error TIOCGWINSZ undefined
#endif
    return 78; /* give up, we will assume screen width to be 80 */
}

/* was called when Miranda starts up and before /help, /aux
   to clear screen - suppressed Oct 2019 */
/* clrscr()
{ printf("\x1b[2J\x1b[H"); fflush(stdout);
} */

/* the following code tests if we are in a UTF-8 locale */

#ifdef CYGWIN
#include <windows.h>

int utf8test(void) {
    return GetACP() == 65001;
}
/* codepage 1252 is Windows version of Latin-1; 65001 is UTF-8 */

#else

int utf8test(void) {
    char *lang;
    if (!(lang = getenv("LC_CTYPE")))
        lang = getenv("LANG");
    if (lang &&
        (strstr(lang, "UTF-8") || strstr(lang, "UTF8") ||
         strstr(lang, "utf-8") || strstr(lang, "utf8")))
        return 1;
    return 0;
}
#endif
