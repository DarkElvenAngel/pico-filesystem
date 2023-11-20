/* pfs_ffs.c - A PFS filesystem using LFS on the Pico flash memory */
// Copyright (c) 2023, Memotech-Bill
// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <fcntl.h>
#include <pfs_private.h>
#include <lfs.h>

#ifndef STATIC
#define STATIC  static
#endif

STATIC struct pfs_file *ffs_open (struct pfs_pfs *pfs, const char *fn, int oflag);
STATIC int ffs_close (struct pfs_file *pfs_fd);
STATIC int ffs_read (struct pfs_file *pfs_fd, char *buffer, int length);
STATIC int ffs_write (struct pfs_file *pfs_fd, char *buffer, int length);
STATIC long ffs_lseek (struct pfs_file *pfs_fd, long pos, int whence);
STATIC int ffs_fstat (struct pfs_file *pfs_fd, struct stat *buf);
STATIC int ffs_isatty (struct pfs_file *fd);
STATIC int ffs_stat (struct pfs_pfs *pfs, const char *name, struct stat *buf);
STATIC int ffs_rename (struct pfs_pfs *pfs, const char *old, const char *new);
STATIC int ffs_delete (struct pfs_pfs *pfs, const char *name);
STATIC int ffs_mkdir (struct pfs_pfs *pfs, const char *pathname, mode_t mode);
STATIC int ffs_rmdir (struct pfs_pfs *pfs, const char *pathname);
STATIC void *ffs_opendir (struct pfs_pfs *pfs, const char *name);
STATIC struct dirent *ffs_readdir (void *dirp);
STATIC int ffs_closedir (void *dirp);
STATIC int ffs_chmod (struct pfs_pfs *pfs, const char *pathname, mode_t mode);

STATIC const struct pfs_v_pfs ffs_v_pfs =
    {
    ffs_open,
    ffs_stat,
    ffs_rename,
    ffs_delete,
    ffs_mkdir,
    ffs_rmdir,
    ffs_opendir,
    ffs_chmod
    };
    
STATIC const struct pfs_v_file ffs_v_file =
    {
    ffs_close,
    ffs_read,
    ffs_write,
    ffs_lseek,
    ffs_fstat,
    NULL,           // isatty
    NULL            // ioctl
    };

STATIC const struct pfs_v_dir ffs_v_dir =
    {
    ffs_readdir,
    ffs_closedir,
    };

struct ffs_pfs
    {
    const struct pfs_v_pfs *    entry;
    lfs_t                       base;
    struct lfs_config           cfg;
    };

struct ffs_file
    {
    const struct pfs_v_file *   entry;
    struct ffs_pfs *            ffs;
    const char *                pn;
    lfs_file_t                  ft;
    };

struct ffs_dir
    {
    const struct pfs_v_dir *    entry;
    struct ffs_pfs *            ffs;
    int                         flags;
    struct pfs_mount *          m;
    struct dirent               de;
    lfs_dir_t                   dt;
    };

STATIC struct pfs_file *ffs_open (struct pfs_pfs *pfs, const char *fn, int oflag)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *) pfs;
    struct ffs_file *fd = (struct ffs_file *)pfs_malloc (sizeof (struct ffs_file));
    if ( fd == NULL )
        {
        pfs_error (ENOMEM);
        return NULL;
        }
    fd->entry = &ffs_v_file;
    fd->ffs = ffs;
    enum lfs_open_flags of = 0;
    switch ( oflag & O_ACCMODE )
        {
        case O_RDONLY: of = LFS_O_RDONLY; break;
        case O_WRONLY: of = LFS_O_WRONLY; break;
        case O_RDWR:   of = LFS_O_RDWR;   break;
        }
    if ( oflag & O_APPEND ) of |= LFS_O_APPEND;
    if ( oflag & O_CREAT )  of |= LFS_O_CREAT;
    if ( oflag & O_TRUNC )  of |= LFS_O_TRUNC;
    int r = lfs_file_open (&ffs->base, &fd->ft, fn, of);
    if ( r >= 0 ) return (struct pfs_file *) fd;
    pfs_error (r);
   pfs_free (fd);
    return NULL;
    }

STATIC int ffs_close (struct pfs_file *pfs_fd)
    {
    struct ffs_file *fd = (struct ffs_file *) pfs_fd;
    struct ffs_pfs *ffs = fd->ffs;
    return pfs_error (lfs_file_close (&ffs->base, &fd->ft));
    }

STATIC int ffs_read (struct pfs_file *pfs_fd, char *buffer, int length)
    {
    struct ffs_file *fd = (struct ffs_file *) pfs_fd;
    struct ffs_pfs *ffs = fd->ffs;
    int r = lfs_file_read (&ffs->base, &fd->ft, buffer, length);
    return ( r >= 0 ) ? r : pfs_error (r);
    }

STATIC int ffs_write (struct pfs_file *pfs_fd, char *buffer, int length)
    {
    struct ffs_file *fd = (struct ffs_file *) pfs_fd;
    struct ffs_pfs *ffs = fd->ffs;
    int r = lfs_file_write (&ffs->base, &fd->ft, buffer, length);
    return ( r >= 0 ) ? r : pfs_error (r);
    }

STATIC long ffs_lseek (struct pfs_file *pfs_fd, long pos, int whence)
    {
    struct ffs_file *fd = (struct ffs_file *) pfs_fd;
    struct ffs_pfs *ffs = fd->ffs;
    switch (whence)
        {
        case SEEK_SET: whence = LFS_SEEK_SET; break;
        case SEEK_CUR: whence = LFS_SEEK_CUR; break;
        case SEEK_END: whence = LFS_SEEK_END; break;
        }
    int r = lfs_file_seek (&ffs->base, &fd->ft, pos, whence);
    return ( r >= 0 ) ? r : pfs_error (r);
    }

STATIC int ffs_fstat (struct pfs_file *pfs_fd, struct stat *buf)
    {
    return ffs_stat (pfs_fd->pfs, pfs_fd->pn, buf);
    }

STATIC int ffs_isatty (struct pfs_file *fd)
    {
    return 0;
    }

STATIC int ffs_stat (struct pfs_pfs *pfs, const char *name, struct stat *buf)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *) pfs;
    struct lfs_info info;
    int r = lfs_stat (&ffs->base, name, &info);
    if ( r < 0 ) return pfs_error (r);
    memset (buf, 0, sizeof (struct stat));
    buf->st_size = info.size;
    buf->st_blksize = 1;
    buf->st_blocks = info.size;
    buf->st_nlink = 1;
    buf->st_mode = S_IRWXU | S_IRWXG | S_IRWXO;
    if ( info.type == LFS_TYPE_DIR ) buf->st_mode |= S_IFDIR;
    else buf->st_mode |= S_IFREG;
    return 0;
    }
    

STATIC int ffs_rename (struct pfs_pfs *pfs, const char *old, const char *new)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *) pfs;
    return pfs_error (lfs_rename (&ffs->base, old, new));
    }

STATIC int ffs_delete (struct pfs_pfs *pfs, const char *name)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *) pfs;
    return pfs_error (lfs_remove (&ffs->base, name));
    }

STATIC int ffs_mkdir (struct pfs_pfs *pfs, const char *pathname, mode_t mode)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *) pfs;
    return pfs_error (lfs_mkdir (&ffs->base, pathname));
    }

STATIC int ffs_rmdir (struct pfs_pfs *pfs, const char *pathname)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *) pfs;
    return pfs_error (lfs_remove (&ffs->base, pathname));
    }

STATIC void *ffs_opendir (struct pfs_pfs *pfs, const char *name)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *) pfs;
    struct ffs_dir *dd = (struct ffs_dir *)pfs_malloc (sizeof (struct ffs_dir));
    if ( dd == NULL )
        {
        pfs_error (ENOMEM);
        return NULL;
        }
    dd->entry = &ffs_v_dir;
    dd->ffs = ffs;
    if ( lfs_dir_open (&ffs->base, &dd->dt, name) >= 0 ) return (void *) dd;
   pfs_free (dd);
    return NULL;
    }

STATIC struct dirent *ffs_readdir (void *dirp)
    {
    struct ffs_dir *dd = (struct ffs_dir *) dirp;
    struct ffs_pfs *ffs = dd->ffs;
    struct lfs_info info;
    int r = lfs_dir_read (&ffs->base, &dd->dt, &info);
    if ( r < 0 ) pfs_error (r);
    if ( r <= 0 ) return NULL;
    strncpy (dd->de.d_name, info.name, NAME_MAX);
    return &dd->de;
    }

STATIC int ffs_closedir (void *dirp)
    {
    struct ffs_dir *dd = (struct ffs_dir *) dirp;
    struct ffs_pfs *ffs = dd->ffs;
    return pfs_error (lfs_dir_close (&ffs->base, &dd->dt));
    }

STATIC int ffs_chmod (struct pfs_pfs *pfs, const char *pathname, mode_t mode)
    {
    return pfs_error (EINVAL);
    }

struct pfs_pfs *pfs_ffs_create (const struct lfs_config *cfg)
    {
    struct ffs_pfs *ffs = (struct ffs_pfs *)pfs_malloc (sizeof (struct ffs_pfs));
    if ( ffs == NULL ) return NULL;
    ffs->entry = &ffs_v_pfs;
    memcpy (&ffs->cfg, cfg, sizeof (struct lfs_config));
    int r = lfs_mount (&ffs->base, &ffs->cfg);
    if ( r < 0 )
        {
        r = lfs_format (&ffs->base, &ffs->cfg);
        if ( r == 0 ) r = lfs_mount (&ffs->base, &ffs->cfg);
        }
    if ( r < 0 )
        {
       pfs_free (ffs);
        return NULL;
        }
    return (struct pfs_pfs *) ffs;
    }
