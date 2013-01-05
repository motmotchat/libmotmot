/**
 * readfile.h - Utility routine for reading an entire file into memory.
 */
#ifndef __MOTMOT_READFILE_H__
#define __MOTMOT_READFILE_H__

/**
 * readfile - Read an entire file into memory.
 *
 * Either the full file is read into a newly-allocated buffer, or NULL is
 * returned.  Callers may pass NULL for size, in which case the size of the
 * file is simply not returned.
 *
 * @param path    The location of the file.
 * @param size    The size of the file read.
 * @return        A fresh buffer containing the full contents of the file.
 */
char *readfile(const char *path, size_t *size);

#endif /* __MOTMOT_READFILE_H__ */
