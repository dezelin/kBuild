/* Argument parsing and main program of GNU Make.
Copyright (C) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997,
1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007 Free Software
Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "make.h"
#include "dep.h"
#include "filedef.h"
#include "variable.h"
#include "job.h"
#include "commands.h"
#include "rule.h"
#include "debug.h"
#include "getopt.h"
#ifdef KMK
# include "kbuild.h"
#endif

#include <assert.h>
#ifdef _AMIGA
# include <dos/dos.h>
# include <proto/dos.h>
#endif
#ifdef WINDOWS32
#include <windows.h>
#include <io.h>
#include "pathstuff.h"
#endif
#ifdef __EMX__
# include <sys/types.h>
# include <sys/wait.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef KMK /* for get_online_cpu_count */
# if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#  include <sys/sysctl.h>
# endif
# ifdef __OS2__
#  define INCL_BASE
#  include <os2.h>
# endif
#endif /* KMK*/

#if defined(HAVE_SYS_RESOURCE_H) && defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT)
# define SET_STACK_SIZE
#endif

#ifdef SET_STACK_SIZE
# include <sys/resource.h>
#endif

#ifdef _AMIGA
int __stack = 20000; /* Make sure we have 20K of stack space */
#endif

void init_dir (void);
void remote_setup (void);
void remote_cleanup (void);
RETSIGTYPE fatal_error_signal (int sig);

void print_variable_data_base (void);
void print_dir_data_base (void);
void print_rule_data_base (void);
void print_file_data_base (void);
void print_vpath_data_base (void);

void verify_file_data_base (void);

#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
void print_variable_stats (void);
void print_file_stats (void);
#endif

#if defined HAVE_WAITPID || defined HAVE_WAIT3
# define HAVE_WAIT_NOHANG
#endif

#if !defined(HAVE_UNISTD_H) && !defined(_MSC_VER) /* bird */
int chdir ();
#endif
#ifndef	STDC_HEADERS
# ifndef sun			/* Sun has an incorrect decl in a header.  */
void exit (int) __attribute__ ((noreturn));
# endif
double atof ();
#endif

static void clean_jobserver (int status);
static void print_data_base (void);
static void print_version (void);
static void decode_switches (int argc, char **argv, int env);
static void decode_env_switches (char *envar, unsigned int len);
static void define_makeflags (int all, int makefile);
static char *quote_for_env (char *out, const char *in);
static void initialize_global_hash_tables (void);


/* The structure that describes an accepted command switch.  */

struct command_switch
  {
    int c;			/* The switch character.  */

    enum			/* Type of the value.  */
      {
	flag,			/* Turn int flag on.  */
	flag_off,		/* Turn int flag off.  */
	string,			/* One string per switch.  */
	filename,		/* A string containing a file name.  */
	positive_int,		/* A positive integer.  */
	floating,		/* A floating-point number (double).  */
	ignore			/* Ignored.  */
      } type;

    void *value_ptr;	/* Pointer to the value-holding variable.  */

    unsigned int env:1;		/* Can come from MAKEFLAGS.  */
    unsigned int toenv:1;	/* Should be put in MAKEFLAGS.  */
    unsigned int no_makefile:1;	/* Don't propagate when remaking makefiles.  */

    const void *noarg_value;	/* Pointer to value used if no arg given.  */
    const void *default_value;	/* Pointer to default value.  */

    char *long_name;		/* Long option name.  */
  };

/* True if C is a switch value that corresponds to a short option.  */

#define short_option(c) ((c) <= CHAR_MAX)

/* The structure used to hold the list of strings given
   in command switches of a type that takes string arguments.  */

struct stringlist
  {
    const char **list;	/* Nil-terminated list of strings.  */
    unsigned int idx;	/* Index into above.  */
    unsigned int max;	/* Number of pointers allocated.  */
  };


/* The recognized command switches.  */

/* Nonzero means do not print commands to be executed (-s).  */

int silent_flag;

/* Nonzero means just touch the files
   that would appear to need remaking (-t)  */

int touch_flag;

/* Nonzero means just print what commands would need to be executed,
   don't actually execute them (-n).  */

int just_print_flag;

#ifdef CONFIG_PRETTY_COMMAND_PRINTING
/* Nonzero means to print commands argument for argument skipping blanks. */

int pretty_command_printing;
#endif

#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
/* Nonzero means to print internal statistics before exiting. */

int print_stats_flag;
#endif

#ifdef CONFIG_WITH_PRINT_TIME_SWITCH
/* Minimum number of seconds to report, -1 if disabled. */

int print_time_min = -1;
static int default_print_time_min = -1;
static int no_val_print_time_min = 0;
static big_int make_start_ts = -1;
int print_time_width = 5;
#endif

/* Print debugging info (--debug).  */

static struct stringlist *db_flags;
static int debug_flag = 0;

int db_level = 0;

/* Output level (--verbosity).  */

static struct stringlist *verbosity_flags;

#ifdef WINDOWS32
/* Suspend make in main for a short time to allow debugger to attach */

int suspend_flag = 0;
#endif

/* Environment variables override makefile definitions.  */

int env_overrides = 0;

/* Nonzero means ignore status codes returned by commands
   executed to remake files.  Just treat them all as successful (-i).  */

int ignore_errors_flag = 0;

/* Nonzero means don't remake anything, just print the data base
   that results from reading the makefile (-p).  */

int print_data_base_flag = 0;

/* Nonzero means don't remake anything; just return a nonzero status
   if the specified targets are not up to date (-q).  */

int question_flag = 0;

/* Nonzero means do not use any of the builtin rules (-r) / variables (-R).  */

int no_builtin_rules_flag = 0;
int no_builtin_variables_flag = 0;

/* Nonzero means keep going even if remaking some file fails (-k).  */

int keep_going_flag;
int default_keep_going_flag = 0;

/* Nonzero means check symlink mtimes.  */

int check_symlink_flag = 0;

/* Nonzero means print directory before starting and when done (-w).  */

int print_directory_flag = 0;

/* Nonzero means ignore print_directory_flag and never print the directory.
   This is necessary because print_directory_flag is set implicitly.  */

int inhibit_print_directory_flag = 0;

/* Nonzero means print version information.  */

int print_version_flag = 0;

/* List of makefiles given with -f switches.  */

static struct stringlist *makefiles = 0;

/* Number of job slots (commands that can be run at once).  */

unsigned int job_slots = 1;
unsigned int default_job_slots = 1;
static unsigned int master_job_slots = 0;

/* Value of job_slots that means no limit.  */

static unsigned int inf_jobs = 0;

/* File descriptors for the jobs pipe.  */

static struct stringlist *jobserver_fds = 0;

int job_fds[2] = { -1, -1 };
int job_rfd = -1;

/* Maximum load average at which multiple jobs will be run.
   Negative values mean unlimited, while zero means limit to
   zero load (which could be useful to start infinite jobs remotely
   but one at a time locally).  */
#ifndef NO_FLOAT
double max_load_average = -1.0;
double default_load_average = -1.0;
#else
int max_load_average = -1;
int default_load_average = -1;
#endif

/* List of directories given with -C switches.  */

static struct stringlist *directories = 0;

/* List of include directories given with -I switches.  */

static struct stringlist *include_directories = 0;

/* List of files given with -o switches.  */

static struct stringlist *old_files = 0;

/* List of files given with -W switches.  */

static struct stringlist *new_files = 0;

/* If nonzero, we should just print usage and exit.  */

static int print_usage_flag = 0;

/* If nonzero, we should print a warning message
   for each reference to an undefined variable.  */

int warn_undefined_variables_flag;

/* If nonzero, always build all targets, regardless of whether
   they appear out of date or not.  */

static int always_make_set = 0;
int always_make_flag = 0;

/* If nonzero, we're in the "try to rebuild makefiles" phase.  */

int rebuilding_makefiles = 0;

/* Remember the original value of the SHELL variable, from the environment.  */

struct variable shell_var;

/* This character introduces a command: it's the first char on the line.  */

char cmd_prefix = '\t';

#ifdef KMK
/* Process priority.
    0 = no change;
    1 = idle / max nice;
    2 = below normal / nice 10;
    3 = normal / nice 0;
    4 = high / nice -10;
    5 = realtime / nice -19; */

int process_priority = 0;

/* Process affinity mask; 0 means any CPU. */

int process_affinity = 0;
#endif /* KMK */

#if defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)
/* When set, we'll gather expensive statistics like for the heap. */

int make_expensive_statistics = 0;
#endif


/* The usage output.  We write it this way to make life easier for the
   translators, especially those trying to translate to right-to-left
   languages like Hebrew.  */

static const char *const usage[] =
  {
    N_("Options:\n"),
    N_("\
  -b, -m                      Ignored for compatibility.\n"),
    N_("\
  -B, --always-make           Unconditionally make all targets.\n"),
    N_("\
  -C DIRECTORY, --directory=DIRECTORY\n\
                              Change to DIRECTORY before doing anything.\n"),
    N_("\
  -d                          Print lots of debugging information.\n"),
    N_("\
  --debug[=FLAGS]             Print various types of debugging information.\n"),
    N_("\
  -e, --environment-overrides\n\
                              Environment variables override makefiles.\n"),
    N_("\
  -f FILE, --file=FILE, --makefile=FILE\n\
                              Read FILE as a makefile.\n"),
    N_("\
  -h, --help                  Print this message and exit.\n"),
    N_("\
  -i, --ignore-errors         Ignore errors from recipes.\n"),
    N_("\
  -I DIRECTORY, --include-dir=DIRECTORY\n\
                              Search DIRECTORY for included makefiles.\n"),
#ifdef KMK
    N_("\
  -j [N], --jobs[=N]          Allow N jobs at once; infinite jobs with no arg.\n\
                              The default is the number of active CPUs.\n"),
#else
    N_("\
  -j [N], --jobs[=N]          Allow N jobs at once; infinite jobs with no arg.\n"),
#endif
    N_("\
  -k, --keep-going            Keep going when some targets can't be made.\n"),
    N_("\
  -l [N], --load-average[=N], --max-load[=N]\n\
                              Don't start multiple jobs unless load is below N.\n"),
    N_("\
  -L, --check-symlink-times   Use the latest mtime between symlinks and target.\n"),
    N_("\
  -n, --just-print, --dry-run, --recon\n\
                              Don't actually run any recipe; just print them.\n"),
    N_("\
  -o FILE, --old-file=FILE, --assume-old=FILE\n\
                              Consider FILE to be very old and don't remake it.\n"),
    N_("\
  -p, --print-data-base       Print make's internal database.\n"),
    N_("\
  -q, --question              Run no recipe; exit status says if up to date.\n"),
    N_("\
  -r, --no-builtin-rules      Disable the built-in implicit rules.\n"),
    N_("\
  -R, --no-builtin-variables  Disable the built-in variable settings.\n"),
    N_("\
  -s, --silent, --quiet       Don't echo recipes.\n"),
    N_("\
  -S, --no-keep-going, --stop\n\
                              Turns off -k.\n"),
    N_("\
  -t, --touch                 Touch targets instead of remaking them.\n"),
    N_("\
  -v, --version               Print the version number of make and exit.\n"),
    N_("\
  -w, --print-directory       Print the current directory.\n"),
    N_("\
  --no-print-directory        Turn off -w, even if it was turned on implicitly.\n"),
    N_("\
  -W FILE, --what-if=FILE, --new-file=FILE, --assume-new=FILE\n\
                              Consider FILE to be infinitely new.\n"),
    N_("\
  --warn-undefined-variables  Warn when an undefined variable is referenced.\n"),
#ifdef KMK
    N_("\
  --affinity=mask             Sets the CPU affinity on some hosts.\n"),
    N_("\
  --priority=1-5              Sets the process priority / nice level:\n\
                                1 = idle / max nice;\n\
                                2 = below normal / nice 10;\n\
                                3 = normal / nice 0;\n\
                                4 = high / nice -10;\n\
                                5 = realtime / nice -19;\n"),
#endif /* KMK */
#ifdef CONFIG_PRETTY_COMMAND_PRINTING
    N_("\
  --pretty-command-printing   Makes the command echo easier to read.\n"),
#endif
#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
    N_("\
  --print-stats               Print make statistics.\n"),
#endif
#ifdef CONFIG_WITH_PRINT_TIME_SWITCH
    N_("\
  --print-time[=MIN-SEC]      Print file build times starting at arg.\n"),
#endif
#ifdef CONFIG_WITH_MAKE_STATS
    N_("\
  --statistics                Gather extra statistics for $(make-stats ).\n"),
#endif
    NULL
  };

/* The table of command switches.  */

static const struct command_switch switches[] =
  {
    { 'b', ignore, 0, 0, 0, 0, 0, 0, 0 },
    { 'B', flag, &always_make_set, 1, 1, 0, 0, 0, "always-make" },
    { 'C', filename, &directories, 0, 0, 0, 0, 0, "directory" },
    { 'd', flag, &debug_flag, 1, 1, 0, 0, 0, 0 },
    { CHAR_MAX+1, string, &db_flags, 1, 1, 0, "basic", 0, "debug" },
#ifdef WINDOWS32
    { 'D', flag, &suspend_flag, 1, 1, 0, 0, 0, "suspend-for-debug" },
#endif
    { 'e', flag, &env_overrides, 1, 1, 0, 0, 0, "environment-overrides", },
    { 'f', filename, &makefiles, 0, 0, 0, 0, 0, "file" },
    { 'h', flag, &print_usage_flag, 0, 0, 0, 0, 0, "help" },
    { 'i', flag, &ignore_errors_flag, 1, 1, 0, 0, 0, "ignore-errors" },
    { 'I', filename, &include_directories, 1, 1, 0, 0, 0,
      "include-dir" },
    { 'j', positive_int, &job_slots, 1, 1, 0, &inf_jobs, &default_job_slots,
      "jobs" },
    { CHAR_MAX+2, string, &jobserver_fds, 1, 1, 0, 0, 0, "jobserver-fds" },
    { 'k', flag, &keep_going_flag, 1, 1, 0, 0, &default_keep_going_flag,
      "keep-going" },
#ifndef NO_FLOAT
    { 'l', floating, &max_load_average, 1, 1, 0, &default_load_average,
      &default_load_average, "load-average" },
#else
    { 'l', positive_int, &max_load_average, 1, 1, 0, &default_load_average,
      &default_load_average, "load-average" },
#endif
    { 'L', flag, &check_symlink_flag, 1, 1, 0, 0, 0, "check-symlink-times" },
    { 'm', ignore, 0, 0, 0, 0, 0, 0, 0 },
    { 'n', flag, &just_print_flag, 1, 1, 1, 0, 0, "just-print" },
    { 'o', filename, &old_files, 0, 0, 0, 0, 0, "old-file" },
    { 'p', flag, &print_data_base_flag, 1, 1, 0, 0, 0, "print-data-base" },
#ifdef CONFIG_PRETTY_COMMAND_PRINTING
    { CHAR_MAX+10, flag, (char *) &pretty_command_printing, 1, 1, 1, 0, 0,
       "pretty-command-printing" },
#endif
#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
    { CHAR_MAX+11, flag, (char *) &print_stats_flag, 1, 1, 1, 0, 0,
       "print-stats" },
#endif
#ifdef CONFIG_WITH_PRINT_TIME_SWITCH
    { CHAR_MAX+12, positive_int, (char *) &print_time_min, 1, 1, 0,
      (char *) &no_val_print_time_min, (char *) &default_print_time_min,
      "print-time" },
#endif
#ifdef KMK
    { CHAR_MAX+14, positive_int, (char *) &process_priority, 1, 1, 0,
      (char *) &process_priority, (char *) &process_priority, "priority" },
    { CHAR_MAX+15, positive_int, (char *) &process_affinity, 1, 1, 0,
      (char *) &process_affinity, (char *) &process_affinity, "affinity" },
#endif
    { 'q', flag, &question_flag, 1, 1, 1, 0, 0, "question" },
    { 'r', flag, &no_builtin_rules_flag, 1, 1, 0, 0, 0, "no-builtin-rules" },
    { 'R', flag, &no_builtin_variables_flag, 1, 1, 0, 0, 0,
      "no-builtin-variables" },
    { 's', flag, &silent_flag, 1, 1, 0, 0, 0, "silent" },
    { 'S', flag_off, &keep_going_flag, 1, 1, 0, 0, &default_keep_going_flag,
      "no-keep-going" },
#if defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)
    { CHAR_MAX+16, flag, (char *) &make_expensive_statistics, 1, 1, 1, 0, 0,
       "statistics" },
#endif
    { 't', flag, &touch_flag, 1, 1, 1, 0, 0, "touch" },
    { 'v', flag, &print_version_flag, 1, 1, 0, 0, 0, "version" },
    { CHAR_MAX+3, string, &verbosity_flags, 1, 1, 0, 0, 0,
      "verbosity" },
    { 'w', flag, &print_directory_flag, 1, 1, 0, 0, 0, "print-directory" },
    { CHAR_MAX+4, flag, &inhibit_print_directory_flag, 1, 1, 0, 0, 0,
      "no-print-directory" },
    { 'W', filename, &new_files, 0, 0, 0, 0, 0, "what-if" },
    { CHAR_MAX+5, flag, &warn_undefined_variables_flag, 1, 1, 0, 0, 0,
      "warn-undefined-variables" },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 }
  };

/* Secondary long names for options.  */

static struct option long_option_aliases[] =
  {
    { "quiet",		no_argument,		0, 's' },
    { "stop",		no_argument,		0, 'S' },
    { "new-file",	required_argument,	0, 'W' },
    { "assume-new",	required_argument,	0, 'W' },
    { "assume-old",	required_argument,	0, 'o' },
    { "max-load",	optional_argument,	0, 'l' },
    { "dry-run",	no_argument,		0, 'n' },
    { "recon",		no_argument,		0, 'n' },
    { "makefile",	required_argument,	0, 'f' },
  };

/* List of goal targets.  */

#ifndef KMK
static
#endif
struct dep *goals, *lastgoal;

/* List of variables which were defined on the command line
   (or, equivalently, in MAKEFLAGS).  */

struct command_variable
  {
    struct command_variable *next;
    struct variable *variable;
  };
static struct command_variable *command_variables;

/* The name we were invoked with.  */

char *program;

/* Our current directory before processing any -C options.  */

char *directory_before_chdir;

/* Our current directory after processing all -C options.  */

char *starting_directory;

/* Value of the MAKELEVEL variable at startup (or 0).  */

unsigned int makelevel;

/* First file defined in the makefile whose name does not
   start with `.'.  This is the default to remake if the
   command line does not specify.  */

struct file *default_goal_file;

/* Pointer to the value of the .DEFAULT_GOAL special
   variable.  */
char ** default_goal_name;

/* Pointer to structure for the file .DEFAULT
   whose commands are used for any file that has none of its own.
   This is zero if the makefiles do not define .DEFAULT.  */

struct file *default_file;

/* Nonzero if we have seen the magic `.POSIX' target.
   This turns on pedantic compliance with POSIX.2.  */

int posix_pedantic;

/* Nonzero if we have seen the '.SECONDEXPANSION' target.
   This turns on secondary expansion of prerequisites.  */

int second_expansion;

#ifdef CONFIG_WITH_2ND_TARGET_EXPANSION
/* Nonzero if we have seen the '.SECONDTARGETEXPANSION' target.
   This turns on secondary expansion of targets.  */

int second_target_expansion;
#endif

#ifndef CONFIG_WITH_EXTENDED_NOTPARALLEL
/* Nonzero if we have seen the `.NOTPARALLEL' target.
   This turns off parallel builds for this invocation of make.  */

#else  /* CONFIG_WITH_EXTENDED_NOTPARALLEL */

/* Negative if we have seen the `.NOTPARALLEL' target with an
   empty dependency list.

   Zero if no `.NOTPARALLEL' or no file in the dependency list
   is being executed.

   Positive when a file in the `.NOTPARALLEL' dependency list
   is in progress, the value is the number of notparallel files
   in progress (running or queued for running).

   In short, any nonzero value means no more parallel builing. */
#endif /* CONFIG_WITH_EXTENDED_NOTPARALLEL */

int not_parallel;

/* Nonzero if some rule detected clock skew; we keep track so (a) we only
   print one warning about it during the run, and (b) we can print a final
   warning at the end of the run. */

int clock_skew_detected;

/* Mask of signals that are being caught with fatal_error_signal.  */

#ifdef	POSIX
sigset_t fatal_signal_set;
#else
# ifdef	HAVE_SIGSETMASK
int fatal_signal_mask;
# endif
#endif

#if !defined HAVE_BSD_SIGNAL && !defined bsd_signal
# if !defined HAVE_SIGACTION
#  define bsd_signal signal
# else
typedef RETSIGTYPE (*bsd_signal_ret_t) ();

static bsd_signal_ret_t
bsd_signal (int sig, bsd_signal_ret_t func)
{
  struct sigaction act, oact;
  act.sa_handler = func;
  act.sa_flags = SA_RESTART;
  sigemptyset (&act.sa_mask);
  sigaddset (&act.sa_mask, sig);
  if (sigaction (sig, &act, &oact) != 0)
    return SIG_ERR;
  return oact.sa_handler;
}
# endif
#endif

#ifdef CONFIG_WITH_ALLOC_CACHES
struct alloccache dep_cache;
struct alloccache file_cache;
struct alloccache commands_cache;
struct alloccache nameseq_cache;
struct alloccache variable_cache;
struct alloccache variable_set_cache;
struct alloccache variable_set_list_cache;

static void
initialize_global_alloc_caches (void)
{
  alloccache_init (&dep_cache,               sizeof (struct dep),               "dep",               NULL, NULL);
  alloccache_init (&file_cache,              sizeof (struct file),              "file",              NULL, NULL);
  alloccache_init (&commands_cache,          sizeof (struct commands),          "commands",          NULL, NULL);
  alloccache_init (&nameseq_cache,           sizeof (struct nameseq),           "nameseq",           NULL, NULL);
  alloccache_init (&variable_cache,          sizeof (struct variable),          "variable",          NULL, NULL);
  alloccache_init (&variable_set_cache,      sizeof (struct variable_set),      "variable_set",      NULL, NULL);
  alloccache_init (&variable_set_list_cache, sizeof (struct variable_set_list), "variable_set_list", NULL, NULL);
}
#endif /* CONFIG_WITH_ALLOC_CACHES */

static void
initialize_global_hash_tables (void)
{
  init_hash_global_variable_set ();
  strcache_init ();
  init_hash_files ();
  hash_init_directories ();
  hash_init_function_table ();
}

static const char *
expand_command_line_file (char *name)
{
  const char *cp;
  char *expanded = 0;

  if (name[0] == '\0')
    fatal (NILF, _("empty string invalid as file name"));

  if (name[0] == '~')
    {
      expanded = tilde_expand (name);
      if (expanded != 0)
	name = expanded;
    }

  /* This is also done in parse_file_seq, so this is redundant
     for names read from makefiles.  It is here for names passed
     on the command line.  */
  while (name[0] == '.' && name[1] == '/' && name[2] != '\0')
    {
      name += 2;
      while (*name == '/')
	/* Skip following slashes: ".//foo" is "foo", not "/foo".  */
	++name;
    }

  if (*name == '\0')
    {
      /* It was all slashes!  Move back to the dot and truncate
	 it after the first slash, so it becomes just "./".  */
      do
	--name;
      while (name[0] != '.');
      name[2] = '\0';
    }

  cp = strcache_add (name);

  if (expanded)
    free (expanded);

  return cp;
}

/* Toggle -d on receipt of SIGUSR1.  */

#ifdef SIGUSR1
static RETSIGTYPE
debug_signal_handler (int sig UNUSED)
{
  db_level = db_level ? DB_NONE : DB_BASIC;
}
#endif

static void
decode_debug_flags (void)
{
  const char **pp;

  if (debug_flag)
    db_level = DB_ALL;

  if (!db_flags)
    return;

  for (pp=db_flags->list; *pp; ++pp)
    {
      const char *p = *pp;

      while (1)
        {
          switch (tolower (p[0]))
            {
            case 'a':
              db_level |= DB_ALL;
              break;
            case 'b':
              db_level |= DB_BASIC;
              break;
            case 'i':
              db_level |= DB_BASIC | DB_IMPLICIT;
              break;
            case 'j':
              db_level |= DB_JOBS;
              break;
            case 'm':
              db_level |= DB_BASIC | DB_MAKEFILES;
              break;
            case 'v':
              db_level |= DB_BASIC | DB_VERBOSE;
              break;
#ifdef DB_KMK
            case 'k':
              db_level |= DB_KMK;
              break;
#endif /* DB_KMK */
            default:
              fatal (NILF, _("unknown debug level specification `%s'"), p);
            }

          while (*(++p) != '\0')
            if (*p == ',' || *p == ' ')
              break;

          if (*p == '\0')
            break;

          ++p;
        }
    }
}


#ifdef KMK
static void
set_make_priority_and_affinity (void)
{
# ifdef WINDOWS32
  DWORD dwPriority;
  if (process_affinity)
    if (!SetProcessAffinityMask (GetCurrentProcess (), process_affinity))
      fprintf (stderr, "warning: SetPriorityClass (,%#x) failed with last error %d\n",
               process_affinity, GetLastError());

  switch (process_priority)
    {
      case 0:     return;
      case 1:     dwPriority = IDLE_PRIORITY_CLASS; break;
      case 2:     dwPriority = BELOW_NORMAL_PRIORITY_CLASS; break;
      case 3:     dwPriority = NORMAL_PRIORITY_CLASS; break;
      case 4:     dwPriority = HIGH_PRIORITY_CLASS; break;
      case 5:     dwPriority = REALTIME_PRIORITY_CLASS; break;
      default:    fatal(NILF, _("invalid priority %d\n"), process_priority);
    }
  if (!SetPriorityClass (GetCurrentProcess (), dwPriority))
    fprintf (stderr, "warning: SetPriorityClass (,%#x) failed with last error %d\n",
             dwPriority, GetLastError ());

# else /*#elif HAVE_NICE */
  int nice_level = 0;
  switch (process_priority)
    {
      case 0:     return;
      case 1:     nice_level = 19; break;
      case 2:     nice_level = 10; break;
      case 3:     nice_level = 0; break;
      case 4:     nice_level = -10; break;
      case 5:     nice_level = -19; break;
      default:    fatal(NILF, _("invalid priority %d\n"), process_priority);
    }
  errno = 0;
  if (nice (nice_level) == -1 && errno != 0)
    fprintf (stderr, "warning: nice (%d) failed: %s\n",
             nice_level, strerror (errno));
# endif
}
#endif /* KMK */


#ifdef WINDOWS32
/*
 * HANDLE runtime exceptions by avoiding a requestor on the GUI. Capture
 * exception and print it to stderr instead.
 *
 * If ! DB_VERBOSE, just print a simple message and exit.
 * If DB_VERBOSE, print a more verbose message.
 * If compiled for DEBUG, let exception pass through to GUI so that
 *   debuggers can attach.
 */
LONG WINAPI
handle_runtime_exceptions( struct _EXCEPTION_POINTERS *exinfo )
{
  PEXCEPTION_RECORD exrec = exinfo->ExceptionRecord;
  LPSTR cmdline = GetCommandLine();
  LPSTR prg = strtok(cmdline, " ");
  CHAR errmsg[1024];
#ifdef USE_EVENT_LOG
  HANDLE hEventSource;
  LPTSTR lpszStrings[1];
#endif

  if (! ISDB (DB_VERBOSE))
    {
      sprintf(errmsg,
              _("%s: Interrupt/Exception caught (code = 0x%lx, addr = 0x%lx)\n"),
              prg, exrec->ExceptionCode, (DWORD)exrec->ExceptionAddress);
      fprintf(stderr, errmsg);
      exit(255);
    }

  sprintf(errmsg,
          _("\nUnhandled exception filter called from program %s\nExceptionCode = %lx\nExceptionFlags = %lx\nExceptionAddress = %lx\n"),
          prg, exrec->ExceptionCode, exrec->ExceptionFlags,
          (DWORD)exrec->ExceptionAddress);

  if (exrec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
      && exrec->NumberParameters >= 2)
    sprintf(&errmsg[strlen(errmsg)],
            (exrec->ExceptionInformation[0]
             ? _("Access violation: write operation at address %lx\n")
             : _("Access violation: read operation at address %lx\n")),
            exrec->ExceptionInformation[1]);

  /* turn this on if we want to put stuff in the event log too */
#ifdef USE_EVENT_LOG
  hEventSource = RegisterEventSource(NULL, "GNU Make");
  lpszStrings[0] = errmsg;

  if (hEventSource != NULL)
    {
      ReportEvent(hEventSource,         /* handle of event source */
                  EVENTLOG_ERROR_TYPE,  /* event type */
                  0,                    /* event category */
                  0,                    /* event ID */
                  NULL,                 /* current user's SID */
                  1,                    /* strings in lpszStrings */
                  0,                    /* no bytes of raw data */
                  lpszStrings,          /* array of error strings */
                  NULL);                /* no raw data */

      (VOID) DeregisterEventSource(hEventSource);
    }
#endif

  /* Write the error to stderr too */
  fprintf(stderr, errmsg);

#ifdef DEBUG
  return EXCEPTION_CONTINUE_SEARCH;
#else
  exit(255);
  return (255); /* not reached */
#endif
}

/*
 * On WIN32 systems we don't have the luxury of a /bin directory that
 * is mapped globally to every drive mounted to the system. Since make could
 * be invoked from any drive, and we don't want to propogate /bin/sh
 * to every single drive. Allow ourselves a chance to search for
 * a value for default shell here (if the default path does not exist).
 */

int
find_and_set_default_shell (const char *token)
{
  int sh_found = 0;
  char *atoken = 0;
  char *search_token;
  char *tokend;
  PATH_VAR(sh_path);
  extern char *default_shell;

  if (!token)
    search_token = default_shell;
  else
    atoken = search_token = xstrdup (token);

  /* If the user explicitly requests the DOS cmd shell, obey that request.
     However, make sure that's what they really want by requiring the value
     of SHELL either equal, or have a final path element of, "cmd" or
     "cmd.exe" case-insensitive.  */
  tokend = search_token + strlen (search_token) - 3;
  if (((tokend == search_token
        || (tokend > search_token
            && (tokend[-1] == '/' || tokend[-1] == '\\')))
       && !strcasecmp (tokend, "cmd"))
      || ((tokend - 4 == search_token
           || (tokend - 4 > search_token
               && (tokend[-5] == '/' || tokend[-5] == '\\')))
          && !strcasecmp (tokend - 4, "cmd.exe"))) {
    batch_mode_shell = 1;
    unixy_shell = 0;
    sprintf (sh_path, "%s", search_token);
    default_shell = xstrdup (w32ify (sh_path, 0));
    DB (DB_VERBOSE,
        (_("find_and_set_shell setting default_shell = %s\n"), default_shell));
    sh_found = 1;
  } else if (!no_default_sh_exe &&
             (token == NULL || !strcmp (search_token, default_shell))) {
    /* no new information, path already set or known */
    sh_found = 1;
  } else if (file_exists_p (search_token)) {
    /* search token path was found */
    sprintf (sh_path, "%s", search_token);
    default_shell = xstrdup (w32ify (sh_path, 0));
    DB (DB_VERBOSE,
        (_("find_and_set_shell setting default_shell = %s\n"), default_shell));
    sh_found = 1;
  } else {
    char *p;
    struct variable *v = lookup_variable (STRING_SIZE_TUPLE ("PATH"));

    /* Search Path for shell */
    if (v && v->value) {
      char *ep;

      p  = v->value;
      ep = strchr (p, PATH_SEPARATOR_CHAR);

      while (ep && *ep) {
        *ep = '\0';

        if (dir_file_exists_p (p, search_token)) {
          sprintf (sh_path, "%s/%s", p, search_token);
          default_shell = xstrdup (w32ify (sh_path, 0));
          sh_found = 1;
          *ep = PATH_SEPARATOR_CHAR;

          /* terminate loop */
          p += strlen (p);
        } else {
          *ep = PATH_SEPARATOR_CHAR;
           p = ++ep;
        }

        ep = strchr (p, PATH_SEPARATOR_CHAR);
      }

      /* be sure to check last element of Path */
      if (p && *p && dir_file_exists_p (p, search_token)) {
          sprintf (sh_path, "%s/%s", p, search_token);
          default_shell = xstrdup (w32ify (sh_path, 0));
          sh_found = 1;
      }

      if (sh_found)
        DB (DB_VERBOSE,
            (_("find_and_set_shell path search set default_shell = %s\n"),
             default_shell));
    }
  }

#if 0/* def KMK - has been fixed in sub_proc.c */
  /* WORKAROUND:
    With GNU Make 3.81, this kludge was necessary to get double quotes
    working correctly again (worked fine with the 3.81beta1 code).
    beta1 was forcing batch_mode_shell I think, so let's enforce that
    for the kBuild shell. */
  if (sh_found && strstr(default_shell, "kmk_ash")) {
      unixy_shell = 1;
      batch_mode_shell = 1;
  } else
#endif
  /* naive test */
  if (!unixy_shell && sh_found &&
      (strstr (default_shell, "sh") || strstr (default_shell, "SH"))) {
    unixy_shell = 1;
    batch_mode_shell = 0;
  }

#ifdef BATCH_MODE_ONLY_SHELL
  batch_mode_shell = 1;
#endif

  if (atoken)
    free (atoken);

  return (sh_found);
}

/* bird: */
#ifdef CONFIG_NEW_WIN32_CTRL_EVENT
#include <process.h>
static UINT g_tidMainThread = 0;
static int volatile g_sigPending = 0; /* lazy bird */
# ifndef _M_IX86
static LONG volatile g_lTriggered = 0;
static CONTEXT g_Ctx;
# endif

# ifdef _M_IX86
static __declspec(naked) void dispatch_stub(void)
{
    __asm  {
        pushfd
        pushad
        cld
    }
    fflush(stdout);
    /*fprintf(stderr, "dbg: raising %s on the main thread (%d)\n", g_sigPending == SIGINT ? "SIGINT" : "SIGBREAK", _getpid());*/
    raise(g_sigPending);
    __asm  {
        popad
        popfd
        ret
    }
}
# else /* !_M_IX86 */
static void dispatch_stub(void)
{
    fflush(stdout);
    /*fprintf(stderr, "dbg: raising %s on the main thread (%d)\n", g_sigPending == SIGINT ? "SIGINT" : "SIGBREAK", _getpid());*/
    raise(g_sigPending);

    SetThreadContext(GetCurrentThread(), &g_Ctx);
    fprintf(stderr, "fatal error: SetThreadContext failed with last error %d\n", GetLastError());
    for (;;)
        exit(131);
}
# endif /* !_M_IX86 */

static BOOL WINAPI ctrl_event(DWORD CtrlType)
{
    int sig = (CtrlType == CTRL_C_EVENT) ? SIGINT : SIGBREAK;
    HANDLE hThread;
    CONTEXT Ctx;

#ifndef _M_IX86
    /* only once. */
    if (InterlockedExchange(&g_lTriggered, 1))
    {
        Sleep(1);
        return TRUE;
    }
#endif

    /* open the main thread and suspend it. */
    hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, g_tidMainThread);
    SuspendThread(hThread);

    /* Get the thread context and if we've get a valid Esp, dispatch
       it on the main thread otherwise raise the signal in the
       ctrl-event thread (this). */
    memset(&Ctx, 0, sizeof(Ctx));
    Ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(hThread, &Ctx)
#ifdef _M_IX86
        && Ctx.Esp >= 0x1000
#else
        && Ctx.Rsp >= 0x1000
#endif
       )
    {
#ifdef _M_IX86
        ((uintptr_t *)Ctx.Esp)[-1] = Ctx.Eip;
        Ctx.Esp -= sizeof(uintptr_t);
        Ctx.Eip = (uintptr_t)&dispatch_stub;
#else
        g_Ctx = Ctx;
        Ctx.Rsp -= 0x20;
        Ctx.Rsp &= ~(uintptr_t)0xf;
        Ctx.Rip = (uintptr_t)&dispatch_stub;
#endif

        SetThreadContext(hThread, &Ctx);
        g_sigPending = sig;
        ResumeThread(hThread);
        CloseHandle(hThread);
    }
    else
    {
        fprintf(stderr, "dbg: raising %s on the ctrl-event thread (%d)\n", sig == SIGINT ? "SIGINT" : "SIGBREAK", _getpid());
        raise(sig);
        ResumeThread(hThread);
        CloseHandle(hThread);
        exit(130);
    }

    Sleep(1);
    return TRUE;
}
#endif /* CONFIG_NEW_WIN32_CTRL_EVENT */

#endif  /* WINDOWS32 */

#ifdef KMK
/* Determins the number of CPUs that are currently online.
   This is used to setup the default number of job slots. */
static int
get_online_cpu_count(void)
{
# ifdef WINDOWS32
    /* Windows: Count the active CPUs. */
    int cpus, i;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    for (i = cpus = 0; i < sizeof(si.dwActiveProcessorMask) * 8; i++)
      {
        if (si.dwActiveProcessorMask & 1)
          cpus++;
        si.dwActiveProcessorMask >>= 1;
      }
    return cpus ? cpus : 1;

# elif defined(__OS2__)
    /* OS/2: Count the active CPUs. */
    int cpus, i, j;
    MPAFFINITY mp;
    if (DosQueryThreadAffinity(AFNTY_SYSTEM, &mp))
      return 1;
    for (j = cpus = 0; j < sizeof(mp.mask) / sizeof(mp.mask[0]); j++)
      for (i = 0; i < 32; i++)
        if (mp.mask[j] & (1UL << i))
          cpus++;
    return cpus ? cpus : 1;

# else
  /* UNIX like systems, try sysconf and sysctl. */
  int cpus = -1;
#  if defined(CTL_HW)
  int mib[2];
  size_t sz;
#  endif

#  ifdef _SC_NPROCESSORS_ONLN
  cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (cpus >= 1)
      return cpus;
  cpus = -1;
#  endif

#  if defined(CTL_HW)
#   ifdef HW_AVAILCPU
  sz = sizeof(cpus);
  mib[0] = CTL_HW;
  mib[1] = HW_AVAILCPU;
  if (!sysctl(mib, 2, &cpus, &sz, NULL, 0)
      && cpus >= 1)
    return cpus;
  cpus = -1;
#   endif /* HW_AVAILCPU */

  sz = sizeof(cpus);
  mib[0] = CTL_HW;
  mib[1] = HW_NCPU;
  if (!sysctl(mib, 2, &cpus, &sz, NULL, 0)
      && cpus >= 1)
    return cpus;
  cpus = -1;
#  endif /* CTL_HW */

  /* no idea / failure, just return 1. */
  return 1;
# endif
}
#endif /* KMK */

#ifdef __MSDOS__
static void
msdos_return_to_initial_directory (void)
{
  if (directory_before_chdir)
    chdir (directory_before_chdir);
}
#endif  /* __MSDOS__ */

#ifndef _MSC_VER /* bird */
char *mktemp (char *template);
#endif
int mkstemp (char *template);

FILE *
open_tmpfile(char **name, const char *template)
{
#ifdef HAVE_FDOPEN
  int fd;
#endif

#if defined HAVE_MKSTEMP || defined HAVE_MKTEMP
# define TEMPLATE_LEN   strlen (template)
#else
# define TEMPLATE_LEN   L_tmpnam
#endif
  *name = xmalloc (TEMPLATE_LEN + 1);
  strcpy (*name, template);

#if defined HAVE_MKSTEMP && defined HAVE_FDOPEN
  /* It's safest to use mkstemp(), if we can.  */
  fd = mkstemp (*name);
  if (fd == -1)
    return 0;
  return fdopen (fd, "w");
#else
# ifdef HAVE_MKTEMP
  (void) mktemp (*name);
# else
  (void) tmpnam (*name);
# endif

# ifdef HAVE_FDOPEN
  /* Can't use mkstemp(), but guard against a race condition.  */
  fd = open (*name, O_CREAT|O_EXCL|O_WRONLY, 0600);
  if (fd == -1)
    return 0;
  return fdopen (fd, "w");
# else
  /* Not secure, but what can we do?  */
  return fopen (*name, "w");
# endif
#endif
}

#if defined(set_space_map_entry) /*bird*/
char space_map[space_map_size];
#endif /* set_space_map_entry */


#ifdef _AMIGA
int
main (int argc, char **argv)
#else
int
main (int argc, char **argv, char **envp)
#endif
{
  static char *stdin_nm = 0;
  int makefile_status = MAKE_SUCCESS;
  struct dep *read_makefiles;
  PATH_VAR (current_directory);
  unsigned int restarts = 0;
#ifdef WINDOWS32
  char *unix_path = NULL;
  char *windows32_path = NULL;

#ifndef ELECTRIC_HEAP /* Drop this because it prevents JIT debugging. */
  SetUnhandledExceptionFilter(handle_runtime_exceptions);
#endif /* !ELECTRIC_HEAP */

  /* start off assuming we have no shell */
  unixy_shell = 0;
  no_default_sh_exe = 1;
#endif
# if defined(set_space_map_entry) /* bird */
  memset (space_map, '\0', sizeof(space_map));
  set_space_map_entry (' ');
  set_space_map_entry ('\f');
  set_space_map_entry ('\n');
  set_space_map_entry ('\r');
  set_space_map_entry ('\t');
  set_space_map_entry ('\v');
# endif

#ifdef CONFIG_WITH_PRINT_TIME_SWITCH
  make_start_ts = nano_timestamp ();
#endif

#ifdef SET_STACK_SIZE
 /* Get rid of any avoidable limit on stack size.  */
  {
    struct rlimit rlim;

    /* Set the stack limit huge so that alloca does not fail.  */
    if (getrlimit (RLIMIT_STACK, &rlim) == 0)
      {
        rlim.rlim_cur = rlim.rlim_max;
        setrlimit (RLIMIT_STACK, &rlim);
      }
  }
#endif

#ifdef HAVE_ATEXIT
  atexit (close_stdout);
#endif

  /* Needed for OS/2 */
  initialize_main(&argc, &argv);

#ifdef KMK
  init_kbuild (argc, argv);
#endif

  default_goal_file = 0;
  reading_file = 0;

#if defined (__MSDOS__) && !defined (_POSIX_SOURCE)
  /* Request the most powerful version of `system', to
     make up for the dumb default shell.  */
  __system_flags = (__system_redirect
		    | __system_use_shell
		    | __system_allow_multiple_cmds
		    | __system_allow_long_cmds
		    | __system_handle_null_commands
		    | __system_emulate_chdir);

#endif

  /* Set up gettext/internationalization support.  */
  setlocale (LC_ALL, "");
#ifdef LOCALEDIR /* bird */
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);
#endif

#ifdef	POSIX
  sigemptyset (&fatal_signal_set);
#define	ADD_SIG(sig)	sigaddset (&fatal_signal_set, sig)
#else
#ifdef	HAVE_SIGSETMASK
  fatal_signal_mask = 0;
#define	ADD_SIG(sig)	fatal_signal_mask |= sigmask (sig)
#else
#define	ADD_SIG(sig)
#endif
#endif

#define	FATAL_SIG(sig)							      \
  if (bsd_signal (sig, fatal_error_signal) == SIG_IGN)			      \
    bsd_signal (sig, SIG_IGN);						      \
  else									      \
    ADD_SIG (sig);

#ifdef SIGHUP
  FATAL_SIG (SIGHUP);
#endif
#ifdef SIGQUIT
  FATAL_SIG (SIGQUIT);
#endif
  FATAL_SIG (SIGINT);
  FATAL_SIG (SIGTERM);

#ifdef __MSDOS__
  /* Windows 9X delivers FP exceptions in child programs to their
     parent!  We don't want Make to die when a child divides by zero,
     so we work around that lossage by catching SIGFPE.  */
  FATAL_SIG (SIGFPE);
#endif

#ifdef	SIGDANGER
  FATAL_SIG (SIGDANGER);
#endif
#ifdef SIGXCPU
  FATAL_SIG (SIGXCPU);
#endif
#ifdef SIGXFSZ
  FATAL_SIG (SIGXFSZ);
#endif

#ifdef CONFIG_NEW_WIN32_CTRL_EVENT
  /* bird: dispatch signals in our own way to try avoid deadlocks. */
  g_tidMainThread = GetCurrentThreadId ();
  SetConsoleCtrlHandler (ctrl_event, TRUE);
#endif /* CONFIG_NEW_WIN32_CTRL_EVENT */

#undef	FATAL_SIG

  /* Do not ignore the child-death signal.  This must be done before
     any children could possibly be created; otherwise, the wait
     functions won't work on systems with the SVR4 ECHILD brain
     damage, if our invoker is ignoring this signal.  */

#ifdef HAVE_WAIT_NOHANG
# if defined SIGCHLD
  (void) bsd_signal (SIGCHLD, SIG_DFL);
# endif
# if defined SIGCLD && SIGCLD != SIGCHLD
  (void) bsd_signal (SIGCLD, SIG_DFL);
# endif
#endif

  /* Make sure stdout is line-buffered.  */

#ifdef HAVE_SETVBUF
# ifdef SETVBUF_REVERSED
  setvbuf (stdout, _IOLBF, xmalloc (BUFSIZ), BUFSIZ);
# else	/* setvbuf not reversed.  */
  /* Some buggy systems lose if we pass 0 instead of allocating ourselves.  */
  setvbuf (stdout, 0, _IOLBF, BUFSIZ);
# endif	/* setvbuf reversed.  */
#elif HAVE_SETLINEBUF
  setlinebuf (stdout);
#endif	/* setlinebuf missing.  */

  /* Figure out where this program lives.  */

  if (argv[0] == 0)
    argv[0] = "";
  if (argv[0][0] == '\0')
#ifdef KMK
    program = "kmk";
#else
    program = "make";
#endif
  else
    {
#ifdef VMS
      program = strrchr (argv[0], ']');
#else
      program = strrchr (argv[0], '/');
#endif
#if defined(__MSDOS__) || defined(__EMX__)
      if (program == 0)
	program = strrchr (argv[0], '\\');
      else
	{
	  /* Some weird environments might pass us argv[0] with
	     both kinds of slashes; we must find the rightmost.  */
	  char *p = strrchr (argv[0], '\\');
	  if (p && p > program)
	    program = p;
	}
      if (program == 0 && argv[0][1] == ':')
	program = argv[0] + 1;
#endif
#ifdef WINDOWS32
      if (program == 0)
        {
          /* Extract program from full path */
          int argv0_len;
          program = strrchr (argv[0], '\\');
          if (program)
            {
              argv0_len = strlen(program);
              if (argv0_len > 4 && streq (&program[argv0_len - 4], ".exe"))
                /* Remove .exe extension */
                program[argv0_len - 4] = '\0';
            }
        }
#endif
      if (program == 0)
	program = argv[0];
      else
	++program;
    }

  /* Set up to access user data (files).  */
  user_access ();

#ifdef CONFIG_WITH_ALLOC_CACHES
  initialize_global_alloc_caches ();
#endif
  initialize_global_hash_tables ();

  /* Figure out where we are.  */

#ifdef WINDOWS32
  if (getcwd_fs (current_directory, GET_PATH_MAX) == 0)
#else
  if (getcwd (current_directory, GET_PATH_MAX) == 0)
#endif
    {
#ifdef	HAVE_GETCWD
      perror_with_name ("getcwd", "");
#else
      error (NILF, "getwd: %s", current_directory);
#endif
      current_directory[0] = '\0';
      directory_before_chdir = 0;
    }
  else
    directory_before_chdir = xstrdup (current_directory);
#ifdef  __MSDOS__
  /* Make sure we will return to the initial directory, come what may.  */
  atexit (msdos_return_to_initial_directory);
#endif

  /* Initialize the special variables.  */
  define_variable (".VARIABLES", 10, "", o_default, 0)->special = 1;
  /* define_variable (".TARGETS", 8, "", o_default, 0)->special = 1; */
  define_variable (".RECIPEPREFIX", 13, "", o_default, 0)->special = 1;

  /* Set up .FEATURES */
  define_variable (".FEATURES", 9,
                   "target-specific order-only second-expansion else-if",
                   o_default, 0);
#ifndef NO_ARCHIVES
  do_variable_definition (NILF, ".FEATURES", "archives",
                          o_default, f_append, 0);
#endif
#ifdef MAKE_JOBSERVER
  do_variable_definition (NILF, ".FEATURES", "jobserver",
                          o_default, f_append, 0);
#endif
#ifdef MAKE_SYMLINKS
  do_variable_definition (NILF, ".FEATURES", "check-symlink",
                          o_default, f_append, 0);
#endif
#ifdef CONFIG_WITH_EXPLICIT_MULTITARGET
  do_variable_definition (NILF, ".FEATURES", "explicit-multitarget",
                          o_default, f_append, 0);
#endif
#ifdef CONFIG_WITH_PREPEND_ASSIGNMENT
  do_variable_definition (NILF, ".FEATURES", "prepend-assignment",
                          o_default, f_append, 0);
#endif

#ifdef KMK
  /* Initialize the default number of jobs to the cpu/core/smt count. */
  default_job_slots = job_slots = get_online_cpu_count ();
#endif /* KMK */

  /* Read in variables from the environment.  It is important that this be
     done before $(MAKE) is figured out so its definitions will not be
     from the environment.  */

#ifndef _AMIGA
  {
    unsigned int i;

    for (i = 0; envp[i] != 0; ++i)
      {
        int do_not_define = 0;
        char *ep = envp[i];

        while (*ep != '\0' && *ep != '=')
          ++ep;
#ifdef WINDOWS32
        if (!unix_path && strneq(envp[i], "PATH=", 5))
          unix_path = ep+1;
        else if (!strnicmp(envp[i], "Path=", 5)) {
          do_not_define = 1; /* it gets defined after loop exits */
          if (!windows32_path)
            windows32_path = ep+1;
        }
#endif
        /* The result of pointer arithmetic is cast to unsigned int for
           machines where ptrdiff_t is a different size that doesn't widen
           the same.  */
        if (!do_not_define)
          {
            struct variable *v;

            v = define_variable (envp[i], (unsigned int) (ep - envp[i]),
                                 ep + 1, o_env, 1);
            /* Force exportation of every variable culled from the
               environment.  We used to rely on target_environment's
               v_default code to do this.  But that does not work for the
               case where an environment variable is redefined in a makefile
               with `override'; it should then still be exported, because it
               was originally in the environment.  */
            v->export = v_export;

            /* Another wrinkle is that POSIX says the value of SHELL set in
               the makefile won't change the value of SHELL given to
               subprocesses.  */
            if (streq (v->name, "SHELL"))
              {
#ifndef __MSDOS__
                v->export = v_noexport;
#endif
#ifndef CONFIG_WITH_STRCACHE2
                shell_var.name = "SHELL";
#else
                shell_var.name = v->name;
#endif
                shell_var.length = 5; /* bird - gotta set the length too! */
#ifndef CONFIG_WITH_VALUE_LENGTH
                shell_var.value = xstrdup (ep + 1);
#else
                shell_var.value = savestring (v->value, v->value_length);
                shell_var.value_length = v->value_length;
#endif

              }

            /* If MAKE_RESTARTS is set, remember it but don't export it.  */
            if (streq (v->name, "MAKE_RESTARTS"))
              {
                v->export = v_noexport;
                restarts = (unsigned int) atoi (ep + 1);
              }
          }
      }
  }
#ifdef WINDOWS32
    /* If we didn't find a correctly spelled PATH we define PATH as
     * either the first mispelled value or an empty string
     */
    if (!unix_path)
      define_variable("PATH", 4,
                      windows32_path ? windows32_path : "",
                      o_env, 1)->export = v_export;
#endif
#else /* For Amiga, read the ENV: device, ignoring all dirs */
    {
	BPTR env, file, old;
	char buffer[1024];
	int len;
	__aligned struct FileInfoBlock fib;

	env = Lock ("ENV:", ACCESS_READ);
	if (env)
	{
	    old = CurrentDir (DupLock(env));
	    Examine (env, &fib);

	    while (ExNext (env, &fib))
	    {
		if (fib.fib_DirEntryType < 0) /* File */
		{
		    /* Define an empty variable. It will be filled in
			variable_lookup(). Makes startup quite a bit
			faster. */
			define_variable (fib.fib_FileName,
			    strlen (fib.fib_FileName),
			"", o_env, 1)->export = v_export;
		}
	    }
	    UnLock (env);
	    UnLock(CurrentDir(old));
	}
    }
#endif

  /* Decode the switches.  */

#ifdef KMK
  decode_env_switches (STRING_SIZE_TUPLE ("KMK_FLAGS"));
#else /* !KMK */
  decode_env_switches (STRING_SIZE_TUPLE ("MAKEFLAGS"));
#if 0
  /* People write things like:
        MFLAGS="CC=gcc -pipe" "CFLAGS=-g"
     and we set the -p, -i and -e switches.  Doesn't seem quite right.  */
  decode_env_switches (STRING_SIZE_TUPLE ("MFLAGS"));
#endif
#endif /* !KMK */
  decode_switches (argc, argv, 0);
#ifdef WINDOWS32
  if (suspend_flag) {
        fprintf(stderr, "%s (pid = %ld)\n", argv[0], GetCurrentProcessId());
        fprintf(stderr, _("%s is suspending for 30 seconds..."), argv[0]);
        Sleep(30 * 1000);
        fprintf(stderr, _("done sleep(30). Continuing.\n"));
  }
#endif

  decode_debug_flags ();

#ifdef KMK
  set_make_priority_and_affinity ();
#endif

  /* Set always_make_flag if -B was given and we've not restarted already.  */
  always_make_flag = always_make_set && (restarts == 0);

  /* Print version information.  */
  if (print_version_flag || print_data_base_flag || db_level)
    {
      print_version ();

      /* `make --version' is supposed to just print the version and exit.  */
      if (print_version_flag)
        die (0);
    }

#ifndef VMS
  /* Set the "MAKE_COMMAND" variable to the name we were invoked with.
     (If it is a relative pathname with a slash, prepend our directory name
     so the result will run the same program regardless of the current dir.
     If it is a name with no slash, we can only hope that PATH did not
     find it in the current directory.)  */
#ifdef WINDOWS32
  /*
   * Convert from backslashes to forward slashes for
   * programs like sh which don't like them. Shouldn't
   * matter if the path is one way or the other for
   * CreateProcess().
   */
  if (strpbrk(argv[0], "/:\\") ||
      strstr(argv[0], "..") ||
      strneq(argv[0], "//", 2))
    argv[0] = xstrdup(w32ify(argv[0],1));
#else /* WINDOWS32 */
#if defined (__MSDOS__) || defined (__EMX__)
  if (strchr (argv[0], '\\'))
    {
      char *p;

      argv[0] = xstrdup (argv[0]);
      for (p = argv[0]; *p; p++)
	if (*p == '\\')
	  *p = '/';
    }
  /* If argv[0] is not in absolute form, prepend the current
     directory.  This can happen when Make is invoked by another DJGPP
     program that uses a non-absolute name.  */
  if (current_directory[0] != '\0'
      && argv[0] != 0
      && (argv[0][0] != '/' && (argv[0][0] == '\0' || argv[0][1] != ':'))
# ifdef __EMX__
      /* do not prepend cwd if argv[0] contains no '/', e.g. "make" */
      && (strchr (argv[0], '/') != 0 || strchr (argv[0], '\\') != 0)
# endif
      )
    argv[0] = xstrdup (concat (current_directory, "/", argv[0]));
#else  /* !__MSDOS__ */
  if (current_directory[0] != '\0'
      && argv[0] != 0 && argv[0][0] != '/' && strchr (argv[0], '/') != 0
#ifdef HAVE_DOS_PATHS
      && (argv[0][0] != '\\' && (!argv[0][0] || argv[0][1] != ':'))
      && strchr (argv[0], '\\') != 0
#endif
      )
    argv[0] = xstrdup (concat (current_directory, "/", argv[0]));
#endif /* !__MSDOS__ */
#endif /* WINDOWS32 */
#endif

  /* The extra indirection through $(MAKE_COMMAND) is done
     for hysterical raisins.  */
  (void) define_variable ("MAKE_COMMAND", 12, argv[0], o_default, 0);
  (void) define_variable ("MAKE", 4, "$(MAKE_COMMAND)", o_default, 1);
#ifdef KMK
  (void) define_variable ("KMK", 3, argv[0], o_default, 1);
#endif

  if (command_variables != 0)
    {
      struct command_variable *cv;
      struct variable *v;
      unsigned int len = 0;
      char *value, *p;

      /* Figure out how much space will be taken up by the command-line
	 variable definitions.  */
      for (cv = command_variables; cv != 0; cv = cv->next)
	{
	  v = cv->variable;
	  len += 2 * strlen (v->name);
	  if (! v->recursive)
	    ++len;
	  ++len;
	  len += 2 * strlen (v->value);
	  ++len;
	}

      /* Now allocate a buffer big enough and fill it.  */
      p = value = alloca (len);
      for (cv = command_variables; cv != 0; cv = cv->next)
	{
	  v = cv->variable;
	  p = quote_for_env (p, v->name);
	  if (! v->recursive)
	    *p++ = ':';
	  *p++ = '=';
	  p = quote_for_env (p, v->value);
	  *p++ = ' ';
	}
      p[-1] = '\0';		/* Kill the final space and terminate.  */

      /* Define an unchangeable variable with a name that no POSIX.2
	 makefile could validly use for its own variable.  */
      (void) define_variable ("-*-command-variables-*-", 23,
			      value, o_automatic, 0);

      /* Define the variable; this will not override any user definition.
         Normally a reference to this variable is written into the value of
         MAKEFLAGS, allowing the user to override this value to affect the
         exported value of MAKEFLAGS.  In POSIX-pedantic mode, we cannot
         allow the user's setting of MAKEOVERRIDES to affect MAKEFLAGS, so
         a reference to this hidden variable is written instead. */
#ifdef KMK
      (void) define_variable ("KMK_OVERRIDES", 13,
			      "${-*-command-variables-*-}", o_env, 1);
#else
      (void) define_variable ("MAKEOVERRIDES", 13,
			      "${-*-command-variables-*-}", o_env, 1);
#endif
    }

  /* If there were -C flags, move ourselves about.  */
  if (directories != 0)
    {
      unsigned int i;
      for (i = 0; directories->list[i] != 0; ++i)
        {
          const char *dir = directories->list[i];
#ifdef WINDOWS32
          /* WINDOWS32 chdir() doesn't work if the directory has a trailing '/'
             But allow -C/ just in case someone wants that.  */
          {
            char *p = (char *)dir + strlen (dir) - 1;
            while (p > dir && (p[0] == '/' || p[0] == '\\'))
              --p;
            p[1] = '\0';
          }
#endif
          if (chdir (dir) < 0)
            pfatal_with_name (dir);
        }
    }

#ifdef KMK
  /* Check for [Mm]akefile.kup and change directory when found.
     Makefile.kmk overrides Makefile.kup but not plain Makefile.
     If no -C arguments were given, fake one to indicate chdir. */
  if (makefiles == 0)
    {
      struct stat st;
      if ((   (   stat ("Makefile.kup", &st) == 0
               && S_ISREG (st.st_mode) )
           || (   stat ("makefile.kup", &st) == 0
               && S_ISREG (st.st_mode) ) )
       && stat ("Makefile.kmk", &st) < 0
       && stat ("makefile.kmk", &st) < 0)
        {
          static char fake_path[3*16 + 32] = "..";
          char *cur = &fake_path[2];
          int   up_levels = 1;
          while (up_levels < 16)
            {
              /* File with higher precedence.s */
              strcpy (cur, "/Makefile.kmk");
              if (stat (fake_path, &st) == 0)
                break;
              strcpy (cur, "/makefile.kmk");
              if (stat (fake_path, &st) == 0)
                break;

              /* the .kup files */
              strcpy (cur, "/Makefile.kup");
              if (   stat (fake_path, &st) != 0
                  || !S_ISREG (st.st_mode))
                {
                  strcpy (cur, "/makefile.kup");
                  if (   stat (fake_path, &st) != 0
                      || !S_ISREG (st.st_mode))
                    break;
                }

              /* ok */
              strcpy (cur, "/..");
              cur += 3;
              up_levels++;
            }

          if (up_levels >= 16)
            fatal (NILF, _("Makefile.kup recursion is too deep."));

          /* attempt to change to the directory. */
          *cur = '\0';
          if (chdir (fake_path) < 0)
            pfatal_with_name (fake_path);

          /* add the string to the directories. */
          if (!directories)
            {
              directories = xmalloc (sizeof(*directories));
              directories->list = xmalloc (5 * sizeof (char *));
              directories->max = 5;
              directories->idx = 0;
            }
          else if (directories->idx == directories->max - 1)
            {
              directories->max += 5;
              directories->list = xrealloc ((void *)directories->list,
                                   directories->max * sizeof (char *));
            }
          directories->list[directories->idx++] = fake_path;
        }
    }
#endif /* KMK */

#ifdef WINDOWS32
  /*
   * THIS BLOCK OF CODE MUST COME AFTER chdir() CALL ABOVE IN ORDER
   * TO NOT CONFUSE THE DEPENDENCY CHECKING CODE IN implicit.c.
   *
   * The functions in dir.c can incorrectly cache information for "."
   * before we have changed directory and this can cause file
   * lookups to fail because the current directory (.) was pointing
   * at the wrong place when it was first evaluated.
   */
#ifdef KMK /* this is really a candidate for all platforms... */
  {
    extern char *default_shell;
    const char *bin = get_kbuild_bin_path();
    size_t len = strlen (bin);
    default_shell = xmalloc (len + sizeof("/kmk_ash.exe"));
    memcpy (default_shell, bin, len);
    strcpy (default_shell + len, "/kmk_ash.exe");
    no_default_sh_exe = 0;
    batch_mode_shell = 1;
  }
#else /* !KMK */
   no_default_sh_exe = !find_and_set_default_shell(NULL);
#endif /* !KMK */
#endif /* WINDOWS32 */
  /* Figure out the level of recursion.  */
  {
    struct variable *v = lookup_variable (STRING_SIZE_TUPLE (MAKELEVEL_NAME));
    if (v != 0 && v->value[0] != '\0' && v->value[0] != '-')
      makelevel = (unsigned int) atoi (v->value);
    else
      makelevel = 0;
  }

  /* Except under -s, always do -w in sub-makes and under -C.  */
  if (!silent_flag && (directories != 0 || makelevel > 0))
    print_directory_flag = 1;

  /* Let the user disable that with --no-print-directory.  */
  if (inhibit_print_directory_flag)
    print_directory_flag = 0;

  /* If -R was given, set -r too (doesn't make sense otherwise!)  */
  if (no_builtin_variables_flag)
    no_builtin_rules_flag = 1;

  /* Construct the list of include directories to search.  */

  construct_include_path (include_directories == 0
                          ? 0 : include_directories->list);

  /* Figure out where we are now, after chdir'ing.  */
  if (directories == 0)
    /* We didn't move, so we're still in the same place.  */
    starting_directory = current_directory;
  else
    {
#ifdef WINDOWS32
      if (getcwd_fs (current_directory, GET_PATH_MAX) == 0)
#else
      if (getcwd (current_directory, GET_PATH_MAX) == 0)
#endif
	{
#ifdef	HAVE_GETCWD
	  perror_with_name ("getcwd", "");
#else
	  error (NILF, "getwd: %s", current_directory);
#endif
	  starting_directory = 0;
	}
      else
	starting_directory = current_directory;
    }

  (void) define_variable ("CURDIR", 6, current_directory, o_file, 0);

  /* Read any stdin makefiles into temporary files.  */

  if (makefiles != 0)
    {
      unsigned int i;
      for (i = 0; i < makefiles->idx; ++i)
	if (makefiles->list[i][0] == '-' && makefiles->list[i][1] == '\0')
	  {
	    /* This makefile is standard input.  Since we may re-exec
	       and thus re-read the makefiles, we read standard input
	       into a temporary file and read from that.  */
	    FILE *outfile;
            char *template, *tmpdir;

            if (stdin_nm)
              fatal (NILF, _("Makefile from standard input specified twice."));

#ifdef VMS
# define DEFAULT_TMPDIR     "sys$scratch:"
#else
# ifdef P_tmpdir
#  define DEFAULT_TMPDIR    P_tmpdir
# else
#  define DEFAULT_TMPDIR    "/tmp"
# endif
#endif
#define DEFAULT_TMPFILE     "GmXXXXXX"

	    if (((tmpdir = getenv ("TMPDIR")) == NULL || *tmpdir == '\0')
#if defined (__MSDOS__) || defined (WINDOWS32) || defined (__EMX__)
                /* These are also used commonly on these platforms.  */
                && ((tmpdir = getenv ("TEMP")) == NULL || *tmpdir == '\0')
                && ((tmpdir = getenv ("TMP")) == NULL || *tmpdir == '\0')
#endif
               )
	      tmpdir = DEFAULT_TMPDIR;

            template = alloca (strlen (tmpdir) + sizeof (DEFAULT_TMPFILE) + 1);
	    strcpy (template, tmpdir);

#ifdef HAVE_DOS_PATHS
	    if (strchr ("/\\", template[strlen (template) - 1]) == NULL)
	      strcat (template, "/");
#else
# ifndef VMS
	    if (template[strlen (template) - 1] != '/')
	      strcat (template, "/");
# endif /* !VMS */
#endif /* !HAVE_DOS_PATHS */

	    strcat (template, DEFAULT_TMPFILE);
	    outfile = open_tmpfile (&stdin_nm, template);
	    if (outfile == 0)
	      pfatal_with_name (_("fopen (temporary file)"));
	    while (!feof (stdin) && ! ferror (stdin))
	      {
		char buf[2048];
		unsigned int n = fread (buf, 1, sizeof (buf), stdin);
		if (n > 0 && fwrite (buf, 1, n, outfile) != n)
		  pfatal_with_name (_("fwrite (temporary file)"));
	      }
	    fclose (outfile);

	    /* Replace the name that read_all_makefiles will
	       see with the name of the temporary file.  */
            makefiles->list[i] = strcache_add (stdin_nm);

	    /* Make sure the temporary file will not be remade.  */
            {
              struct file *f = enter_file (strcache_add (stdin_nm));
              f->updated = 1;
              f->update_status = 0;
              f->command_state = cs_finished;
              /* Can't be intermediate, or it'll be removed too early for
                 make re-exec.  */
              f->intermediate = 0;
              f->dontcare = 0;
            }
	  }
    }

#if !defined(__EMX__) || defined(__KLIBC__) /* Don't use a SIGCHLD handler for good old EMX (bird) */
#if defined(MAKE_JOBSERVER) || !defined(HAVE_WAIT_NOHANG)
  /* Set up to handle children dying.  This must be done before
     reading in the makefiles so that `shell' function calls will work.

     If we don't have a hanging wait we have to fall back to old, broken
     functionality here and rely on the signal handler and counting
     children.

     If we're using the jobs pipe we need a signal handler so that
     SIGCHLD is not ignored; we need it to interrupt the read(2) of the
     jobserver pipe in job.c if we're waiting for a token.

     If none of these are true, we don't need a signal handler at all.  */
  {
    RETSIGTYPE child_handler (int sig);
# if defined SIGCHLD
    bsd_signal (SIGCHLD, child_handler);
# endif
# if defined SIGCLD && SIGCLD != SIGCHLD
    bsd_signal (SIGCLD, child_handler);
# endif
  }
#endif
#endif

  /* Let the user send us SIGUSR1 to toggle the -d flag during the run.  */
#ifdef SIGUSR1
  bsd_signal (SIGUSR1, debug_signal_handler);
#endif

  /* Define the initial list of suffixes for old-style rules.  */

  set_default_suffixes ();

  /* Define the file rules for the built-in suffix rules.  These will later
     be converted into pattern rules.  We used to do this in
     install_default_implicit_rules, but since that happens after reading
     makefiles, it results in the built-in pattern rules taking precedence
     over makefile-specified suffix rules, which is wrong.  */

  install_default_suffix_rules ();

  /* Define some internal and special variables.  */

  define_automatic_variables ();

  /* Set up the MAKEFLAGS and MFLAGS variables
     so makefiles can look at them.  */

  define_makeflags (0, 0);

  /* Define the default variables.  */
  define_default_variables ();

  default_file = enter_file (strcache_add (".DEFAULT"));

  {
    struct variable *v = define_variable (".DEFAULT_GOAL", 13, "", o_file, 0);
    default_goal_name = &v->value;
  }

  /* Read all the makefiles.  */

  read_makefiles
    = read_all_makefiles (makefiles == 0 ? 0 : makefiles->list);

#ifdef WINDOWS32
  /* look one last time after reading all Makefiles */
  if (no_default_sh_exe)
    no_default_sh_exe = !find_and_set_default_shell(NULL);
#endif /* WINDOWS32 */

#if defined (__MSDOS__) || defined (__EMX__)
  /* We need to know what kind of shell we will be using.  */
  {
    extern int _is_unixy_shell (const char *_path);
    struct variable *shv = lookup_variable (STRING_SIZE_TUPLE ("SHELL"));
    extern int unixy_shell;
    extern char *default_shell;

    if (shv && *shv->value)
      {
	char *shell_path = recursively_expand(shv);

	if (shell_path && _is_unixy_shell (shell_path))
	  unixy_shell = 1;
	else
	  unixy_shell = 0;
	if (shell_path)
	  default_shell = shell_path;
      }
  }
#endif /* __MSDOS__ || __EMX__ */

  /* Decode switches again, in case the variables were set by the makefile.  */
#ifdef KMK
  decode_env_switches (STRING_SIZE_TUPLE ("KMK_FLAGS"));
#else /* !KMK */
  decode_env_switches (STRING_SIZE_TUPLE ("MAKEFLAGS"));
#if 0
  decode_env_switches (STRING_SIZE_TUPLE ("MFLAGS"));
#endif
#endif /* !KMK */

#if defined (__MSDOS__) || defined (__EMX__)
  if (job_slots != 1
# ifdef __EMX__
      && _osmode != OS2_MODE /* turn off -j if we are in DOS mode */
# endif
      )
    {
      error (NILF,
             _("Parallel jobs (-j) are not supported on this platform."));
      error (NILF, _("Resetting to single job (-j1) mode."));
      job_slots = 1;
    }
#endif

#ifdef MAKE_JOBSERVER
  /* If the jobserver-fds option is seen, make sure that -j is reasonable.  */

  if (jobserver_fds)
  {
    const char *cp;
    unsigned int ui;

    for (ui=1; ui < jobserver_fds->idx; ++ui)
      if (!streq (jobserver_fds->list[0], jobserver_fds->list[ui]))
        fatal (NILF, _("internal error: multiple --jobserver-fds options"));

    /* Now parse the fds string and make sure it has the proper format.  */

    cp = jobserver_fds->list[0];

    if (sscanf (cp, "%d,%d", &job_fds[0], &job_fds[1]) != 2)
      fatal (NILF,
             _("internal error: invalid --jobserver-fds string `%s'"), cp);

    DB (DB_JOBS,
	(_("Jobserver client (fds %d,%d)\n"), job_fds[0], job_fds[1]));

    /* The combination of a pipe + !job_slots means we're using the
       jobserver.  If !job_slots and we don't have a pipe, we can start
       infinite jobs.  If we see both a pipe and job_slots >0 that means the
       user set -j explicitly.  This is broken; in this case obey the user
       (ignore the jobserver pipe for this make) but print a message.  */

    if (job_slots > 0)
      error (NILF,
             _("warning: -jN forced in submake: disabling jobserver mode."));

    /* Create a duplicate pipe, that will be closed in the SIGCHLD
       handler.  If this fails with EBADF, the parent has closed the pipe
       on us because it didn't think we were a submake.  If so, print a
       warning then default to -j1.  */

    else if ((job_rfd = dup (job_fds[0])) < 0)
      {
        if (errno != EBADF)
          pfatal_with_name (_("dup jobserver"));

        error (NILF,
               _("warning: jobserver unavailable: using -j1.  Add `+' to parent make rule."));
        job_slots = 1;
      }

    if (job_slots > 0)
      {
        close (job_fds[0]);
        close (job_fds[1]);
        job_fds[0] = job_fds[1] = -1;
        free (jobserver_fds->list);
        free (jobserver_fds);
        jobserver_fds = 0;
      }
  }

  /* If we have >1 slot but no jobserver-fds, then we're a top-level make.
     Set up the pipe and install the fds option for our children.  */

  if (job_slots > 1)
    {
      char *cp;
      char c = '+';

      if (pipe (job_fds) < 0 || (job_rfd = dup (job_fds[0])) < 0)
	pfatal_with_name (_("creating jobs pipe"));

      /* Every make assumes that it always has one job it can run.  For the
         submakes it's the token they were given by their parent.  For the
         top make, we just subtract one from the number the user wants.  We
         want job_slots to be 0 to indicate we're using the jobserver.  */

      master_job_slots = job_slots;

      while (--job_slots)
        {
          int r;

          EINTRLOOP (r, write (job_fds[1], &c, 1));
          if (r != 1)
            pfatal_with_name (_("init jobserver pipe"));
        }

      /* Fill in the jobserver_fds struct for our children.  */

      cp = xmalloc ((sizeof ("1024")*2)+1);
      sprintf (cp, "%d,%d", job_fds[0], job_fds[1]);

      jobserver_fds = (struct stringlist *)
                        xmalloc (sizeof (struct stringlist));
      jobserver_fds->list = xmalloc (sizeof (char *));
      jobserver_fds->list[0] = cp;
      jobserver_fds->idx = 1;
      jobserver_fds->max = 1;
    }
#endif

#ifndef MAKE_SYMLINKS
  if (check_symlink_flag)
    {
      error (NILF, _("Symbolic links not supported: disabling -L."));
      check_symlink_flag = 0;
    }
#endif

  /* Set up MAKEFLAGS and MFLAGS again, so they will be right.  */

  define_makeflags (1, 0);

  /* Make each `struct dep' point at the `struct file' for the file
     depended on.  Also do magic for special targets.  */

  snap_deps ();

  /* Convert old-style suffix rules to pattern rules.  It is important to
     do this before installing the built-in pattern rules below, so that
     makefile-specified suffix rules take precedence over built-in pattern
     rules.  */

  convert_to_pattern ();

  /* Install the default implicit pattern rules.
     This used to be done before reading the makefiles.
     But in that case, built-in pattern rules were in the chain
     before user-defined ones, so they matched first.  */

  install_default_implicit_rules ();

  /* Compute implicit rule limits.  */

  count_implicit_rule_limits ();

  /* Construct the listings of directories in VPATH lists.  */

  build_vpath_lists ();

  /* Mark files given with -o flags as very old and as having been updated
     already, and files given with -W flags as brand new (time-stamp as far
     as possible into the future).  If restarts is set we'll do -W later.  */

  if (old_files != 0)
    {
      const char **p;
      for (p = old_files->list; *p != 0; ++p)
        {
          struct file *f = enter_file (*p);
          f->last_mtime = f->mtime_before_update = OLD_MTIME;
          f->updated = 1;
          f->update_status = 0;
          f->command_state = cs_finished;
        }
    }

  if (!restarts && new_files != 0)
    {
      const char **p;
      for (p = new_files->list; *p != 0; ++p)
	{
	  struct file *f = enter_file (*p);
	  f->last_mtime = f->mtime_before_update = NEW_MTIME;
	}
    }

  /* Initialize the remote job module.  */
  remote_setup ();

  if (read_makefiles != 0)
    {
      /* Update any makefiles if necessary.  */

      FILE_TIMESTAMP *makefile_mtimes = 0;
      unsigned int mm_idx = 0;
      char **nargv = argv;
      int nargc = argc;
      int orig_db_level = db_level;
      int status;

      if (! ISDB (DB_MAKEFILES))
        db_level = DB_NONE;

      DB (DB_BASIC, (_("Updating makefiles....\n")));

      /* Remove any makefiles we don't want to try to update.
	 Also record the current modtimes so we can compare them later.  */
      {
	register struct dep *d, *last;
	last = 0;
	d = read_makefiles;
	while (d != 0)
	  {
	    struct file *f = d->file;
	    if (f->double_colon)
	      for (f = f->double_colon; f != NULL; f = f->prev)
		{
		  if (f->deps == 0 && f->cmds != 0)
		    {
		      /* This makefile is a :: target with commands, but
			 no dependencies.  So, it will always be remade.
			 This might well cause an infinite loop, so don't
			 try to remake it.  (This will only happen if
			 your makefiles are written exceptionally
			 stupidly; but if you work for Athena, that's how
			 you write your makefiles.)  */

		      DB (DB_VERBOSE,
                          (_("Makefile `%s' might loop; not remaking it.\n"),
                           f->name));

		      if (last == 0)
			read_makefiles = d->next;
		      else
			last->next = d->next;

		      /* Free the storage.  */
                      free_dep (d);

		      d = last == 0 ? read_makefiles : last->next;

		      break;
		    }
		}
	    if (f == NULL || !f->double_colon)
	      {
                makefile_mtimes = xrealloc (makefile_mtimes,
                                            (mm_idx+1)
                                            * sizeof (FILE_TIMESTAMP));
		makefile_mtimes[mm_idx++] = file_mtime_no_search (d->file);
		last = d;
		d = d->next;
	      }
	  }
      }

      /* Set up `MAKEFLAGS' specially while remaking makefiles.  */
      define_makeflags (1, 1);

      rebuilding_makefiles = 1;
      status = update_goal_chain (read_makefiles);
      rebuilding_makefiles = 0;

      switch (status)
	{
	case 1:
          /* The only way this can happen is if the user specified -q and asked
           * for one of the makefiles to be remade as a target on the command
           * line.  Since we're not actually updating anything with -q we can
           * treat this as "did nothing".
           */

	case -1:
	  /* Did nothing.  */
	  break;

	case 2:
	  /* Failed to update.  Figure out if we care.  */
	  {
	    /* Nonzero if any makefile was successfully remade.  */
	    int any_remade = 0;
	    /* Nonzero if any makefile we care about failed
	       in updating or could not be found at all.  */
	    int any_failed = 0;
	    unsigned int i;
            struct dep *d;

	    for (i = 0, d = read_makefiles; d != 0; ++i, d = d->next)
              {
                /* Reset the considered flag; we may need to look at the file
                   again to print an error.  */
                d->file->considered = 0;

                if (d->file->updated)
                  {
                    /* This makefile was updated.  */
                    if (d->file->update_status == 0)
                      {
                        /* It was successfully updated.  */
                        any_remade |= (file_mtime_no_search (d->file)
                                       != makefile_mtimes[i]);
                      }
                    else if (! (d->changed & RM_DONTCARE))
                      {
                        FILE_TIMESTAMP mtime;
                        /* The update failed and this makefile was not
                           from the MAKEFILES variable, so we care.  */
                        error (NILF, _("Failed to remake makefile `%s'."),
                               d->file->name);
                        mtime = file_mtime_no_search (d->file);
                        any_remade |= (mtime != NONEXISTENT_MTIME
                                       && mtime != makefile_mtimes[i]);
                        makefile_status = MAKE_FAILURE;
                      }
                  }
                else
                  /* This makefile was not found at all.  */
                  if (! (d->changed & RM_DONTCARE))
                    {
                      /* This is a makefile we care about.  See how much.  */
                      if (d->changed & RM_INCLUDED)
                        /* An included makefile.  We don't need
                           to die, but we do want to complain.  */
                        error (NILF,
                               _("Included makefile `%s' was not found."),
                               dep_name (d));
                      else
                        {
                          /* A normal makefile.  We must die later.  */
                          error (NILF, _("Makefile `%s' was not found"),
                                 dep_name (d));
                          any_failed = 1;
                        }
                    }
              }
            /* Reset this to empty so we get the right error message below.  */
            read_makefiles = 0;

	    if (any_remade)
	      goto re_exec;
	    if (any_failed)
	      die (2);
            break;
	  }

	case 0:
	re_exec:
	  /* Updated successfully.  Re-exec ourselves.  */

	  remove_intermediates (0);

	  if (print_data_base_flag)
	    print_data_base ();

	  log_working_directory (0);

          clean_jobserver (0);

	  if (makefiles != 0)
	    {
	      /* These names might have changed.  */
	      int i, j = 0;
	      for (i = 1; i < argc; ++i)
		if (strneq (argv[i], "-f", 2)) /* XXX */
		  {
		    char *p = &argv[i][2];
		    if (*p == '\0')
                      /* This cast is OK since we never modify argv.  */
		      argv[++i] = (char *) makefiles->list[j];
		    else
		      argv[i] = xstrdup (concat ("-f", makefiles->list[j], ""));
		    ++j;
		  }
	    }

          /* Add -o option for the stdin temporary file, if necessary.  */
          if (stdin_nm)
            {
              nargv = xmalloc ((nargc + 2) * sizeof (char *));
              memcpy (nargv, argv, argc * sizeof (char *));
              nargv[nargc++] = xstrdup (concat ("-o", stdin_nm, ""));
              nargv[nargc] = 0;
            }

	  if (directories != 0 && directories->idx > 0)
	    {
	      int bad = 1;
	      if (directory_before_chdir != 0)
		{
		  if (chdir (directory_before_chdir) < 0)
		      perror_with_name ("chdir", "");
		  else
		    bad = 0;
		}
	      if (bad)
		fatal (NILF, _("Couldn't change back to original directory."));
	    }

          ++restarts;

	  if (ISDB (DB_BASIC))
	    {
	      char **p;
	      printf (_("Re-executing[%u]:"), restarts);
	      for (p = nargv; *p != 0; ++p)
		printf (" %s", *p);
	      putchar ('\n');
	    }

#ifndef _AMIGA
          {
            char **p;
            for (p = environ; *p != 0; ++p)
              {
                if (strneq (*p, MAKELEVEL_NAME, MAKELEVEL_LENGTH)
                    && (*p)[MAKELEVEL_LENGTH] == '=')
                  {
                    *p = alloca (40);
                    sprintf (*p, "%s=%u", MAKELEVEL_NAME, makelevel);
                  }
                if (strneq (*p, "MAKE_RESTARTS=", 14))
                  {
                    *p = alloca (40);
                    sprintf (*p, "MAKE_RESTARTS=%u", restarts);
                    restarts = 0;
                  }
              }
          }
#else /* AMIGA */
	  {
	    char buffer[256];

            sprintf (buffer, "%u", makelevel);
            SetVar (MAKELEVEL_NAME, buffer, -1, GVF_GLOBAL_ONLY);

            sprintf (buffer, "%u", restarts);
            SetVar ("MAKE_RESTARTS", buffer, -1, GVF_GLOBAL_ONLY);
            restarts = 0;
	  }
#endif

          /* If we didn't set the restarts variable yet, add it.  */
          if (restarts)
            {
              char *b = alloca (40);
              sprintf (b, "MAKE_RESTARTS=%u", restarts);
              putenv (b);
            }

	  fflush (stdout);
	  fflush (stderr);

          /* Close the dup'd jobserver pipe if we opened one.  */
          if (job_rfd >= 0)
            close (job_rfd);

#ifdef _AMIGA
	  exec_command (nargv);
	  exit (0);
#elif defined (__EMX__)
	  {
	    /* It is not possible to use execve() here because this
	       would cause the parent process to be terminated with
	       exit code 0 before the child process has been terminated.
	       Therefore it may be the best solution simply to spawn the
	       child process including all file handles and to wait for its
	       termination. */
	    int pid;
	    int status;
	    pid = child_execute_job (0, 1, nargv, environ);

	    /* is this loop really necessary? */
	    do {
	      pid = wait (&status);
	    } while (pid <= 0);
	    /* use the exit code of the child process */
	    exit (WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
	  }
#else
	  exec_command (nargv, environ);
#endif
	  /* NOTREACHED */

	default:
#define BOGUS_UPDATE_STATUS 0
	  assert (BOGUS_UPDATE_STATUS);
	  break;
	}

      db_level = orig_db_level;

      /* Free the makefile mtimes (if we allocated any).  */
      if (makefile_mtimes)
        free (makefile_mtimes);
    }

  /* Set up `MAKEFLAGS' again for the normal targets.  */
  define_makeflags (1, 0);

  /* Set always_make_flag if -B was given.  */
  always_make_flag = always_make_set;

  /* If restarts is set we haven't set up -W files yet, so do that now.  */
  if (restarts && new_files != 0)
    {
      const char **p;
      for (p = new_files->list; *p != 0; ++p)
	{
	  struct file *f = enter_file (*p);
	  f->last_mtime = f->mtime_before_update = NEW_MTIME;
	}
    }

  /* If there is a temp file from reading a makefile from stdin, get rid of
     it now.  */
  if (stdin_nm && unlink (stdin_nm) < 0 && errno != ENOENT)
    perror_with_name (_("unlink (temporary file): "), stdin_nm);

  {
    int status;

    /* If there were no command-line goals, use the default.  */
    if (goals == 0)
      {
        if (**default_goal_name != '\0')
          {
            if (default_goal_file == 0 ||
                strcmp (*default_goal_name, default_goal_file->name) != 0)
              {
                default_goal_file = lookup_file (*default_goal_name);

                /* In case user set .DEFAULT_GOAL to a non-existent target
                   name let's just enter this name into the table and let
                   the standard logic sort it out. */
                if (default_goal_file == 0)
                  {
                    struct nameseq *ns;
                    char *p = *default_goal_name;

#ifndef CONFIG_WITH_ALLOC_CACHES
                    ns = multi_glob (
                      parse_file_seq (&p, '\0', sizeof (struct nameseq), 1),
                      sizeof (struct nameseq));
#else
                    ns = multi_glob (
                      parse_file_seq (&p, '\0', &nameseq_cache, 1),
                      &nameseq_cache);
#endif

                    /* .DEFAULT_GOAL should contain one target. */
                    if (ns->next != 0)
                      fatal (NILF, _(".DEFAULT_GOAL contains more than one target"));

#ifndef CONFIG_WITH_VALUE_LENGTH
                    default_goal_file = enter_file (strcache_add (ns->name));
#else
                    default_goal_file = enter_file (ns->name);
#endif

                    ns->name = 0; /* It was reused by enter_file(). */
                    free_ns_chain (ns);
                  }
              }

            goals = alloc_dep ();
            goals->file = default_goal_file;
          }
      }
    else
      lastgoal->next = 0;


    if (!goals)
      {
        if (read_makefiles == 0)
          fatal (NILF, _("No targets specified and no makefile found"));

        fatal (NILF, _("No targets"));
      }

    /* Update the goals.  */

    DB (DB_BASIC, (_("Updating goal targets....\n")));

    switch (update_goal_chain (goals))
    {
      case -1:
        /* Nothing happened.  */
      case 0:
        /* Updated successfully.  */
        status = makefile_status;
        break;
      case 1:
        /* We are under -q and would run some commands.  */
        status = MAKE_TROUBLE;
        break;
      case 2:
        /* Updating failed.  POSIX.2 specifies exit status >1 for this;
           but in VMS, there is only success and failure.  */
        status = MAKE_FAILURE;
        break;
      default:
        abort ();
    }

    /* If we detected some clock skew, generate one last warning */
    if (clock_skew_detected)
      error (NILF,
             _("warning:  Clock skew detected.  Your build may be incomplete."));

    /* Exit.  */
    die (status);
  }

  /* NOTREACHED */
  return 0;
}

/* Parsing of arguments, decoding of switches.  */

static char options[1 + sizeof (switches) / sizeof (switches[0]) * 3];
static struct option long_options[(sizeof (switches) / sizeof (switches[0])) +
				  (sizeof (long_option_aliases) /
				   sizeof (long_option_aliases[0]))];

/* Fill in the string and vector for getopt.  */
static void
init_switches (void)
{
  char *p;
  unsigned int c;
  unsigned int i;

  if (options[0] != '\0')
    /* Already done.  */
    return;

  p = options;

  /* Return switch and non-switch args in order, regardless of
     POSIXLY_CORRECT.  Non-switch args are returned as option 1.  */
  *p++ = '-';

  for (i = 0; switches[i].c != '\0'; ++i)
    {
      long_options[i].name = (switches[i].long_name == 0 ? "" :
			      switches[i].long_name);
      long_options[i].flag = 0;
      long_options[i].val = switches[i].c;
      if (short_option (switches[i].c))
	*p++ = switches[i].c;
      switch (switches[i].type)
	{
	case flag:
	case flag_off:
	case ignore:
	  long_options[i].has_arg = no_argument;
	  break;

	case string:
        case filename:
	case positive_int:
	case floating:
	  if (short_option (switches[i].c))
	    *p++ = ':';
	  if (switches[i].noarg_value != 0)
	    {
	      if (short_option (switches[i].c))
		*p++ = ':';
	      long_options[i].has_arg = optional_argument;
	    }
	  else
	    long_options[i].has_arg = required_argument;
	  break;
	}
    }
  *p = '\0';
  for (c = 0; c < (sizeof (long_option_aliases) /
		   sizeof (long_option_aliases[0]));
       ++c)
    long_options[i++] = long_option_aliases[c];
  long_options[i].name = 0;
}

static void
handle_non_switch_argument (char *arg, int env)
{
  /* Non-option argument.  It might be a variable definition.  */
  struct variable *v;
  if (arg[0] == '-' && arg[1] == '\0')
    /* Ignore plain `-' for compatibility.  */
    return;
#ifndef CONFIG_WITH_VALUE_LENGTH
  v = try_variable_definition (0, arg, o_command, 0);
#else
  v = try_variable_definition (0, arg, NULL, o_command, 0);
#endif
  if (v != 0)
    {
      /* It is indeed a variable definition.  If we don't already have this
	 one, record a pointer to the variable for later use in
	 define_makeflags.  */
      struct command_variable *cv;

      for (cv = command_variables; cv != 0; cv = cv->next)
        if (cv->variable == v)
          break;

      if (! cv) {
        cv = xmalloc (sizeof (*cv));
        cv->variable = v;
        cv->next = command_variables;
        command_variables = cv;
      }
    }
  else if (! env)
    {
      /* Not an option or variable definition; it must be a goal
	 target!  Enter it as a file and add it to the dep chain of
	 goals.  */
      struct file *f = enter_file (strcache_add (expand_command_line_file (arg)));
      f->cmd_target = 1;

      if (goals == 0)
	{
	  goals = alloc_dep ();
	  lastgoal = goals;
	}
      else
	{
	  lastgoal->next = alloc_dep ();
	  lastgoal = lastgoal->next;
	}

      lastgoal->file = f;

      {
        /* Add this target name to the MAKECMDGOALS variable. */
        struct variable *gv;
        const char *value;

        gv = lookup_variable (STRING_SIZE_TUPLE ("MAKECMDGOALS"));
        if (gv == 0)
          value = f->name;
        else
          {
            /* Paste the old and new values together */
            unsigned int oldlen, newlen;
            char *vp;

            oldlen = strlen (gv->value);
            newlen = strlen (f->name);
            vp = alloca (oldlen + 1 + newlen + 1);
            memcpy (vp, gv->value, oldlen);
            vp[oldlen] = ' ';
            memcpy (&vp[oldlen + 1], f->name, newlen + 1);
            value = vp;
          }
        define_variable ("MAKECMDGOALS", 12, value, o_default, 0);
      }
    }
}

/* Print a nice usage method.  */

static void
print_usage (int bad)
{
  const char *const *cpp;
  FILE *usageto;

  if (print_version_flag)
    print_version ();

  usageto = bad ? stderr : stdout;

  fprintf (usageto, _("Usage: %s [options] [target] ...\n"), program);

  for (cpp = usage; *cpp; ++cpp)
    fputs (_(*cpp), usageto);

#ifdef KMK
  if (!remote_description || *remote_description == '\0')
    fprintf (usageto, _("\nThis program is built for %s/%s/%s [" __DATE__ " " __TIME__ "]\n"),
             KBUILD_HOST, KBUILD_HOST_ARCH, KBUILD_HOST_CPU);
  else
    fprintf (usageto, _("\nThis program is built for %s/%s/%s (%s) [" __DATE__ " " __TIME__ "]\n"),
             KBUILD_HOST, KBUILD_HOST_ARCH, KBUILD_HOST_CPU, remote_description);
#else  /* !KMK */
  if (!remote_description || *remote_description == '\0')
    fprintf (usageto, _("\nThis program built for %s\n"), make_host);
  else
    fprintf (usageto, _("\nThis program built for %s (%s)\n"),
             make_host, remote_description);
#endif /* !KMK */

  fprintf (usageto, _("Report bugs to <bug-make@gnu.org>\n"));
}

/* Decode switches from ARGC and ARGV.
   They came from the environment if ENV is nonzero.  */

static void
decode_switches (int argc, char **argv, int env)
{
  int bad = 0;
  register const struct command_switch *cs;
  register struct stringlist *sl;
  register int c;

  /* getopt does most of the parsing for us.
     First, get its vectors set up.  */

  init_switches ();

  /* Let getopt produce error messages for the command line,
     but not for options from the environment.  */
  opterr = !env;
  /* Reset getopt's state.  */
  optind = 0;

  while (optind < argc)
    {
      /* Parse the next argument.  */
      c = getopt_long (argc, argv, options, long_options, (int *) 0);
      if (c == EOF)
	/* End of arguments, or "--" marker seen.  */
	break;
      else if (c == 1)
	/* An argument not starting with a dash.  */
	handle_non_switch_argument (optarg, env);
      else if (c == '?')
	/* Bad option.  We will print a usage message and die later.
	   But continue to parse the other options so the user can
	   see all he did wrong.  */
	bad = 1;
      else
	for (cs = switches; cs->c != '\0'; ++cs)
	  if (cs->c == c)
	    {
	      /* Whether or not we will actually do anything with
		 this switch.  We test this individually inside the
		 switch below rather than just once outside it, so that
		 options which are to be ignored still consume args.  */
	      int doit = !env || cs->env;

	      switch (cs->type)
		{
		default:
		  abort ();

		case ignore:
		  break;

		case flag:
		case flag_off:
		  if (doit)
		    *(int *) cs->value_ptr = cs->type == flag;
		  break;

		case string:
		case filename:
		  if (!doit)
		    break;

		  if (optarg == 0)
		    optarg = xstrdup (cs->noarg_value);
                  else if (*optarg == '\0')
                    {
                      error (NILF, _("the `-%c' option requires a non-empty string argument"),
                             cs->c);
                      bad = 1;
                    }

		  sl = *(struct stringlist **) cs->value_ptr;
		  if (sl == 0)
		    {
		      sl = (struct stringlist *)
			xmalloc (sizeof (struct stringlist));
		      sl->max = 5;
		      sl->idx = 0;
		      sl->list = xmalloc (5 * sizeof (char *));
		      *(struct stringlist **) cs->value_ptr = sl;
		    }
		  else if (sl->idx == sl->max - 1)
		    {
		      sl->max += 5;
		      sl->list = xrealloc ((void *)sl->list, /* bird */
                                           sl->max * sizeof (char *));
		    }
                  if (cs->type == filename)
                    sl->list[sl->idx++] = expand_command_line_file (optarg);
                  else
                    sl->list[sl->idx++] = optarg;
		  sl->list[sl->idx] = 0;
		  break;

		case positive_int:
                  /* See if we have an option argument; if we do require that
                     it's all digits, not something like "10foo".  */
		  if (optarg == 0 && argc > optind)
                    {
                      const char *cp;
                      for (cp=argv[optind]; ISDIGIT (cp[0]); ++cp)
                        ;
                      if (cp[0] == '\0')
                        optarg = argv[optind++];
                    }

		  if (!doit)
		    break;

		  if (optarg != 0)
		    {
		      int i = atoi (optarg);
                      const char *cp;

                      /* Yes, I realize we're repeating this in some cases.  */
                      for (cp = optarg; ISDIGIT (cp[0]); ++cp)
                        ;

		      if (i < 1 || cp[0] != '\0')
			{
                          error (NILF, _("the `-%c' option requires a positive integral argument"),
                                 cs->c);
			  bad = 1;
			}
		      else
			*(unsigned int *) cs->value_ptr = i;
		    }
		  else
		    *(unsigned int *) cs->value_ptr
		      = *(unsigned int *) cs->noarg_value;
		  break;

#ifndef NO_FLOAT
		case floating:
		  if (optarg == 0 && optind < argc
		      && (ISDIGIT (argv[optind][0]) || argv[optind][0] == '.'))
		    optarg = argv[optind++];

		  if (doit)
		    *(double *) cs->value_ptr
		      = (optarg != 0 ? atof (optarg)
			 : *(double *) cs->noarg_value);

		  break;
#endif
		}

	      /* We've found the switch.  Stop looking.  */
	      break;
	    }
    }

  /* There are no more options according to getting getopt, but there may
     be some arguments left.  Since we have asked for non-option arguments
     to be returned in order, this only happens when there is a "--"
     argument to prevent later arguments from being options.  */
  while (optind < argc)
    handle_non_switch_argument (argv[optind++], env);


  if (!env && (bad || print_usage_flag))
    {
      print_usage (bad);
      die (bad ? 2 : 0);
    }
}

/* Decode switches from environment variable ENVAR (which is LEN chars long).
   We do this by chopping the value into a vector of words, prepending a
   dash to the first word if it lacks one, and passing the vector to
   decode_switches.  */

static void
decode_env_switches (char *envar, unsigned int len)
{
  char *varref = alloca (2 + len + 2);
  char *value, *p;
  int argc;
  char **argv;

  /* Get the variable's value.  */
  varref[0] = '$';
  varref[1] = '(';
  memcpy (&varref[2], envar, len);
  varref[2 + len] = ')';
  varref[2 + len + 1] = '\0';
  value = variable_expand (varref);

  /* Skip whitespace, and check for an empty value.  */
  value = next_token (value);
  len = strlen (value);
  if (len == 0)
    return;

  /* Allocate a vector that is definitely big enough.  */
  argv = alloca ((1 + len + 1) * sizeof (char *));

  /* Allocate a buffer to copy the value into while we split it into words
     and unquote it.  We must use permanent storage for this because
     decode_switches may store pointers into the passed argument words.  */
  p = xmalloc (2 * len);

  /* getopt will look at the arguments starting at ARGV[1].
     Prepend a spacer word.  */
  argv[0] = 0;
  argc = 1;
  argv[argc] = p;
  while (*value != '\0')
    {
      if (*value == '\\' && value[1] != '\0')
	++value;		/* Skip the backslash.  */
      else if (isblank ((unsigned char)*value))
	{
	  /* End of the word.  */
	  *p++ = '\0';
	  argv[++argc] = p;
	  do
	    ++value;
	  while (isblank ((unsigned char)*value));
	  continue;
	}
      *p++ = *value++;
    }
  *p = '\0';
  argv[++argc] = 0;

  if (argv[1][0] != '-' && strchr (argv[1], '=') == 0)
    /* The first word doesn't start with a dash and isn't a variable
       definition.  Add a dash and pass it along to decode_switches.  We
       need permanent storage for this in case decode_switches saves
       pointers into the value.  */
    argv[1] = xstrdup (concat ("-", argv[1], ""));

  /* Parse those words.  */
  decode_switches (argc, argv, 1);
}

/* Quote the string IN so that it will be interpreted as a single word with
   no magic by decode_env_switches; also double dollar signs to avoid
   variable expansion in make itself.  Write the result into OUT, returning
   the address of the next character to be written.
   Allocating space for OUT twice the length of IN is always sufficient.  */

static char *
quote_for_env (char *out, const char *in)
{
  while (*in != '\0')
    {
      if (*in == '$')
	*out++ = '$';
      else if (isblank ((unsigned char)*in) || *in == '\\')
        *out++ = '\\';
      *out++ = *in++;
    }

  return out;
}

/* Define the MAKEFLAGS and MFLAGS variables to reflect the settings of the
   command switches.  Include options with args if ALL is nonzero.
   Don't include options with the `no_makefile' flag set if MAKEFILE.  */

static void
define_makeflags (int all, int makefile)
{
#ifdef KMK
  static const char ref[] = "$(KMK_OVERRIDES)";
#else
  static const char ref[] = "$(MAKEOVERRIDES)";
#endif
  static const char posixref[] = "$(-*-command-variables-*-)";
  register const struct command_switch *cs;
  char *flagstring;
  register char *p;
  unsigned int words;
  struct variable *v;

  /* We will construct a linked list of `struct flag's describing
     all the flags which need to go in MAKEFLAGS.  Then, once we
     know how many there are and their lengths, we can put them all
     together in a string.  */

  struct flag
    {
      struct flag *next;
      const struct command_switch *cs;
      const char *arg;
    };
  struct flag *flags = 0;
  unsigned int flagslen = 0;
#define	ADD_FLAG(ARG, LEN) \
  do {									      \
    struct flag *new = alloca (sizeof (struct flag));                         \
    new->cs = cs;							      \
    new->arg = (ARG);							      \
    new->next = flags;							      \
    flags = new;							      \
    if (new->arg == 0)							      \
      ++flagslen;		/* Just a single flag letter.  */	      \
    else								      \
      flagslen += 1 + 1 + 1 + 1 + 3 * (LEN); /* " -x foo" */		      \
    if (!short_option (cs->c))						      \
      /* This switch has no single-letter version, so we use the long.  */    \
      flagslen += 2 + strlen (cs->long_name);				      \
  } while (0)

  for (cs = switches; cs->c != '\0'; ++cs)
    if (cs->toenv && (!makefile || !cs->no_makefile))
      switch (cs->type)
	{
	case ignore:
	  break;

	case flag:
	case flag_off:
	  if (!*(int *) cs->value_ptr == (cs->type == flag_off)
	      && (cs->default_value == 0
		  || *(int *) cs->value_ptr != *(int *) cs->default_value))
	    ADD_FLAG (0, 0);
	  break;

	case positive_int:
	  if (all)
	    {
	      if ((cs->default_value != 0
		   && (*(unsigned int *) cs->value_ptr
		       == *(unsigned int *) cs->default_value)))
		break;
	      else if (cs->noarg_value != 0
		       && (*(unsigned int *) cs->value_ptr ==
			   *(unsigned int *) cs->noarg_value))
		ADD_FLAG ("", 0); /* Optional value omitted; see below.  */
#if !defined(KMK) || !defined(WINDOWS32) /* jobserver stuff doesn't work on windows???. */
	      else if (cs->c == 'j')
		/* Special case for `-j'.  */
		ADD_FLAG ("1", 1);
#endif
	      else
		{
		  char *buf = alloca (30);
		  sprintf (buf, "%u", *(unsigned int *) cs->value_ptr);
		  ADD_FLAG (buf, strlen (buf));
		}
	    }
	  break;

#ifndef NO_FLOAT
	case floating:
	  if (all)
	    {
	      if (cs->default_value != 0
		  && (*(double *) cs->value_ptr
		      == *(double *) cs->default_value))
		break;
	      else if (cs->noarg_value != 0
		       && (*(double *) cs->value_ptr
			   == *(double *) cs->noarg_value))
		ADD_FLAG ("", 0); /* Optional value omitted; see below.  */
	      else
		{
		  char *buf = alloca (100);
		  sprintf (buf, "%g", *(double *) cs->value_ptr);
		  ADD_FLAG (buf, strlen (buf));
		}
	    }
	  break;
#endif

	case filename:
	case string:
	  if (all)
	    {
	      struct stringlist *sl = *(struct stringlist **) cs->value_ptr;
	      if (sl != 0)
		{
		  /* Add the elements in reverse order, because all the flags
		     get reversed below; and the order matters for some
		     switches (like -I).  */
		  unsigned int i = sl->idx;
		  while (i-- > 0)
		    ADD_FLAG (sl->list[i], strlen (sl->list[i]));
		}
	    }
	  break;

	default:
	  abort ();
	}

  flagslen += 4 + sizeof posixref; /* Four more for the possible " -- ".  */

#undef	ADD_FLAG

  /* Construct the value in FLAGSTRING.
     We allocate enough space for a preceding dash and trailing null.  */
  flagstring = alloca (1 + flagslen + 1);
  memset (flagstring, '\0', 1 + flagslen + 1);
  p = flagstring;
  words = 1;
  *p++ = '-';
  while (flags != 0)
    {
      /* Add the flag letter or name to the string.  */
      if (short_option (flags->cs->c))
	*p++ = flags->cs->c;
      else
	{
          if (*p != '-')
            {
              *p++ = ' ';
              *p++ = '-';
            }
	  *p++ = '-';
	  strcpy (p, flags->cs->long_name);
	  p += strlen (p);
	}
      if (flags->arg != 0)
	{
	  /* A flag that takes an optional argument which in this case is
	     omitted is specified by ARG being "".  We must distinguish
	     because a following flag appended without an intervening " -"
	     is considered the arg for the first.  */
	  if (flags->arg[0] != '\0')
	    {
	      /* Add its argument too.  */
	      *p++ = !short_option (flags->cs->c) ? '=' : ' ';
	      p = quote_for_env (p, flags->arg);
	    }
	  ++words;
	  /* Write a following space and dash, for the next flag.  */
	  *p++ = ' ';
	  *p++ = '-';
	}
      else if (!short_option (flags->cs->c))
	{
	  ++words;
	  /* Long options must each go in their own word,
	     so we write the following space and dash.  */
	  *p++ = ' ';
	  *p++ = '-';
	}
      flags = flags->next;
    }

  /* Define MFLAGS before appending variable definitions.  */

  if (p == &flagstring[1])
    /* No flags.  */
    flagstring[0] = '\0';
  else if (p[-1] == '-')
    {
      /* Kill the final space and dash.  */
      p -= 2;
      *p = '\0';
    }
  else
    /* Terminate the string.  */
    *p = '\0';

#ifdef KMK
  /* Since MFLAGS is not parsed for flags, there is no reason to
     override any makefile redefinition.  */
  (void) define_variable ("MFLAGS", 6, flagstring, o_env, 1);
#endif /* !KMK */

  if (all && command_variables != 0)
    {
      /* Now write a reference to $(MAKEOVERRIDES), which contains all the
	 command-line variable definitions.  */

      if (p == &flagstring[1])
	/* No flags written, so elide the leading dash already written.  */
	p = flagstring;
      else
	{
	  /* Separate the variables from the switches with a "--" arg.  */
	  if (p[-1] != '-')
	    {
	      /* We did not already write a trailing " -".  */
	      *p++ = ' ';
	      *p++ = '-';
	    }
	  /* There is a trailing " -"; fill it out to " -- ".  */
	  *p++ = '-';
	  *p++ = ' ';
	}

      /* Copy in the string.  */
      if (posix_pedantic)
	{
	  memcpy (p, posixref, sizeof posixref - 1);
	  p += sizeof posixref - 1;
	}
      else
	{
	  memcpy (p, ref, sizeof ref - 1);
	  p += sizeof ref - 1;
	}
    }
  else if (p == &flagstring[1])
    {
      words = 0;
      --p;
    }
  else if (p[-1] == '-')
    /* Kill the final space and dash.  */
    p -= 2;
  /* Terminate the string.  */
  *p = '\0';

#ifdef KMK
  v = define_variable ("KMK_FLAGS", 9,
                       /* If there are switches, omit the leading dash
                          unless it is a single long option with two
                          leading dashes.  */
                       &flagstring[(flagstring[0] == '-'
                                    && flagstring[1] != '-')
                                   ? 1 : 0],
                       /* This used to use o_env, but that lost when a
                          makefile defined MAKEFLAGS.  Makefiles set
                          MAKEFLAGS to add switches, but we still want
                          to redefine its value with the full set of
                          switches.  Of course, an override or command
                          definition will still take precedence.  */
                       o_file, 1);
#else
  v = define_variable ("MAKEFLAGS", 9,
		       /* If there are switches, omit the leading dash
			  unless it is a single long option with two
			  leading dashes.  */
		       &flagstring[(flagstring[0] == '-'
				    && flagstring[1] != '-')
				   ? 1 : 0],
		       /* This used to use o_env, but that lost when a
			  makefile defined MAKEFLAGS.  Makefiles set
			  MAKEFLAGS to add switches, but we still want
			  to redefine its value with the full set of
			  switches.  Of course, an override or command
			  definition will still take precedence.  */
		       o_file, 1);
#endif
  if (! all)
    /* The first time we are called, set MAKEFLAGS to always be exported.
       We should not do this again on the second call, because that is
       after reading makefiles which might have done `unexport MAKEFLAGS'. */
    v->export = v_export;

#ifdef KMK
  /* Provide simple access to some of the options. */
  {
    char val[32];
    sprintf (val, "%u", job_slots);
    define_variable ("KMK_OPTS_JOBS", sizeof("KMK_OPTS_JOBS") - 1,
                     val, o_default, 1);
    define_variable ("KMK_OPTS_KEEP_GOING", sizeof("KMK_OPTS_KEEP_GOING") - 1,
                     keep_going_flag ? "1" : "0", o_default, 1);
    define_variable ("KMK_OPTS_JUST_PRINT", sizeof("KMK_OPTS_JUST_PRINT") - 1,
                     just_print_flag ? "1" : "0", o_default, 1);
    define_variable ("KMK_OPTS_PRETTY_COMMAND_PRINTING", sizeof("KMK_OPTS_PRETTY_COMMAND_PRINTING") - 1,
                     pretty_command_printing ? "1" : "0", o_default, 1);
    sprintf (val, "%u", process_priority);
    define_variable ("KMK_OPTS_PRORITY", sizeof("KMK_OPTS_PRORITY") - 1,
                     val, o_default, 1);
    sprintf (val, "%u", process_affinity);
    define_variable ("KMK_OPTS_AFFINITY", sizeof("KMK_OPTS_AFFINITY") - 1,
                     val, o_default, 1);
#if defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)
    define_variable ("KMK_OPTS_STATISTICS", sizeof("KMK_OPTS_STATISTICS") - 1,
                     make_expensive_statistics ? "1" : "0", o_default, 1);
# endif
#ifdef CONFIG_WITH_PRINT_TIME_SWITCH
    sprintf (val, "%u", print_time_min);
    define_variable ("KMK_OPTS_PRINT_TIME", sizeof("KMK_OPTS_PRINT_TIME") - 1,
                     val, o_default, 1);
#endif
  }
#endif
}

/* Print version information.  */

static void
print_version (void)
{
  static int printed_version = 0;

  char *precede = print_data_base_flag ? "# " : "";

  if (printed_version)
    /* Do it only once.  */
    return;

#ifdef KMK
  printf ("%skmk - kBuild version %d.%d.%d (r%u)\n\
\n",
          precede, KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR,
          KBUILD_VERSION_PATCH, KBUILD_SVN_REV);

  printf("%sBased on GNU Make %s:\n", precede, version_string);

#else  /* !KMK */
  printf ("%sGNU Make %s\n", precede, version_string);

  if (!remote_description || *remote_description == '\0')
    printf (_("%sBuilt for %s\n"), precede, make_host);
  else
    printf (_("%sBuilt for %s (%s)\n"),
            precede, make_host, remote_description);
#endif /* !KMK */

  /* Print this untranslated.  The coding standards recommend translating the
     (C) to the copyright symbol, but this string is going to change every
     year, and none of the rest of it should be translated (including the
     word "Copyright", so it hardly seems worth it.  */

  printf ("%sCopyright (C) 2007  Free Software Foundation, Inc.\n", precede);

  printf (_("%sLicense GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n\
%sThis is free software: you are free to change and redistribute it.\n\
%sThere is NO WARRANTY, to the extent permitted by law.\n"),
            precede, precede, precede);

#ifdef KMK
  printf ("\n\
%skBuild modifications:\n\
%s Copyright (C) 2005-2008  Knut St. Osmundsen.\n\
\n\
%skmkbuiltin commands derived from *BSD sources:\n\
%s Copyright (c) 1983 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994\n\
%s  The Regents of the University of California. All rights reserved.\n\
%s Copyright (c) 1998  Todd C. Miller <Todd.Miller@courtesan.com>\n",
          precede, precede, precede, precede, precede, precede);

# ifdef KBUILD_PATH
  printf (_("\n\
%sKBUILD_PATH:     '%s' (default '%s')\n\
%sKBUILD_BIN_PATH: '%s' (default '%s')\n\
\n"),
          precede, get_kbuild_path(), KBUILD_PATH,
          precede, get_kbuild_bin_path(), KBUILD_BIN_PATH);
# else  /* !KBUILD_PATH */
  printf ("\n\
%sKBUILD_PATH:     '%s'\n\
%sKBUILD_BIN_PATH: '%s'\n\
\n",
          precede, get_kbuild_path(),
          precede, get_kbuild_bin_path());
# endif /* !KBUILD_PATH */

  if (!remote_description || *remote_description == '\0')
    printf (_("%sThis program is a %s build, built for %s/%s/%s [" __DATE__ " " __TIME__ "]\n\n"),
            precede, KBUILD_TYPE, KBUILD_HOST, KBUILD_HOST_ARCH, KBUILD_HOST_CPU);
  else
    printf (_("%sThis program is a %s build, built for %s/%s/%s (%s) [" __DATE__ " " __TIME__ "]\n\n"),
            precede, KBUILD_TYPE, KBUILD_HOST, KBUILD_HOST_ARCH, KBUILD_HOST_CPU, remote_description);

#endif /* KMK */

  printed_version = 1;

  /* Flush stdout so the user doesn't have to wait to see the
     version information while things are thought about.  */
  fflush (stdout);
}

/* Print a bunch of information about this and that.  */

static void
print_data_base ()
{
  time_t when;

  when = time ((time_t *) 0);
  printf (_("\n# Make data base, printed on %s"), ctime (&when));

  print_variable_data_base ();
  print_dir_data_base ();
  print_rule_data_base ();
  print_file_data_base ();
  print_vpath_data_base ();
#ifndef CONFIG_WITH_STRCACHE2
  strcache_print_stats ("#");
#else
  strcache2_print_stats_all ("#");
#endif
#ifdef CONFIG_WITH_ALLOC_CACHES
  alloccache_print_all ();
#endif

  when = time ((time_t *) 0);
  printf (_("\n# Finished Make data base on %s\n"), ctime (&when));
}
#ifdef CONFIG_WITH_PRINT_STATS_SWITCH

static void
print_stats ()
{
  time_t when;

  when = time ((time_t *) 0);
  printf (_("\n# Make statistics, printed on %s"), ctime (&when));

  print_variable_stats ();
  print_file_stats ();
# ifndef CONFIG_WITH_STRCACHE2
  strcache_print_stats ("#");
# else
  strcache2_print_stats_all ("#");
# endif
# ifdef CONFIG_WITH_ALLOC_CACHES
  alloccache_print_all ();
# endif
  print_heap_stats ();

  when = time ((time_t *) 0);
  printf (_("\n# Finished Make statistics on %s\n"), ctime (&when));
}
#endif

static void
clean_jobserver (int status)
{
  char token = '+';

  /* Sanity: have we written all our jobserver tokens back?  If our
     exit status is 2 that means some kind of syntax error; we might not
     have written all our tokens so do that now.  If tokens are left
     after any other error code, that's bad.  */

  if (job_fds[0] != -1 && jobserver_tokens)
    {
      if (status != 2)
        error (NILF,
               "INTERNAL: Exiting with %u jobserver tokens (should be 0)!",
               jobserver_tokens);
      else
        while (jobserver_tokens--)
          {
            int r;

            EINTRLOOP (r, write (job_fds[1], &token, 1));
            if (r != 1)
              perror_with_name ("write", "");
          }
    }


  /* Sanity: If we're the master, were all the tokens written back?  */

  if (master_job_slots)
    {
      /* We didn't write one for ourself, so start at 1.  */
      unsigned int tcnt = 1;

      /* Close the write side, so the read() won't hang.  */
      close (job_fds[1]);

      while (read (job_fds[0], &token, 1) == 1)
        ++tcnt;

      if (tcnt != master_job_slots)
        error (NILF,
               "INTERNAL: Exiting with %u jobserver tokens available; should be %u!",
               tcnt, master_job_slots);

      close (job_fds[0]);
    }
}

/* Exit with STATUS, cleaning up as necessary.  */

void
die (int status)
{
  static char dying = 0;

  if (!dying)
    {
      int err;

      dying = 1;

      if (print_version_flag)
	print_version ();

      /* Wait for children to die.  */
      err = (status != 0);
      while (job_slots_used > 0)
	reap_children (1, err);

      /* Let the remote job module clean up its state.  */
      remote_cleanup ();

      /* Remove the intermediate files.  */
      remove_intermediates (0);

      if (print_data_base_flag)
	print_data_base ();

#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
      if (print_stats_flag)
        print_stats ();
#endif

#ifdef NDEBUG /* bird: Don't waste time on debug sanity checks.  */
      if (print_data_base_flag || db_level)
#endif
        verify_file_data_base ();

      clean_jobserver (status);

      /* Try to move back to the original directory.  This is essential on
	 MS-DOS (where there is really only one process), and on Unix it
	 puts core files in the original directory instead of the -C
	 directory.  Must wait until after remove_intermediates(), or unlinks
         of relative pathnames fail.  */
      if (directory_before_chdir != 0)
	chdir (directory_before_chdir);

#ifdef CONFIG_WITH_PRINT_TIME_SWITCH
      if (print_time_min != -1)
        {
          big_int elapsed = nano_timestamp () - make_start_ts;
          if (elapsed >= print_time_min * BIG_INT_C(1000000000))
            {
              char buf[64];
              format_elapsed_nano (buf, sizeof (buf), elapsed);
              message (1, _("%*s"), print_time_width, buf);
            }
        }
#endif

      log_working_directory (0);
    }

  exit (status);
}

/* Write a message indicating that we've just entered or
   left (according to ENTERING) the current directory.  */

void
log_working_directory (int entering)
{
  static int entered = 0;

  /* Print nothing without the flag.  Don't print the entering message
     again if we already have.  Don't print the leaving message if we
     haven't printed the entering message.  */
  if (! print_directory_flag || entering == entered)
    return;

  entered = entering;

  if (print_data_base_flag)
    fputs ("# ", stdout);

  /* Use entire sentences to give the translators a fighting chance.  */

  if (makelevel == 0)
    if (starting_directory == 0)
      if (entering)
        printf (_("%s: Entering an unknown directory\n"), program);
      else
        printf (_("%s: Leaving an unknown directory\n"), program);
    else
      if (entering)
        printf (_("%s: Entering directory `%s'\n"),
                program, starting_directory);
      else
        printf (_("%s: Leaving directory `%s'\n"),
                program, starting_directory);
  else
    if (starting_directory == 0)
      if (entering)
        printf (_("%s[%u]: Entering an unknown directory\n"),
                program, makelevel);
      else
        printf (_("%s[%u]: Leaving an unknown directory\n"),
                program, makelevel);
    else
      if (entering)
        printf (_("%s[%u]: Entering directory `%s'\n"),
                program, makelevel, starting_directory);
      else
        printf (_("%s[%u]: Leaving directory `%s'\n"),
                program, makelevel, starting_directory);

  /* Flush stdout to be sure this comes before any stderr output.  */
  fflush (stdout);
}
