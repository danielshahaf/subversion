/* util.c --- utility functions for FSFS repo access
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <assert.h>

#include "svn_ctype.h"
#include "svn_dirent_uri.h"
#include "private/svn_string_private.h"

#include "fs_fs.h"
#include "pack.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

svn_boolean_t
svn_fs_fs__is_packed_rev(svn_fs_t *fs,
                         svn_revnum_t rev)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return (rev < ffd->min_unpacked_rev);
}

svn_boolean_t
svn_fs_fs__is_packed_revprop(svn_fs_t *fs,
                             svn_revnum_t rev)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* rev 0 will not be packed */
  return (rev < ffd->min_unpacked_rev)
      && (rev != 0)
      && (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT);
}

const char *
svn_fs_fs__path_txn_current(svn_fs_t *fs,
                            apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXN_CURRENT, pool);
}

const char *
svn_fs_fs__path_txn_current_lock(svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXN_CURRENT_LOCK, pool);
}

const char *
svn_fs_fs__path_lock(svn_fs_t *fs,
                     apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_LOCK_FILE, pool);
}

const char *
svn_fs_fs__path_revprop_generation(svn_fs_t *fs,
                                   apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_REVPROP_GENERATION, pool);
}

const char *
svn_fs_fs__path_rev_packed(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *kind,
                           apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  assert(svn_fs_fs__is_packed_rev(fs, rev));

  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool,
                                           "%ld" PATH_EXT_PACKED_SHARD,
                                           rev / ffd->max_files_per_dir),
                              kind, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_rev_shard(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool, "%ld",
                                                 rev / ffd->max_files_per_dir),
                              SVN_VA_NULL);
}

const char *
svn_fs_fs__path_rev(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(! svn_fs_fs__is_packed_rev(fs, rev));

  if (ffd->max_files_per_dir)
    {
      return svn_dirent_join(svn_fs_fs__path_rev_shard(fs, rev, pool),
                             apr_psprintf(pool, "%ld", rev),
                             pool);
    }

  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool, "%ld", rev), SVN_VA_NULL);
}

const char *
svn_fs_fs__path_rev_absolute(svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return (   ffd->format < SVN_FS_FS__MIN_PACKED_FORMAT
          || ! svn_fs_fs__is_packed_rev(fs, rev))
       ? svn_fs_fs__path_rev(fs, rev, pool)
       : svn_fs_fs__path_rev_packed(fs, rev, PATH_PACKED, pool);
}

const char *
svn_fs_fs__path_revprops_shard(svn_fs_t *fs,
                               svn_revnum_t rev,
                               apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld",
                                           rev / ffd->max_files_per_dir),
                              SVN_VA_NULL);
}

const char *
svn_fs_fs__path_revprops_pack_shard(svn_fs_t *fs,
                                    svn_revnum_t rev,
                                    apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld" PATH_EXT_PACKED_SHARD,
                                           rev / ffd->max_files_per_dir),
                              SVN_VA_NULL);
}

const char *
svn_fs_fs__path_revprops(svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->max_files_per_dir)
    {
      return svn_dirent_join(svn_fs_fs__path_revprops_shard(fs, rev, pool),
                             apr_psprintf(pool, "%ld", rev),
                             pool);
    }

  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld", rev), SVN_VA_NULL);
}

/* Return TO_ADD appended to the C string representation of TXN_ID.
 * Allocate the result in POOL.
 */
static const char *
combine_txn_id_string(const svn_fs_fs__id_part_t *txn_id,
                      const char *to_add,
                      apr_pool_t *pool)
{
  return apr_pstrcat(pool, svn_fs_fs__id_txn_unparse(txn_id, pool),
                     to_add, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_txn_dir(svn_fs_t *fs,
                        const svn_fs_fs__id_part_t *txn_id,
                        apr_pool_t *pool)
{
  SVN_ERR_ASSERT_NO_RETURN(txn_id != NULL);
  return svn_dirent_join_many(pool, fs->path, PATH_TXNS_DIR,
                              combine_txn_id_string(txn_id, PATH_EXT_TXN,
                                                    pool),
                              SVN_VA_NULL);
}

const char *
svn_fs_fs__path_txn_proto_rev(svn_fs_t *fs,
                              const svn_fs_fs__id_part_t *txn_id,
                              apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    return svn_dirent_join_many(pool, fs->path, PATH_TXN_PROTOS_DIR,
                                combine_txn_id_string(txn_id, PATH_EXT_REV,
                                                      pool),
                                SVN_VA_NULL);
  else
    return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                           PATH_REV, pool);
}

const char *
svn_fs_fs__path_txn_proto_rev_lock(svn_fs_t *fs,
                                   const svn_fs_fs__id_part_t *txn_id,
                                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    return svn_dirent_join_many(pool, fs->path, PATH_TXN_PROTOS_DIR,
                                combine_txn_id_string(txn_id,
                                                      PATH_EXT_REV_LOCK,
                                                      pool),
                                SVN_VA_NULL);
  else
    return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                           PATH_REV_LOCK, pool);
}

const char *
svn_fs_fs__path_txn_node_rev(svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             apr_pool_t *pool)
{
  char *filename = (char *)svn_fs_fs__id_unparse(id, pool)->data;
  *strrchr(filename, '.') = '\0';

  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, svn_fs_fs__id_txn_id(id),
                                                 pool),
                         apr_psprintf(pool, PATH_PREFIX_NODE "%s",
                                      filename),
                         pool);
}

const char *
svn_fs_fs__path_txn_node_props(svn_fs_t *fs,
                               const svn_fs_id_t *id,
                               apr_pool_t *pool)
{
  return apr_pstrcat(pool, svn_fs_fs__path_txn_node_rev(fs, id, pool),
                     PATH_EXT_PROPS, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_txn_node_children(svn_fs_t *fs,
                                  const svn_fs_id_t *id,
                                  apr_pool_t *pool)
{
  return apr_pstrcat(pool, svn_fs_fs__path_txn_node_rev(fs, id, pool),
                     PATH_EXT_CHILDREN, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_node_origin(svn_fs_t *fs,
                            const svn_fs_fs__id_part_t *node_id,
                            apr_pool_t *pool)
{
  char buffer[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__ui64tobase36(buffer, node_id->number);

  if (len > 1)
    buffer[len - 1] = '\0';

  return svn_dirent_join_many(pool, fs->path, PATH_NODE_ORIGINS_DIR,
                              buffer, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_min_unpacked_rev(svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_MIN_UNPACKED_REV, pool);
}

svn_error_t *
svn_fs_fs__check_file_buffer_numeric(const char *buf,
                                     apr_off_t offset,
                                     const char *path,
                                     const char *title,
                                     apr_pool_t *pool)
{
  const char *p;

  for (p = buf + offset; *p; p++)
    if (!svn_ctype_isdigit(*p))
      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
        _("%s file '%s' contains unexpected non-digit '%c' within '%s'"),
        title, svn_dirent_local_style(path, pool), *p, buf);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                                 svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  char buf[80];
  apr_file_t *file;
  apr_size_t len;

  SVN_ERR(svn_io_file_open(&file,
                           svn_fs_fs__path_min_unpacked_rev(fs, pool),
                           APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT,
                           pool));
  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  *min_unpacked_rev = SVN_STR_TO_REV(buf);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__update_min_unpacked_rev(svn_fs_t *fs,
                                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR_ASSERT(ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT);

  return svn_fs_fs__read_min_unpacked_rev(&ffd->min_unpacked_rev, fs, pool);
}

svn_error_t *
svn_fs_fs__write_min_unpacked_rev(svn_fs_t *fs,
                                  svn_revnum_t revnum,
                                  apr_pool_t *scratch_pool)
{
  const char *final_path;
  char buf[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__i64toa(buf, revnum);
  buf[len] = '\n';

  final_path = svn_fs_fs__path_min_unpacked_rev(fs, scratch_pool);

  SVN_ERR(svn_io_write_atomic(final_path, buf, len + 1,
                              final_path /* copy_perms */, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__write_current(svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_uint64_t next_node_id,
                         apr_uint64_t next_copy_id,
                         apr_pool_t *pool)
{
  char *buf;
  const char *name;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Now we can just write out this line. */
  if (ffd->format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
    {
      buf = apr_psprintf(pool, "%ld\n", rev);
    }
  else
    {
      char node_id_str[SVN_INT64_BUFFER_SIZE];
      char copy_id_str[SVN_INT64_BUFFER_SIZE];
      svn__ui64tobase36(node_id_str, next_node_id);
      svn__ui64tobase36(copy_id_str, next_copy_id);

      buf = apr_psprintf(pool, "%ld %s %s\n", rev, node_id_str, copy_id_str);
    }

  name = svn_fs_fs__path_current(fs, pool);
  SVN_ERR(svn_io_write_atomic(name, buf, strlen(buf),
                              name /* copy_perms_path */, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__try_stringbuf_from_file(svn_stringbuf_t **content,
                                   svn_boolean_t *missing,
                                   const char *path,
                                   svn_boolean_t last_attempt,
                                   apr_pool_t *pool)
{
  svn_error_t *err = svn_stringbuf_from_file2(content, path, pool);
  if (missing)
    *missing = FALSE;

  if (err)
    {
      *content = NULL;

      if (APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (!last_attempt)
            {
              svn_error_clear(err);
              if (missing)
                *missing = TRUE;
              return SVN_NO_ERROR;
            }
        }
#ifdef ESTALE
      else if (APR_TO_OS_ERROR(err->apr_err) == ESTALE
                || APR_TO_OS_ERROR(err->apr_err) == EIO)
        {
          if (!last_attempt)
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
        }
#endif
    }

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__get_file_offset(apr_off_t *offset_p,
                           apr_file_t *file,
                           apr_pool_t *pool)
{
  apr_off_t offset;

  /* Note that, for buffered files, one (possibly surprising) side-effect
     of this call is to flush any unwritten data to disk. */
  offset = 0;
  SVN_ERR(svn_io_file_seek(file, APR_CUR, &offset, pool));
  *offset_p = offset;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_content(svn_stringbuf_t **content,
                        const char *fname,
                        apr_pool_t *pool)
{
  int i;
  *content = NULL;

  for (i = 0; !*content && (i < SVN_FS_FS__RECOVERABLE_RETRY_COUNT); ++i)
    SVN_ERR(svn_fs_fs__try_stringbuf_from_file(content, NULL,
                        fname, i + 1 < SVN_FS_FS__RECOVERABLE_RETRY_COUNT,
                        pool));

  if (!*content)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't read '%s'"),
                             svn_dirent_local_style(fname, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_number_from_stream(apr_int64_t *result,
                                   svn_boolean_t *hit_eof,
                                   svn_stream_t *stream,
                                   apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *sb;
  svn_boolean_t eof;
  svn_error_t *err;

  SVN_ERR(svn_stream_readline(stream, &sb, "\n", &eof, scratch_pool));
  if (hit_eof)
    *hit_eof = eof;
  else
    if (eof)
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL, _("Unexpected EOF"));

  if (!eof)
    {
      err = svn_cstring_atoi64(result, sb->data);
      if (err)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                                 _("Number '%s' invalid or too large"),
                                 sb->data);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__move_into_place(const char *old_filename,
                           const char *new_filename,
                           const char *perms_reference,
                           apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR(svn_io_copy_perms(perms_reference, old_filename, pool));

  /* Move the file into place. */
  err = svn_io_file_rename(old_filename, new_filename, pool);
  if (err && APR_STATUS_IS_EXDEV(err->apr_err))
    {
      apr_file_t *file;

      /* Can't rename across devices; fall back to copying. */
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      SVN_ERR(svn_io_copy_file(old_filename, new_filename, TRUE, pool));

      /* Flush the target of the copy to disk. */
      SVN_ERR(svn_io_file_open(&file, new_filename, APR_READ,
                               APR_OS_DEFAULT, pool));
      /* ### BH: Does this really guarantee a flush of the data written
         ### via a completely different handle on all operating systems?
         ###
         ### Maybe we should perform the copy ourselves instead of making
         ### apr do that and flush the real handle? */
      SVN_ERR(svn_io_file_flush_to_disk(file, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }
  if (err)
    return svn_error_trace(err);

#ifdef __linux__
  {
    /* Linux has the unusual feature that fsync() on a file is not
       enough to ensure that a file's directory entries have been
       flushed to disk; you have to fsync the directory as well.
       On other operating systems, we'd only be asking for trouble
       by trying to open and fsync a directory. */
    const char *dirname;
    apr_file_t *file;

    dirname = svn_dirent_dirname(new_filename, pool);
    SVN_ERR(svn_io_file_open(&file, dirname, APR_READ, APR_OS_DEFAULT,
                             pool));
    SVN_ERR(svn_io_file_flush_to_disk(file, pool));
    SVN_ERR(svn_io_file_close(file, pool));
  }
#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open_pack_or_rev_file(apr_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;
  svn_boolean_t retry = FALSE;

  do
    {
      const char *path = svn_fs_fs__path_rev_absolute(fs, rev, pool);

      /* open the revision file in buffered r/o mode */
      err = svn_io_file_open(file, path,
                            APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
            {
              /* Could not open the file. This may happen if the
               * file once existed but got packed later. */
              svn_error_clear(err);

              /* if that was our 2nd attempt, leave it at that. */
              if (retry)
                return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                         _("No such revision %ld"), rev);

              /* We failed for the first time. Refresh cache & retry. */
              SVN_ERR(svn_fs_fs__update_min_unpacked_rev(fs, pool));

              retry = TRUE;
            }
          else
            {
              svn_error_clear(err);
              return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                       _("No such revision %ld"), rev);
            }
        }
      else
        {
          retry = FALSE;
        }
    }
  while (retry);

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__item_offset(apr_off_t *absolute_position,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_off_t offset,
                       apr_pool_t *pool)
{
  if (svn_fs_fs__is_packed_rev(fs, rev))
    {
      apr_off_t rev_offset;
      SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, rev, pool));
      *absolute_position = rev_offset + offset;
    }
  else
    {
      *absolute_position = offset;
    }

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs_fs__supports_move(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return ffd->format >= SVN_FS_FS__MIN_MOVE_SUPPORT_FORMAT;
}
