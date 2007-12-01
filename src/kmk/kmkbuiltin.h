/* $Id$ */
/** @file
 *
 * kMk Builtin command handling.
 *
 * Copyright (c) 2005-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifdef _MSC_VER
# ifndef pid_t /* see config.h.win */
#  define pid_t int
# endif
#else
# include <sys/types.h>
#endif

int kmk_builtin_command(const char *pszCmd, char ***ppapszArgvToSpawn, pid_t *pPidSpawned);
int kmk_builtin_command_parsed(int argc, char **argv, char ***ppapszArgvToSpawn, pid_t *pPidSpawned);

extern int kmk_builtin_append(int argc, char **argv, char **envp);
extern int kmk_builtin_cp(int argc, char **argv, char **envp);
extern int kmk_builtin_cat(int argc, char **argv, char **envp);
extern int kmk_builtin_cmp(int argc, char **argv, char **envp);
extern int kmk_builtin_echo(int argc, char **argv, char **envp);
extern int kmk_builtin_install(int argc, char **argv, char **envp);
extern int kmk_builtin_ln(int argc, char **argv, char **envp);
extern int kmk_builtin_md5sum(int argc, char **argv, char **envp);
extern int kmk_builtin_mkdir(int argc, char **argv, char **envp);
extern int kmk_builtin_mv(int argc, char **argv, char **envp);
extern int kmk_builtin_printf(int argc, char **argv, char **envp);
extern int kmk_builtin_rm(int argc, char **argv, char **envp);
extern int kmk_builtin_rmdir(int argc, char **argv, char **envp);
extern int kmk_builtin_test(int argc, char **argv, char **envp, char ***ppapszArgvSpawn);
extern int kmk_builtin_kDepIDB(int argc, char **argv, char **envp);

extern int kbuild_version(const char *argv0);

