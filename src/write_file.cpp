#include "r_archive.h"
#include <fcntl.h>

#if ARCHIVE_VERSION_NUMBER >= 3002000
struct file {
  archive* ar;
  archive_entry* entry;
  std::string filename;
  int filters[FILTER_MAX];
};

/* callback function to store received data */
static size_t
file_write_data(const void* contents, size_t sz, size_t n, Rconnection ctx) {
  file* r = (file*)ctx->private_ptr;

  size_t realsize = sz * n;
  archive_write_data(r->ar, contents, realsize);

  return n;
}

static Rboolean file_write_open(Rconnection con) {
  file* r = (file*)con->private_ptr;

  r->ar = archive_write_new();

  for (int i = 0; i < FILTER_MAX && r->filters[i] != -1; ++i) {
    int ret = archive_write_add_filter(r->ar, r->filters[i]);
    if (ret == ARCHIVE_FATAL) {
      Rf_error("%i", r->filters);
      Rf_error(archive_error_string(r->ar));
    }
  }
  archive_write_set_format_raw(r->ar);
  archive_write_open_filename(r->ar, r->filename.c_str());

  r->entry = archive_entry_new();

  archive_entry_set_pathname(r->entry, r->filename.c_str());
  archive_entry_set_filetype(r->entry, AE_IFREG);
  archive_entry_set_perm(r->entry, 0644);
  archive_write_header(r->ar, r->entry);

  con->isopen = TRUE;
  return TRUE;
}

/* This function closes the temporary scratch file, then writes the actual
 * archive file based on the archive filename given and then unlinks the
 * scratch file */
void file_write_close(Rconnection con) {
  file* r = (file*)con->private_ptr;

  archive_entry_free(r->entry);
  archive_write_close(r->ar);
  archive_write_free(r->ar);
}

void file_write_destroy(Rconnection con) {
  file* r = (file*)con->private_ptr;

  /* free the handle connection */
  delete r;
}
#endif

// Get a connection to a single non-archive file, optionally with one or more
// filters.
[[cpp11::register]] SEXP
write_file_connection(const std::string& filename, cpp11::integers filters) {
#if ARCHIVE_VERSION_NUMBER < 3002000
  cpp11::stop("This functionality is only available with libarchive >= 3.2.0");
#else
  Rconnection con;
  SEXP rc = PROTECT(new_connection("input", "wb", "archive_write", &con));

  /* Setup file */
  file* r = new file;

  r->filename = filename;

  /* Initialize filters */
  if (filters.size() > FILTER_MAX) {
    cpp11::stop("Cannot use more than %i filters", FILTER_MAX);
  }
  for (int i = 0; i < FILTER_MAX; ++i) {
    r->filters[i] = -1;
  }
  for (int i = 0; i < filters.size(); ++i) {
    r->filters[i] = filters[i];
  }

  /* set connection properties */
  con->incomplete = TRUE;
  con->private_ptr = r;
  con->canread = FALSE;
  con->canseek = FALSE;
  con->canwrite = TRUE;
  con->isopen = FALSE;
  con->blocking = TRUE;
  con->text = FALSE;
  con->open = file_write_open;
  con->close = file_write_close;
  con->destroy = file_write_destroy;
  con->write = file_write_data;

  UNPROTECT(1);
  return rc;
#endif
}

// Write files already on disk to a new archive
[[cpp11::register]] SEXP write_files_(
    const std::string& archive_filename,
    cpp11::strings files,
    int format,
    cpp11::integers filters,
    size_t sz = 16384) {

  struct archive* a;
  struct archive_entry* entry;
  struct stat st;
  char buff[8192];
  int len;
  int fd;
  int response;

  a = archive_write_new();

  response = archive_write_set_format(a, format);
  if (response != ARCHIVE_OK) {
    Rf_error(archive_error_string(a));
  }

  for (int i = 0; i < filters.size(); ++i) {
    response = archive_write_add_filter(a, filters[i]);
    if (response != ARCHIVE_OK) {
      Rf_error(archive_error_string(a));
    }
  }

  archive_write_open_filename(a, archive_filename.c_str());
  for (std::string file : files) {
    stat(file.c_str(), &st);
    entry = archive_entry_new();
    archive_entry_copy_stat(entry, &st);
    archive_entry_set_pathname(entry, file.c_str());
    archive_write_header(a, entry);
    if ((fd = open(file.c_str(), O_RDONLY)) != -1) {
      len = read(fd, buff, sizeof(buff));
      while (len > 0) {
        archive_write_data(a, buff, len);
        len = read(fd, buff, sizeof(buff));
      }
      close(fd);
    }
    archive_entry_free(entry);
  }
  archive_write_free(a);

  return R_NilValue;
}
