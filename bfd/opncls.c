/* Copyright (C) 1990, 1991 Free Software Foundation, Inc.

This file is part of BFD, the Binary File Diddler.

BFD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

BFD is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with BFD; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* $Id$ */

/*** opncls.c -- open and close a bfd. */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"


extern void bfd_cache_init();
FILE *bfd_open_file();

/* fdopen is a loser -- we should use stdio exclusively.  Unfortunately
   if we do that we can't use fcntl.  */

/** Locking 

   Locking is loosely controlled by the preprocessor variable
   BFD_LOCKS.  I say loosely because Unix barely understands locking
   -- at least in BSD it doesn't affect programs which don't
   explicitly use it!  That is to say it's practically useless, though
   if everyone uses this library you'll be OK.

   From among the many and varied lock facilities available, (none of
   which, of course, knows about any other) we use the fcntl locks,
   because they're Posix.

   The reason that bfd_openr and bfd_fdopenr exist, yet only bfd_openw
   exists is because of locking.  When we do output, we lock the
   filename file for output, then open a temporary file which does not
   actually get its correct filename until closing time.  This is
   safest, but requires the asymmetry in read and write entry points.

   Perhaps, since unix has so many different kinds of locking anyway,
   we should use the emacs lock scheme?... */


bfd *new_bfd()
{
  bfd *nbfd = (bfd *)zalloc(sizeof(bfd));

  nbfd->direction = no_direction;
  nbfd->iostream = NULL;
  nbfd->where = 0;
  nbfd->sections = (asection *)NULL;
  nbfd->format = bfd_unknown;
  nbfd->my_archive = (bfd *)NULL;
  nbfd->origin = 0;				   
  nbfd->opened_once = false;
  nbfd->output_has_begun = false;
  nbfd->section_count = 0;
  nbfd->usrdata = (PTR)NULL;
  nbfd->sections = (asection *)NULL;
  nbfd->cacheable = false;
  nbfd->flags = NO_FLAGS;
  return nbfd;
}
bfd *new_bfd_contained_in(obfd)
bfd *obfd;
{
	bfd *nbfd = new_bfd();
	nbfd->xvec = obfd->xvec;
	nbfd->my_archive = obfd;
	nbfd->direction = read_direction;
	return nbfd;
}

/** bfd_openr, bfd_fdopenr -- open for reading.
  Returns a pointer to a freshly-allocated bfd on success, or NULL. */

bfd *
DEFUN(bfd_openr, (filename, target),
      CONST char *filename AND
      CONST char *target)
{
  bfd *nbfd;
  bfd_target *target_vec;

  target_vec = bfd_find_target (target);
  if (target_vec == NULL) {
    bfd_error = invalid_target;
    return NULL;
  }

  bfd_error = system_call_error;
  nbfd = new_bfd();
  if (nbfd == NULL) {
    bfd_error = no_memory;
    return NULL;
  }

  nbfd->filename = filename;
  nbfd->xvec = target_vec;
  nbfd->direction = read_direction; 

  if (bfd_open_file (nbfd) == NULL) {
    bfd_error = system_call_error;	/* File didn't exist, or some such */
    free (nbfd);
    return NULL;
  }
  return nbfd;
}


/* Don't try to `optimize' this function:

   o - We lock using stack space so that interrupting the locking
       won't cause a storage leak.
   o - We open the file stream last, since we don't want to have to
       close it if anything goes wrong.  Closing the stream means closing
       the file descriptor too, even though we didn't open it.
 */

bfd *
DEFUN(bfd_fdopenr,(filename, target, fd),
      CONST char *filename AND
      CONST char *target AND
      int fd)
{
  bfd *nbfd;
  bfd_target *target_vec;
  int fdflags;
#ifdef BFD_LOCKS
  struct flock lock, *lockp = &lock;
#endif

  target_vec = bfd_find_target (target);
  if (target_vec == NULL) {
    bfd_error = invalid_target;
    return NULL;
  }

  bfd_error = system_call_error;
  
  fdflags = fcntl (fd, F_GETFL);
  if (fdflags == -1) return NULL;

#ifdef BFD_LOCKS
  lockp->l_type = F_RDLCK;
  if (fcntl (fd, F_SETLKW, lockp) == -1) return NULL;
#endif

  nbfd = new_bfd();

  if (nbfd == NULL) {
    bfd_error = no_memory;
    return NULL;
  }
#ifdef BFD_LOCKS
  nbfd->lock = (struct flock *) (nbfd + 1);
#endif
  /* if the fd were open for read only, this still would not hurt: */
  nbfd->iostream = (char *) fdopen (fd, "r+"); 
  if (nbfd->iostream == NULL) {
    free (nbfd);
    return NULL;
  }
  
  /* OK, put everything where it belongs */

  nbfd->filename = filename;
  nbfd->xvec = target_vec;

  /* As a special case we allow a FD open for read/write to
     be written through, although doing so requires that we end
     the previous clause with a preposition.  */
  switch (fdflags & O_ACCMODE) {
  case O_RDONLY: nbfd->direction = read_direction; break;
  case O_WRONLY: nbfd->direction = write_direction; break;  
  case O_RDWR: nbfd->direction = both_direction; break;
  default: abort ();
  }
				   
#ifdef BFD_LOCKS
  memcpy (nbfd->lock, lockp, sizeof (struct flock))
#endif

    bfd_cache_init (nbfd);

  return nbfd;
}

/** bfd_openw -- open for writing.
  Returns a pointer to a freshly-allocated bfd on success, or NULL.

  See comment by bfd_fdopenr before you try to modify this function. */

bfd *
DEFUN(bfd_openw,(filename, target),
      CONST char *filename AND
      CONST char *target)
{
  bfd *nbfd;
  bfd_target *target_vec;
  
  target_vec = bfd_find_target (target);
  if (target_vec == NULL) return NULL;

  bfd_error = system_call_error;

  /* nbfd has to point to head of malloc'ed block so that bfd_close may
     reclaim it correctly. */

  nbfd = new_bfd();
  if (nbfd == NULL) {
    bfd_error = no_memory;
    return NULL;
  }

  nbfd->filename = filename;
  nbfd->xvec = target_vec;
  nbfd->direction = write_direction;

  if (bfd_open_file (nbfd) == NULL) {
    bfd_error = system_call_error;	/* File not writeable, etc */
    free (nbfd);
    return NULL;
  }
  return nbfd;
}



/** Close up shop, get your deposit back. */
boolean
bfd_close (abfd)
     bfd *abfd;
{
  if (BFD_SEND (abfd, _close_and_cleanup, (abfd)) != true) return false;

  bfd_cache_close(abfd);
/* If the file was open for writing and is now executable
  make it so */
  if (abfd->direction == write_direction 
      && abfd->flags & EXEC_P) {
    struct stat buf;
    stat(abfd->filename, &buf);
    chmod(abfd->filename,buf.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
  }
  free (abfd);
  return true;
}
/* 
 called to create a bfd with no ascociated file or target 
 */
bfd *
DEFUN(bfd_create,(filename, template),
      CONST char *filename AND
      CONST bfd *template)
{
  bfd *nbfd = new_bfd();
  if (nbfd == (bfd *)NULL) {
    bfd_error = no_memory;
    return (bfd *)NULL;
  }
  nbfd->filename = filename;
  nbfd->xvec = template->xvec;
  nbfd->direction = no_direction;
  return nbfd;



}
