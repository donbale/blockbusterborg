/*
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.



  BlockFuse: See 2010-12-21 entry: http://www.globallinuxsecurity.pro/blog.php

	Started from Miklos's hello.c:
		Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	BlockFuse contributions:
		Copyright (C) 2010-2011 Eric Wheeler <blockfuse@ew.ewheeler.org>

  gcc -Wall `pkg-config fuse --cflags --libs` block-fuse.c -o block-fuse
*/

#define _GNU_SOURCE
#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 500	/* pread */

#define DEBUG "/tmp/block-fuse.log"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fs.h>


#include <limits.h>
#include <stddef.h>
#include <dirent.h>

/* TODO convert mmap* and fd into union. */
typedef struct
{
  char *mmap_ptr;
  size_t mmap_length;		/* should be size_t really... */
  int mmap_mode;		/* if yes - use mmap-operations. somtimes it unavailable...reverting to read() bahaviour */
  int fd;
} fd_info;

#ifdef DEBUG
FILE *logfile;
#endif

#define MAX_PATH (4096)

static char *ROOT = NULL;
time_t MOUNT_TIME;

static int blockfuse_getattr (const char *path, struct stat *stbuf)
{
  char spath[MAX_PATH + 1] = "";
  struct stat mystbuf;
  int fd;

  memset (stbuf, 0, sizeof (struct stat));

  if (strcmp (path, "/") == 0)
  {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    stbuf->st_atime = MOUNT_TIME;
    stbuf->st_ctime = MOUNT_TIME;
    stbuf->st_mtime = MOUNT_TIME;
    stbuf->st_blksize = 4096;
    return 0;
  }

  snprintf (spath, sizeof (spath) - 1, "%s/%s", ROOT, path);

  fd = open (spath, O_RDONLY);
  if (fd < 0)
  {
    return -errno;
  }

  if (fstat (fd, &mystbuf) < 0)
  {
    close (fd);
    return -errno;
  }

  if (!S_ISBLK (mystbuf.st_mode))
  {
    close (fd);
    return -ENOTBLK;
  }

  if (ioctl (fd, BLKGETSIZE64, &stbuf->st_size) < 0)
  {
    close (fd);
    return -errno;
  }

  close (fd);

  /* losetup devices */
  if (stbuf->st_size == 0)
  {
    return -ENODEV;
  }


  stbuf->st_uid = mystbuf.st_uid;
  stbuf->st_gid = mystbuf.st_gid;
  stbuf->st_blksize = 512;
  stbuf->st_blocks = stbuf->st_size / 512;
  stbuf->st_mode =
    S_IFREG | (mystbuf.st_mode & (S_IRUSR | S_IRGRP | S_IROTH));
  stbuf->st_nlink = 1;
  stbuf->st_atime = mystbuf.st_atime;
  stbuf->st_ctime = mystbuf.st_ctime;
  stbuf->st_mtime = mystbuf.st_mtime;

  return 0;
}

static int blockfuse_readdir (const char *path, void *buf,
			      fuse_fill_dir_t filler, off_t offset,
			      struct fuse_file_info *fi)
{
  struct dirent *de, *alloc_de;
  DIR *dp;
  char spath[MAX_PATH + 1];
  struct stat stbuf;

  (void) offset;
  (void) fi;

  snprintf (spath, sizeof (spath) - 1, "%s/%s", ROOT, path);

  dp = opendir (ROOT);
  if (!dp)
  {
    return -errno;
  }

  filler (buf, ".", NULL, 0);
  filler (buf, "..", NULL, 0);

  /* see man readdir_r */
  alloc_de =
    alloca (offsetof (struct dirent, d_name) +pathconf (ROOT, _PC_NAME_MAX) +
	    1);

  while (!readdir_r (dp, alloc_de, &de) && de)
  {
    switch (de->d_type)
    {
    case DT_BLK:
      filler (buf, de->d_name, NULL, 0);
      break;
    case DT_LNK:
      snprintf (spath, sizeof (spath) - 1, "%s/%s", ROOT, de->d_name);
      /* note, stat(), not lstat() */
      if (stat (spath, &stbuf) >= 0 && S_ISBLK (stbuf.st_mode))
      {
	filler (buf, de->d_name, NULL, 0);
      }
      break;
    }
  }
  closedir (dp);
  return 0;
}

static int blockfuse_release (const char *path, struct fuse_file_info *fi)
{
  fd_info *our_info = (fd_info *) (unsigned long) (fi->fh);

  (void) path;

  if (!our_info)
    return 0;

  if (our_info->mmap_mode)
  {
    munmap (our_info->mmap_ptr, our_info->mmap_length);
  }
  else
  {
    close (our_info->fd);
  }
  free (our_info);
  fi->fh = 0;
  return 0;
}

static int blockfuse_open (const char *path, struct fuse_file_info *fi)
{
  char spath[MAX_PATH + 1] = "";
  struct stat stbuf;
  fd_info our_info, *pinfo;

  snprintf (spath, sizeof (spath) - 1, "%s/%s", ROOT, path);

  if ((fi->flags & 3) != O_RDONLY)
    return -EACCES;

  memset (&our_info, 0, sizeof (our_info));

  our_info.fd = open (spath, O_RDONLY);
  if (our_info.fd < 0)
  {
    return -errno;
  }

  if (fstat (our_info.fd, &stbuf) < 0)
  {
    close (our_info.fd);
    return -errno;
  }
  if (!S_ISBLK (stbuf.st_mode))
  {
    close (our_info.fd);
    return -EINVAL;
  }
  if (ioctl (our_info.fd, BLKGETSIZE64, &stbuf.st_size) < 0)
  {
    close (our_info.fd);
    return -errno;
  }

  pinfo = malloc (sizeof (our_info));
  if (!pinfo)
  {
    close (our_info.fd);
    return -ENOMEM;
  }

  /* may not happen on 32-bit system and large block devices... */
  if (stbuf.st_size <= SSIZE_MAX)
  {
    our_info.mmap_length = (size_t) stbuf.st_size;
    our_info.mmap_ptr = mmap (NULL, our_info.mmap_length,
			      PROT_READ, MAP_PRIVATE, our_info.fd, 0);
    if (our_info.mmap_ptr == MAP_FAILED)
    {
#ifdef DEBUG
      fprintf (logfile, "mmap: %p/%d/%jd\n", our_info.mmap_ptr, our_info.fd,
	       stbuf.st_size);
#endif
    }
    else
    {
      our_info.mmap_mode = 1;
      close (our_info.fd);
      our_info.fd = -1;
    }
  }

#if 0
  if (our_info.mmap_mode)
  {
    madvise (our_info.mmap_ptr, our_info.mmap_length, MADV_RANDOM);
  }
  else
  {
    posix_fadvise (our_info.fd, 0, our_info.mmap_length, POSIX_FADV_RANDOM);
  }
#endif

  *pinfo = our_info;
  fi->fh = (uint64_t) (unsigned long) pinfo;
  return 0;
}

static int blockfuse_read (const char *path, char *buf, size_t size,
			   off_t offset, struct fuse_file_info *fi)
{
  fd_info *our_info = (fd_info *) (unsigned long) (fi->fh);
  off_t len;

  (void) path;

  if (!our_info)
    return -ENOENT;

  if (!our_info->mmap_mode)
    return pread (our_info->fd, buf, size, offset);

  if ((size_t) offset > our_info->mmap_length)
    return -EINVAL;

  if ((size_t) offset == our_info->mmap_length || size == 0)
    return 0;

  /* Adjust size in case it exceeds the mmap region. */
  if (offset + size > our_info->mmap_length)
    len = our_info->mmap_length - offset;
  else
    len = size;

#ifdef DEBUG
  fprintf (logfile, "read size=%zu, len=%jd, offset=%jd, dev_size=%zu\n",
	   size, len, offset, our_info->mmap_length);

#endif

  memcpy (buf, our_info->mmap_ptr + offset, len);
  return len;
}

static struct fuse_operations blockfuse_oper = {
  .getattr = blockfuse_getattr,	/* */
  .readdir = blockfuse_readdir,	/* */
  .open = blockfuse_open,	/* */
  .release = blockfuse_release,	/* */
  .read = blockfuse_read,	/* */
};

int main (int argc, char *argv[])
{
  char *args[3];
  MOUNT_TIME = time (0);

  if (argc < 3)
  {
    fprintf (stderr, "usage: %s /dev/directory /mnt/point\n", argv[0]);
    exit (1);
  }

  if (argv[1][0] != '/' || argv[2][0] != '/')
  {
    fprintf (stderr, "Error: directory paths must be absolute.\n");
    exit (1);
  }

#ifdef DEBUG
  logfile = fopen (DEBUG, "at");

  if (!logfile)
  {
    fprintf (stderr, "/tmp/block-fuse.log\n");
    return 1;
  }

  fprintf (logfile, "starting.\n");
#endif

  ROOT = argv[1];
  args[0] = argv[0];
  args[1] = argv[2];
  args[2] = NULL;
  return fuse_main (2, args, &blockfuse_oper, NULL);
}
