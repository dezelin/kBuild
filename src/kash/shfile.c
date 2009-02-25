/* $Id$ */
/** @file
 *
 * File management.
 *
 * Copyright (c) 2007-2009  knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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

#include "shfile.h"
#include "shinstance.h" /* TRACE2 */
#include <stdlib.h>
#include <stdio.h>

#if K_OS == K_OS_WINDOWS
# include <limits.h>
# ifndef PIPE_BUF
#  define PIPE_BUF 512
# endif
#else
# include <unistd.h>
# include <fcntl.h>
# include <dirent.h>
#endif

#ifdef DEBUG
extern FILE *tracefile;
#endif


int shfile_open(shfdtab *pfdtab, const char *name, unsigned flags, mode_t mode)
{
    int fd;

#ifdef SH_PURE_STUB_MODE
    fd = -1;
#elif defined(SH_STUB_MODE)
    fd = open(name, flags, mode);
#else
#endif

#ifdef DEBUG
    if (tracefile)
        TRACE2((NULL, "shfile_open(%p:{%s}, %#x, 0%o) -> %d [%d]\n", name, name, flags, mode, fd, errno));
#endif
    return fd;
}

int shfile_pipe(shfdtab *pfdtab, int fds[2])
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return _pipe(fds, PIPE_BUF, O_BINARY);
# else
    return pipe(fds);
# endif

#else
#endif
}

int shfile_dup(shfdtab *pfdtab, int fd)
{
    int rc;
#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE)
    rc = dup(fd);

#else
#endif

    TRACE2((NULL, "shfile_dup(%d) -> %d [%d]\n", fd, rc, errno));
    return rc;
}

int shfile_close(shfdtab *pfdtab, unsigned fd)
{
    int rc;

#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE)
    rc = close(fd);

#else
#endif

    TRACE2((NULL, "shfile_close(%d) -> %d [%d]\n", fd, rc, errno));
    return rc;
}

long shfile_read(shfdtab *pfdtab, int fd, void *buf, size_t len)
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return read(fd, buf, (unsigned)len);
# else
    return read(fd, buf, len);
# endif

#else
#endif
}

long shfile_write(shfdtab *pfdtab, int fd, const void *buf, size_t len)
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return write(fd, buf, (unsigned)len);
# else
    return write(fd, buf, len);
# endif

#else
#endif
}

long shfile_lseek(shfdtab *pfdtab, int fd, long off, int whench)
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
    return lseek(fd, off, whench);

#else
#endif
}

int shfile_fcntl(shfdtab *pfdtab, int fd, int cmd, int arg)
{
    int rc;
#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE) || defined(SH_FORKED_MODE)
# ifdef _MSC_VER
    switch (cmd)
    {
        /* Just enough F_GETFL/F_SETFL to get along with. */
        case F_GETFL:
            errno = 0;
            rc = _isatty(fd);
            if (errno == EBADF)
                rc = -1;
            break;

        case F_SETFL:
            errno = 0;
            rc = _isatty(fd);
            if (errno != EBADF)
            {
                if (!arg)
                    rc = 0;
                else
                {
                    errno = EINVAL;
                    rc = -1;
                }
            }
            else
                rc = -1;
            break;

        case F_DUPFD:
        {
#  if 1
            /* the brute force approach. */
            int i = 0;
            int fds[256];
            for (i = 0; i < 256; i++)
            {
                fds[i] = -1;
                rc = _dup(fd);
                if (rc >= arg)
                    break;
                fds[i] = rc;
            }
            while (i-- > 0)
                close(fds[i]);
            if (rc < arg)
            {
                errno = EMFILE;
                rc = -1;
            }
#  else
            /* A much quick approach which is spoiled by crash validation in the CRT. */
            int fdnew = arg;
            rc = -2;
            for (fdnew = arg; fdnew < 1024; fdnew++)
            {
                /* is the file open? */
                errno = 0;
                _isatty(fdnew);
                if (errno == EBADF)
                {
                    rc = _dup2(fd, fdnew);
                    break;
                }
            }
            if (rc == -2)
            {
                errno = EMFILE;
                rc = -1;
            }
#  endif
            break;
        }
    }
# else
    rc = fcntl(fd, cmd, arg);
# endif

#else
#endif

#ifdef DEBUG
    if (tracefile)
        switch (cmd)
        {
            case F_GETFL:
                TRACE2((NULL, "shfile_fcntl(%d,F_GETFL,ignored=%d) -> %d [%d]\n", fd, arg, rc, errno));
                break;
            case F_SETFL:
                TRACE2((NULL, "shfile_fcntl(%d,F_SETFL,newflags=%#x) -> %d [%d]\n", fd, arg, rc, errno));
                break;
            case F_DUPFD:
                TRACE2((NULL, "shfile_fcntl(%d,F_DUPFS,minfd=%d) -> %d [%d]\n", fd, arg, rc, errno));
                break;
            default:
                TRACE2((NULL, "shfile_fcntl(%d,%d,%d) -> %d [%d]\n", fd, cmd, arg, rc, errno));
                break;
        }
#endif
    return rc;
}

int shfile_stat(shfdtab *pfdtab, const char *path, struct stat *pst)
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
    return stat(path, pst);

#else
#endif
}

int shfile_lstat(shfdtab *pfdtab, const char *link, struct stat *pst)
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    return stat(link, pst);
# else
    return lstat(link, pst);
# endif

#else
#endif
}

int shfile_chdir(shfdtab *pfdtab, const char *path)
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER //???
    return chdir(path);
# else
    return chdir(path);
# endif

#else
#endif
}

char *shfile_getcwd(shfdtab *pfdtab, char *buf, int len)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;

#elif defined(SH_STUB_MODE)
    return getcwd(buf, len);

#else
#endif
}

int shfile_access(shfdtab *pfdtab, const char *path, int type)
{
#ifdef SH_PURE_STUB_MODE
    return -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    type &= ~X_OK;
    return access(path, type);
# else
    return access(path, type);
# endif

#else
#endif
}

int shfile_isatty(shfdtab *pfdtab, int fd)
{
    int rc;

#ifdef SH_PURE_STUB_MODE
    rc = 0;
#elif defined(SH_STUB_MODE)
    rc = isatty(fd);
#else
#endif

    TRACE2((NULL, "isatty(%d) -> %d [%d]\n", fd, rc, errno));
    return rc;
}


int shfile_cloexec(shfdtab *pfdtab, int fd, int closeit)
{
    int rc;

#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    errno = ENOSYS;
    rc = -1;
# else
    rc = fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0)
                          | (closeit ? FD_CLOEXEC : 0));
# endif

#else
#endif

    TRACE2((NULL, "shfile_cloexec(%d, %d) -> %d [%d]\n", fd, closeit, rc, errno));
    return rc;
}


int shfile_ioctl(shfdtab *pfdtab, int fd, unsigned long request, void *buf)
{
    int rc;

#ifdef SH_PURE_STUB_MODE
    rc = -1;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    errno = ENOSYS;
    rc = -1;
# else
    rc = ioctl(fd, request, buf);
# endif

#else
#endif

    TRACE2((NULL, "ioctl(%d, %#x, %p) -> %d\n", fd, request, buf, rc));
    return rc;
}


mode_t shfile_get_umask(shfdtab *pfdtab)
{
#ifdef SH_PURE_STUB_MODE
    return 022;

#elif defined(SH_STUB_MODE)
    return 022;

#else
#endif
}


shdir *shfile_opendir(shfdtab *pfdtab, const char *dir)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    errno = ENOSYS;
    return NULL;
# else
    return (shdir *)opendir(dir);
# endif

#else
#endif
}

shdirent *shfile_readdir(struct shdir *pdir)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    errno = ENOSYS;
    return NULL;
# else
    struct dirent *pde = readdir((DIR *)pdir);
    return pde ? (shdirent *)&pde->d_name[0] : NULL;
# endif

#else
#endif
}

void shfile_closedir(struct shdir *pdir)
{
#ifdef SH_PURE_STUB_MODE
    return NULL;

#elif defined(SH_STUB_MODE)
# ifdef _MSC_VER
    errno = ENOSYS;
# else
    closedir((DIR *)pdir);
# endif

#else
#endif
}
