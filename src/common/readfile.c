/**
 * readfile.c - Read an entire file into a fresh buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

char *
readfile(const char *path, size_t *size)
{
  FILE *f;
  struct stat sb;
  char *buf;

  if (stat(path, &sb)) {
    return NULL;
  }

  buf = malloc(sb.st_size + 1);
  if (buf == NULL) {
    return NULL;
  }
  buf[sb.st_size] = '\0';

  f = fopen(path, "r");
  if (f == NULL) {
    free(buf);
    return NULL;
  }

  if (fread(buf, 1, sb.st_size, f) != sb.st_size) {
    free(buf);
    buf = NULL;
  }

  if (size != NULL) {
    *size = sb.st_size;
  }

  fclose(f);
  return buf;
}
