#ifndef SWR_TRACE_ZIP_H
#define SWR_TRACE_ZIP_H

/* Create a portable, store-only ZIP archive beside a completed trace folder.
   The source folder is retained. */
int trace_zip_create(const char* directory, char* archive, unsigned archive_size);

#endif
