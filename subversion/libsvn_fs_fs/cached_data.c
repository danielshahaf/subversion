/* cached_data.c --- cached (read) access to FSFS data
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

#include "cached_data.h"

#include <assert.h>

#include "svn_hash.h"
#include "svn_ctype.h"
#include "private/svn_temp_serializer.h"

#include "fs_fs.h"
#include "id.h"
#include "low_level.h"
#include "pack.h"
#include "util.h"
#include "temp_serializer.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Defined this to enable access logging via dgb__log_access
#define SVN_FS_FS__LOG_ACCESS
 */

/* Data / item types.
 */
#define SVN_FS_FS__ITEM_TYPE_ANY_REP    1  /* item is a representation. */
#define SVN_FS_FS__ITEM_TYPE_NODEREV    2  /* item is a noderev */
#define SVN_FS_FS__ITEM_TYPE_CHANGES    3  /* item is a changed paths list */

/* When SVN_FS_FS__LOG_ACCESS has been defined, write a line to console
 * showing where REVISION, ITEM_INDEX is located in FS and use ITEM to
 * show details on it's contents if not NULL.  To support format 6 and
 * earlier repos, ITEM_TYPE (SVN_FS_FS__ITEM_TYPE_*) must match ITEM.
 * Use SCRATCH_POOL for temporary allocations.
 *
 * For pre-format7 repos, the display will be restricted.
 */
static svn_error_t *
dbg_log_access(svn_fs_t *fs,
               svn_revnum_t revision,
               apr_off_t offset,
               void *item,
               int item_type,
               apr_pool_t *scratch_pool)
{
  /* no-op if this macro is not defined */
#ifdef SVN_FS_FS__LOG_ACCESS
  fs_fs_data_t *ffd = fs->fsap_data;
  static const char *types[] = {"<n/a>", "rep ", "node ", "chgs "};
  const char *description = "";
  const char *type = types[item_type];
  const char *pack = "";
  apr_off_t offset_in_rev = offset;

  /* determine rev / pack file offset */
  SVN_ERR(svn_fs_fs__item_offset(&offset, fs, revision, offset,
                                 scratch_pool));

  /* constructing the pack file description */
  if (revision < ffd->min_unpacked_rev)
    pack = apr_psprintf(scratch_pool, "%4ld|",
                        revision / ffd->max_files_per_dir);

  /* construct description if possible */
  if (item_type == SVN_FS_FS__ITEM_TYPE_NODEREV && item != NULL)
    {
      node_revision_t *node = item;
      const char *data_rep
        = node->data_rep
        ? apr_psprintf(scratch_pool, " d=%ld/%" APR_UINT64_T_FMT,
                       node->data_rep->revision,
                       node->data_rep->offset)
        : "";
      const char *prop_rep
        = node->prop_rep
        ? apr_psprintf(scratch_pool, " p=%ld/%" APR_UINT64_T_FMT,
                       node->prop_rep->revision,
                       node->prop_rep->offset)
        : "";
      description = apr_psprintf(scratch_pool, "%s   (pc=%d%s%s)",
                                 node->created_path,
                                 node->predecessor_count,
                                 data_rep,
                                 prop_rep);
    }
  else if (item_type == SVN_FS_FS__ITEM_TYPE_ANY_REP)
    {
      svn_fs_fs__rep_header_t *header = item;
      if (header == NULL)
        description = "  (txdelta window)";
      else if (header->type == svn_fs_fs__rep_plain)
        description = "  PLAIN";
      else if (header->type == svn_fs_fs__rep_self_delta)
        description = "  DELTA";
      else
        description = apr_psprintf(scratch_pool,
                                   "  DELTA against %ld/%" APR_UINT64_T_FMT,
                                   header->base_revision,
                                   header->base_offset);
    }
  else if (item_type == SVN_FS_FS__ITEM_TYPE_CHANGES && item != NULL)
    {
      apr_array_header_t *changes = item;
      switch (changes->nelts)
        {
          case 0:  description = "  no change";
                   break;
          case 1:  description = "  1 change";
                   break;
          default: description = apr_psprintf(scratch_pool, "  %d changes",
                                              changes->nelts);
        }
    }

  printf("%5s%10" APR_UINT64_T_HEX_FMT " %s %7ld %7" APR_UINT64_T_FMT \
          "   %s\n",
          pack, (apr_uint64_t)(offset), type, revision, offset_in_rev,
          description);

#endif

  return SVN_NO_ERROR;
}

/* Convenience wrapper around svn_io_file_aligned_seek. */
static svn_error_t *
aligned_seek(apr_file_t *file,
             apr_off_t offset,
             apr_pool_t *pool)
{
  return svn_error_trace(svn_io_file_aligned_seek(file, 0, NULL, offset,
                                                  pool));
}

/* Open the revision file for revision REV in filesystem FS and store
   the newly opened file in FILE.  Seek to location OFFSET before
   returning.  Perform temporary allocations in POOL. */
static svn_error_t *
open_and_seek_revision(apr_file_t **file,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_off_t offset,
                       apr_pool_t *pool)
{
  apr_file_t *rev_file;

  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, pool));

  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, rev, pool));
  SVN_ERR(svn_fs_fs__item_offset(&offset, fs, rev, offset, pool));
  SVN_ERR(aligned_seek(rev_file, offset, pool));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Open the representation REP for a node-revision in filesystem FS, seek
   to its position and store the newly opened file in FILE.  Perform
   temporary allocations in POOL. */
static svn_error_t *
open_and_seek_transaction(apr_file_t **file,
                          svn_fs_t *fs,
                          representation_t *rep,
                          apr_pool_t *pool)
{
  apr_file_t *rev_file;

  SVN_ERR(svn_io_file_open(&rev_file,
                           svn_fs_fs__path_txn_proto_rev(fs, &rep->txn_id,
                                                         pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(aligned_seek(rev_file, rep->offset, pool));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Given a node-id ID, and a representation REP in filesystem FS, open
   the correct file and seek to the correction location.  Store this
   file in *FILE_P.  Perform any allocations in POOL. */
static svn_error_t *
open_and_seek_representation(apr_file_t **file_p,
                             svn_fs_t *fs,
                             representation_t *rep,
                             apr_pool_t *pool)
{
  if (! svn_fs_fs__id_txn_used(&rep->txn_id))
    return open_and_seek_revision(file_p, fs, rep->revision, rep->offset,
                                  pool);
  else
    return open_and_seek_transaction(file_p, fs, rep, pool);
}



static svn_error_t *
err_dangling_id(svn_fs_t *fs, const svn_fs_id_t *id)
{
  svn_string_t *id_str = svn_fs_fs__id_unparse(id, fs->pool);
  return svn_error_createf
    (SVN_ERR_FS_ID_NOT_FOUND, 0,
     _("Reference to non-existent node '%s' in filesystem '%s'"),
     id_str->data, fs->path);
}

/* Get the node-revision for the node ID in FS.
   Set *NODEREV_P to the new node-revision structure, allocated in POOL.
   See svn_fs_fs__get_node_revision, which wraps this and adds another
   error. */
static svn_error_t *
get_node_revision_body(node_revision_t **noderev_p,
                       svn_fs_t *fs,
                       const svn_fs_id_t *id,
                       apr_pool_t *pool)
{
  apr_file_t *revision_file;
  svn_error_t *err;
  svn_boolean_t is_cached = FALSE;
  fs_fs_data_t *ffd = fs->fsap_data;

  if (svn_fs_fs__id_is_txn(id))
    {
      /* This is a transaction node-rev.  Its storage logic is very
         different from that of rev / pack files. */
      err = svn_io_file_open(&revision_file,
                             svn_fs_fs__path_txn_node_rev(fs, id, pool),
                             APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
      if (err)
        {
          if (APR_STATUS_IS_ENOENT(err->apr_err))
            {
              svn_error_clear(err);
              return svn_error_trace(err_dangling_id(fs, id));
            }

          return svn_error_trace(err);
        }

      SVN_ERR(svn_fs_fs__read_noderev(noderev_p,
                                      svn_stream_from_aprfile2(revision_file,
                                                               FALSE,
                                                               pool),
                                      pool));
    }
  else
    {
      /* noderevs in rev / pack files can be cached */
      const svn_fs_fs__id_part_t *rev_item = svn_fs_fs__id_rev_offset(id);
      pair_cache_key_t key = { 0 };
      key.revision = rev_item->revision;
      key.second = rev_item->number;

      /* Not found or not applicable. Try a noderev cache lookup.
       * If that succeeds, we are done here. */
      if (ffd->node_revision_cache)
        {
          SVN_ERR(svn_cache__get((void **) noderev_p,
                                 &is_cached,
                                 ffd->node_revision_cache,
                                 &key,
                                 pool));
          if (is_cached)
            return SVN_NO_ERROR;
        }

      /* read the data from disk */
      SVN_ERR(open_and_seek_revision(&revision_file, fs,
                                     rev_item->revision,
                                     rev_item->number,
                                     pool));
      SVN_ERR(svn_fs_fs__read_noderev(noderev_p,
                                      svn_stream_from_aprfile2(revision_file,
                                                                FALSE,
                                                                pool),
                                      pool));

      /* Workaround issue #4031: is-fresh-txn-root in revision files. */
      (*noderev_p)->is_fresh_txn_root = FALSE;

      /* The noderev is not in cache, yet. Add it, if caching has been enabled. */
      if (ffd->node_revision_cache)
        SVN_ERR(svn_cache__set(ffd->node_revision_cache,
                                &key,
                                *noderev_p,
                                pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_node_revision(node_revision_t **noderev_p,
                             svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             apr_pool_t *pool)
{
  const svn_fs_fs__id_part_t *rev_offset = svn_fs_fs__id_rev_offset(id);

  svn_error_t *err = get_node_revision_body(noderev_p, fs, id, pool);
  if (err && err->apr_err == SVN_ERR_FS_CORRUPT)
    {
      svn_string_t *id_string = svn_fs_fs__id_unparse(id, pool);
      return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                               "Corrupt node-revision '%s'",
                               id_string->data);
    }

  SVN_ERR(dbg_log_access(fs,
                         rev_offset->revision,
                         rev_offset->number,
                         *noderev_p,
                         SVN_FS_FS__ITEM_TYPE_NODEREV,
                         pool));

  return svn_error_trace(err);
}


/* Given a revision file REV_FILE, opened to REV in FS, find the Node-ID
   of the header located at OFFSET and store it in *ID_P.  Allocate
   temporary variables from POOL. */
static svn_error_t *
get_fs_id_at_offset(svn_fs_id_t **id_p,
                    apr_file_t *rev_file,
                    svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_off_t offset,
                    apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(aligned_seek(rev_file, offset, pool));
  SVN_ERR(svn_fs_fs__read_noderev(&noderev,
                                  svn_stream_from_aprfile2(rev_file, TRUE,
                                                           pool),
                                  pool));

  /* noderev->id is const, get rid of that */
  *id_p = svn_fs_fs__id_copy(noderev->id, pool);

  /* assert that the txn_id is REV
   * (asserting on offset would be harder because we the rev_offset is not
   * known here) */
  assert(svn_fs_fs__id_rev(*id_p) == rev);

  return SVN_NO_ERROR;
}


/* Given an open revision file REV_FILE in FS for REV, locate the trailer that
   specifies the offset to the root node-id and to the changed path
   information.  Store the root node offset in *ROOT_OFFSET and the
   changed path offset in *CHANGES_OFFSET.  If either of these
   pointers is NULL, do nothing with it.

   If PACKED is true, REV_FILE should be a packed shard file.
   ### There is currently no such parameter.  This function assumes that
       svn_fs_fs__is_packed_rev(FS, REV) will indicate whether REV_FILE is
       a packed file.  Therefore FS->fsap_data->min_unpacked_rev must not
       have been refreshed since REV_FILE was opened if there is a
       possibility that revision REV may have become packed since then.
       TODO: Take an IS_PACKED parameter instead, in order to remove this
       requirement.

   Allocate temporary variables from POOL. */
static svn_error_t *
get_root_changes_offset(apr_off_t *root_offset,
                        apr_off_t *changes_offset,
                        apr_file_t *rev_file,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_off_t offset;
  apr_off_t rev_offset;
  apr_seek_where_t seek_relative;
  svn_stringbuf_t *trailer = svn_stringbuf_create_ensure(64, pool);

  /* Determine where to seek to in the file.

     If we've got a pack file, we want to seek to the end of the desired
     revision.  But we don't track that, so we seek to the beginning of the
     next revision.

     Unless the next revision is in a different file, in which case, we can
     just seek to the end of the pack file -- just like we do in the
     non-packed case. */
  if (svn_fs_fs__is_packed_rev(fs, rev)
      && ((rev + 1) % ffd->max_files_per_dir != 0))
    {
      SVN_ERR(svn_fs_fs__get_packed_offset(&offset, fs, rev + 1, pool));
      seek_relative = APR_SET;
    }
  else
    {
      seek_relative = APR_END;
      offset = 0;
    }

  /* Offset of the revision from the start of the pack file, if applicable. */
  if (svn_fs_fs__is_packed_rev(fs, rev))
    SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, rev, pool));
  else
    rev_offset = 0;

  /* We will assume that the last line containing the two offsets
     will never be longer than 64 characters. */
  SVN_ERR(svn_io_file_seek(rev_file, seek_relative, &offset, pool));

  trailer->len = trailer->blocksize-1;
  SVN_ERR(aligned_seek(rev_file, offset - trailer->len, pool));

  /* Read in this last block, from which we will identify the last line. */
  SVN_ERR(svn_io_file_read(rev_file, trailer->data, &trailer->len, pool));
  trailer->data[trailer->len] = 0;

  /* Parse the last line. */
  SVN_ERR(svn_fs_fs__parse_revision_trailer(root_offset,
                                            changes_offset,
                                            trailer,
                                            rev));

  /* return absolute offsets */
  if (root_offset)
    *root_offset += rev_offset;
  if (changes_offset)
    *changes_offset += rev_offset;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__rev_get_root(svn_fs_id_t **root_id_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  apr_file_t *revision_file;
  apr_off_t root_offset;
  svn_fs_id_t *root_id = NULL;
  svn_boolean_t is_cached;

  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, pool));
  SVN_ERR(svn_cache__get((void **) root_id_p, &is_cached,
                         ffd->rev_root_id_cache, &rev, pool));
  if (is_cached)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&revision_file, fs, rev, pool));
  SVN_ERR(get_root_changes_offset(&root_offset, NULL, revision_file, fs, rev,
                                  pool));

  SVN_ERR(get_fs_id_at_offset(&root_id, revision_file, fs, rev,
                              root_offset, pool));

  SVN_ERR(svn_io_file_close(revision_file, pool));

  SVN_ERR(svn_cache__set(ffd->rev_root_id_cache, &rev, root_id, pool));

  *root_id_p = root_id;

  return SVN_NO_ERROR;
}

/* Describes a lazily opened rev / pack file.  Instances will be shared
   between multiple instances of rep_state_t. */
typedef struct shared_file_t
{
  /* The opened file. NULL while file is not open, yet. */
  apr_file_t *file;

  /* Stream wrapper around FILE. NULL while file is not open, yet. */
  svn_stream_t *stream;

  /* file system to open the file in */
  svn_fs_t *fs;

  /* a revision contained in the FILE.  Since this file may be shared,
     that value may be different from REP_STATE_T->REVISION. */
  svn_revnum_t revision;

  /* pool to use when creating the FILE.  This guarantees that the file
     remains open / valid beyond the respective local context that required
     the file to be opened eventually. */
  apr_pool_t *pool;
} shared_file_t;

/* Represents where in the current svndiff data block each
   representation is. */
typedef struct rep_state_t
{
                    /* shared lazy-open rev/pack file structure */
  shared_file_t *file;
                    /* The txdelta window cache to use or NULL. */
  svn_cache__t *window_cache;
                    /* Caches un-deltified windows. May be NULL. */
  svn_cache__t *combined_cache;
                    /* revision containing the representation */
  svn_revnum_t revision;
                    /* representation's offset in REVISION */
  apr_uint64_t offset;
                    /* length of the header at the start of the rep.
                       0 iff this is rep is stored in a container
                       (i.e. does not have a header) */
  apr_size_t header_size;
  apr_off_t start;  /* The starting offset for the raw
                       svndiff/plaintext data minus header.
                       -1 if the offset is yet unknown. */
  apr_off_t current;/* The current offset relative to START. */
  apr_off_t size;   /* The on-disk size of the representation. */
  int ver;          /* If a delta, what svndiff version? 
                       -1 for unknown delta version. */
  int chunk_index;  /* number of the window to read */
} rep_state_t;

/* Open FILE->FILE and FILE->STREAM if they haven't been opened, yet. */
static svn_error_t*
auto_open_shared_file(shared_file_t *file)
{
  if (file->file == NULL)
    {
      SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&file->file, file->fs,
                                               file->revision, file->pool));
      file->stream = svn_stream_from_aprfile2(file->file, TRUE, file->pool);
    }

  return SVN_NO_ERROR;
}

/* Set RS->START to the begin of the representation raw in RS->FILE->FILE,
   if that hasn't been done yet.  Use POOL for temporary allocations. */
static svn_error_t*
auto_set_start_offset(rep_state_t *rs, apr_pool_t *pool)
{
  if (rs->start == -1)
    {
      SVN_ERR(svn_fs_fs__item_offset(&rs->start, rs->file->fs, rs->revision,
                                     rs->offset, pool));
      rs->start += rs->header_size;
    }

  return SVN_NO_ERROR;
}

/* Set RS->VER depending on what is found in the already open RS->FILE->FILE
   if the diff version is still unknown.  Use POOL for temporary allocations.
 */
static svn_error_t*
auto_read_diff_version(rep_state_t *rs, apr_pool_t *pool)
{
  if (rs->ver == -1)
    {
      char buf[4];
      SVN_ERR(aligned_seek(rs->file->file, rs->start, pool));
      SVN_ERR(svn_io_file_read_full2(rs->file->file, buf, sizeof(buf),
                                     NULL, NULL, pool));

      /* ### Layering violation */
      if (! ((buf[0] == 'S') && (buf[1] == 'V') && (buf[2] == 'N')))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Malformed svndiff data in representation"));
      rs->ver = buf[3];

      rs->chunk_index = 0;
      rs->current = 4;
    }

  return SVN_NO_ERROR;
}

/* See create_rep_state, which wraps this and adds another error. */
static svn_error_t *
create_rep_state_body(rep_state_t **rep_state,
                      svn_fs_fs__rep_header_t **rep_header,
                      shared_file_t **shared_file,
                      representation_t *rep,
                      svn_fs_t *fs,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  rep_state_t *rs = apr_pcalloc(pool, sizeof(*rs));
  svn_fs_fs__rep_header_t *rh;
  svn_boolean_t is_cached = FALSE;

  /* If the hint is
   * - given,
   * - refers to a valid revision,
   * - refers to a packed revision,
   * - as does the rep we want to read, and
   * - refers to the same pack file as the rep
   * we can re-use the same, already open file object
   */
  svn_boolean_t reuse_shared_file
    =    shared_file && *shared_file && (*shared_file)->file
      && SVN_IS_VALID_REVNUM((*shared_file)->revision)
      && (*shared_file)->revision < ffd->min_unpacked_rev
      && rep->revision < ffd->min_unpacked_rev
      && (   ((*shared_file)->revision / ffd->max_files_per_dir)
          == (rep->revision / ffd->max_files_per_dir));

  representation_cache_key_t key;
  key.revision = rep->revision;
  key.is_packed = rep->revision < ffd->min_unpacked_rev;
  key.offset = rep->offset;

  /* continue constructing RS and RA */
  rs->size = rep->size;
  rs->revision = rep->revision;
  rs->offset = rep->offset;
  rs->window_cache = ffd->txdelta_window_cache;
  rs->combined_cache = ffd->combined_window_cache;
  rs->ver = -1;
  rs->start = -1;

  /* cache lookup, i.e. skip reading the rep header if possible */
  if (ffd->rep_header_cache && !svn_fs_fs__id_txn_used(&rep->txn_id))
    SVN_ERR(svn_cache__get((void **) &rh, &is_cached,
                           ffd->rep_header_cache, &key, pool));

  /* initialize the (shared) FILE member in RS */
  if (reuse_shared_file)
    {
      rs->file = *shared_file;
    }
  else
    {
      shared_file_t *file = apr_pcalloc(pool, sizeof(*file));
      file->revision = rep->revision;
      file->pool = pool;
      file->fs = fs;
      rs->file = file;

      /* remember the current file, if suggested by the caller */
      if (shared_file)
        *shared_file = file;
    }

  /* read rep header, if necessary */
  if (!is_cached)
    {
      /* ensure file is open and navigate to the start of rep header */
      if (reuse_shared_file)
        {
          apr_off_t offset;

          /* ... we can re-use the same, already open file object.
           * This implies that we don't read from a txn.
           */
          rs->file = *shared_file;
          SVN_ERR(auto_open_shared_file(rs->file));
          SVN_ERR(svn_fs_fs__get_packed_offset(&offset, fs, rep->revision,
                                               pool));
          SVN_ERR(aligned_seek((*shared_file)->file, offset + rep->offset,
                               pool));
        }
      else
        {
          /* otherwise, create a new file object.  May or may not be
           * an in-txn file.
           */
          SVN_ERR(open_and_seek_representation(&rs->file->file, fs, rep,
                                               pool));
          rs->file->stream = svn_stream_from_aprfile2(rs->file->file, TRUE,
                                                      rs->file->pool);
        }

      SVN_ERR(svn_fs_fs__read_rep_header(&rh, rs->file->stream, pool));
      SVN_ERR(svn_fs_fs__get_file_offset(&rs->start, rs->file->file, pool));

      /* cache the rep header if appropriate */
      if (! svn_fs_fs__id_txn_used(&rep->txn_id))
        if (ffd->rep_header_cache)
          SVN_ERR(svn_cache__set(ffd->rep_header_cache, &key, rh, pool));
    }

  /* finalize */
  SVN_ERR(dbg_log_access(fs, rep->revision, rep->offset, rh,
                         SVN_FS_FS__ITEM_TYPE_ANY_REP, pool));

  rs->header_size = rh->header_size;
  *rep_state = rs;
  *rep_header = rh;

  if (rh->type == svn_fs_fs__rep_plain)
    /* This is a plaintext, so just return the current rep_state. */
    return SVN_NO_ERROR;

  /* skip "SVNx" diff marker */
  rs->current = 4;

  return SVN_NO_ERROR;
}

/* Read the rep args for REP in filesystem FS and create a rep_state
   for reading the representation.  Return the rep_state in *REP_STATE
   and the rep header in *REP_HEADER, both allocated in POOL.

   When reading multiple reps, i.e. a skip delta chain, you may provide
   non-NULL SHARED_FILE.  (If SHARED_FILE is not NULL, in the first
   call it should be a pointer to NULL.)  The function will use this
   variable to store the previous call results and tries to re-use it.
   This may result in significant savings in I/O for packed files and
   number of open file handles.
 */
static svn_error_t *
create_rep_state(rep_state_t **rep_state,
                 svn_fs_fs__rep_header_t **rep_header,
                 shared_file_t **shared_file,
                 representation_t *rep,
                 svn_fs_t *fs,
                 apr_pool_t *pool)
{
  svn_error_t *err = create_rep_state_body(rep_state, rep_header,
                                           shared_file, rep, fs, pool);
  if (err && err->apr_err == SVN_ERR_FS_CORRUPT)
    {
      fs_fs_data_t *ffd = fs->fsap_data;

      /* ### This always returns "-1" for transaction reps, because
         ### this particular bit of code doesn't know if the rep is
         ### stored in the protorev or in the mutable area (for props
         ### or dir contents).  It is pretty rare for FSFS to *read*
         ### from the protorev file, though, so this is probably OK.
         ### And anyone going to debug corruption errors is probably
         ### going to jump straight to this comment anyway! */
      return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                               "Corrupt representation '%s'",
                               rep
                               ? svn_fs_fs__unparse_representation
                                   (rep, ffd->format, TRUE, pool)->data
                               : "(null)");
    }
  /* ### Call representation_string() ? */
  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__check_rep(representation_t *rep,
                     svn_fs_t *fs,
                     void **hint,
                     apr_pool_t *pool)
{
  rep_state_t *rs;
  svn_fs_fs__rep_header_t *rep_header;

  /* ### Should this be using read_rep_line() directly? */
  SVN_ERR(create_rep_state(&rs, &rep_header, (shared_file_t**)hint, rep,
                           fs, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__rep_chain_length(int *chain_length,
                            representation_t *rep,
                            svn_fs_t *fs,
                            apr_pool_t *pool)
{
  int count = 0;
  apr_pool_t *sub_pool = svn_pool_create(pool);
  svn_boolean_t is_delta = FALSE;
  
  /* Check whether the length of the deltification chain is acceptable.
   * Otherwise, shared reps may form a non-skipping delta chain in
   * extreme cases. */
  representation_t base_rep = *rep;

  /* re-use open files between iterations */
  shared_file_t *file_hint = NULL;

  svn_fs_fs__rep_header_t *header;

  /* follow the delta chain towards the end but for at most
   * MAX_CHAIN_LENGTH steps. */
  do
    {
      rep_state_t *rep_state;
      SVN_ERR(create_rep_state_body(&rep_state,
                                    &header,
                                    &file_hint,
                                    &base_rep,
                                    fs,
                                    sub_pool));

      base_rep.revision = header->base_revision;
      base_rep.offset = header->base_offset;
      base_rep.size = header->base_length;
      svn_fs_fs__id_txn_reset(&base_rep.txn_id);
      is_delta = header->type == svn_fs_fs__rep_delta;

      ++count;
      if (count % 16 == 0)
        {
          file_hint = NULL;
          svn_pool_clear(sub_pool);
        }
    }
  while (is_delta && base_rep.revision);

  *chain_length = count;
  svn_pool_destroy(sub_pool);

  return SVN_NO_ERROR;
}

struct rep_read_baton
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* If not NULL, this is the base for the first delta window in rs_list */
  svn_stringbuf_t *base_window;

  /* The state of all prior delta representations. */
  apr_array_header_t *rs_list;

  /* The plaintext state, if there is a plaintext. */
  rep_state_t *src_state;

  /* The index of the current delta chunk, if we are reading a delta. */
  int chunk_index;

  /* The buffer where we store undeltified data. */
  char *buf;
  apr_size_t buf_pos;
  apr_size_t buf_len;

  /* A checksum context for summing the data read in order to verify it.
     Note: we don't need to use the sha1 checksum because we're only doing
     data verification, for which md5 is perfectly safe.  */
  svn_checksum_ctx_t *md5_checksum_ctx;

  svn_boolean_t checksum_finalized;

  /* The stored checksum of the representation we are reading, its
     length, and the amount we've read so far.  Some of this
     information is redundant with rs_list and src_state, but it's
     convenient for the checksumming code to have it here. */
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];

  svn_filesize_t len;
  svn_filesize_t off;

  /* The key for the fulltext cache for this rep, if there is a
     fulltext cache. */
  pair_cache_key_t fulltext_cache_key;
  /* The text we've been reading, if we're going to cache it. */
  svn_stringbuf_t *current_fulltext;

  /* Used for temporary allocations during the read. */
  apr_pool_t *pool;

  /* Pool used to store file handles and other data that is persistant
     for the entire stream read. */
  apr_pool_t *filehandle_pool;
};

/* Set window key in *KEY to address the window described by RS.
   For convenience, return the KEY. */
static window_cache_key_t *
get_window_key(window_cache_key_t *key, rep_state_t *rs)
{
  assert(rs->revision <= APR_UINT32_MAX);
  key->revision = (apr_uint32_t)rs->revision;
  key->offset = rs->offset;
  key->chunk_index = rs->chunk_index;

  return key;
}

/* Read the WINDOW_P number CHUNK_INDEX for the representation given in
 * rep state RS from the current FSFS session's cache.  This will be a
 * no-op and IS_CACHED will be set to FALSE if no cache has been given.
 * If a cache is available IS_CACHED will inform the caller about the
 * success of the lookup. Allocations (of the window in particualar) will
 * be made from POOL.
 *
 * If the information could be found, put RS to CHUNK_INDEX.
 */
static svn_error_t *
get_cached_window(svn_txdelta_window_t **window_p,
                  rep_state_t *rs,
                  int chunk_index,
                  svn_boolean_t *is_cached,
                  apr_pool_t *pool)
{
  if (! rs->window_cache)
    {
      /* txdelta window has not been enabled */
      *is_cached = FALSE;
    }
  else
    {
      /* ask the cache for the desired txdelta window */
      svn_fs_fs__txdelta_cached_window_t *cached_window;
      window_cache_key_t key = { 0 };
      get_window_key(&key, rs);
      key.chunk_index = chunk_index;
      SVN_ERR(svn_cache__get((void **) &cached_window,
                             is_cached,
                             rs->window_cache,
                             &key,
                             pool));

      if (*is_cached)
        {
          /* found it. Pass it back to the caller. */
          *window_p = cached_window->window;

          /* manipulate the RS as if we just read the data */
          rs->current = cached_window->end_offset;
          rs->chunk_index = chunk_index;
        }
    }

  return SVN_NO_ERROR;
}

/* Store the WINDOW read for the rep state RS in the current FSFS
 * session's cache.  This will be a no-op if no cache has been given.
 * Temporary allocations will be made from SCRATCH_POOL. */
static svn_error_t *
set_cached_window(svn_txdelta_window_t *window,
                  rep_state_t *rs,
                  apr_pool_t *scratch_pool)
{
  if (rs->window_cache)
    {
      /* store the window and the first offset _past_ it */
      svn_fs_fs__txdelta_cached_window_t cached_window;
      window_cache_key_t key = {0};

      cached_window.window = window;
      cached_window.end_offset = rs->current;

      /* but key it with the start offset because that is the known state
       * when we will look it up */
      SVN_ERR(svn_cache__set(rs->window_cache,
                             get_window_key(&key, rs),
                             &cached_window,
                             scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Read the WINDOW_P for the rep state RS from the current FSFS session's
 * cache. This will be a no-op and IS_CACHED will be set to FALSE if no
 * cache has been given. If a cache is available IS_CACHED will inform
 * the caller about the success of the lookup. Allocations (of the window
 * in particualar) will be made from POOL.
 */
static svn_error_t *
get_cached_combined_window(svn_stringbuf_t **window_p,
                           rep_state_t *rs,
                           svn_boolean_t *is_cached,
                           apr_pool_t *pool)
{
  if (! rs->combined_cache)
    {
      /* txdelta window has not been enabled */
      *is_cached = FALSE;
    }
  else
    {
      /* ask the cache for the desired txdelta window */
      window_cache_key_t key = { 0 };
      return svn_cache__get((void **)window_p,
                            is_cached,
                            rs->combined_cache,
                            get_window_key(&key, rs),
                            pool);
    }

  return SVN_NO_ERROR;
}

/* Store the WINDOW read for the rep state RS in the current FSFS session's
 * cache. This will be a no-op if no cache has been given.
 * Temporary allocations will be made from SCRATCH_POOL. */
static svn_error_t *
set_cached_combined_window(svn_stringbuf_t *window,
                           rep_state_t *rs,
                           apr_pool_t *scratch_pool)
{
  if (rs->combined_cache)
    {
      /* but key it with the start offset because that is the known state
       * when we will look it up */
      window_cache_key_t key = { 0 };
      return svn_cache__set(rs->combined_cache,
                            get_window_key(&key, rs),
                            window,
                            scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Build an array of rep_state structures in *LIST giving the delta
   reps from first_rep to a plain-text or self-compressed rep.  Set
   *SRC_STATE to the plain-text rep we find at the end of the chain,
   or to NULL if the final delta representation is self-compressed.
   The representation to start from is designated by filesystem FS, id
   ID, and representation REP.
   Also, set *WINDOW_P to the base window content for *LIST, if it
   could be found in cache. Otherwise, *LIST will contain the base
   representation for the whole delta chain.
   Finally, return the expanded size of the representation in
   *EXPANDED_SIZE. It will take care of cases where only the on-disk
   size is known.  */
static svn_error_t *
build_rep_list(apr_array_header_t **list,
               svn_stringbuf_t **window_p,
               rep_state_t **src_state,
               svn_filesize_t *expanded_size,
               svn_fs_t *fs,
               representation_t *first_rep,
               apr_pool_t *pool)
{
  representation_t rep;
  rep_state_t *rs = NULL;
  svn_fs_fs__rep_header_t *rep_header;
  svn_boolean_t is_cached = FALSE;
  shared_file_t *shared_file = NULL;

  *list = apr_array_make(pool, 1, sizeof(rep_state_t *));
  rep = *first_rep;

  /* The value as stored in the data struct.
     0 is either for unknown length or actually zero length. */
  *expanded_size = first_rep->expanded_size;

  /* for the top-level rep, we need the rep_args */
  SVN_ERR(create_rep_state(&rs, &rep_header, &shared_file, &rep, fs, pool));

  /* Unknown size or empty representation?
     That implies the this being the first iteration.
     Usually size equals on-disk size, except for empty,
     compressed representations (delta, size = 4).
     Please note that for all non-empty deltas have
     a 4-byte header _plus_ some data. */
  if (*expanded_size == 0)
    if (rep_header->type == svn_fs_fs__rep_plain || first_rep->size != 4)
      *expanded_size = first_rep->size;

  while (1)
    {
      /* fetch state, if that has not been done already */
      if (!rs)
        SVN_ERR(create_rep_state(&rs, &rep_header, &shared_file,
                                 &rep, fs, pool));

      /* for txn reps, there won't be a cached combined window */
      if (!svn_fs_fs__id_txn_used(&rep.txn_id))
        SVN_ERR(get_cached_combined_window(window_p, rs, &is_cached, pool));

      if (is_cached)
        {
          /* We already have a reconstructed window in our cache.
             Write a pseudo rep_state with the full length. */
          rs->start = 0;
          rs->current = 0;
          rs->size = (*window_p)->len;
          *src_state = rs;
          return SVN_NO_ERROR;
        }

      if (rep_header->type == svn_fs_fs__rep_plain)
        {
          /* This is a plaintext, so just return the current rep_state. */
          *src_state = rs;
          return SVN_NO_ERROR;
        }

      /* Push this rep onto the list.  If it's self-compressed, we're done. */
      APR_ARRAY_PUSH(*list, rep_state_t *) = rs;
      if (rep_header->type == svn_fs_fs__rep_self_delta)
        {
          *src_state = NULL;
          return SVN_NO_ERROR;
        }

      rep.revision = rep_header->base_revision;
      rep.offset = rep_header->base_offset;
      rep.size = rep_header->base_length;
      svn_fs_fs__id_txn_reset(&rep.txn_id);

      rs = NULL;
    }
}


/* Create a rep_read_baton structure for node revision NODEREV in
   filesystem FS and store it in *RB_P.  If FULLTEXT_CACHE_KEY is not
   NULL, it is the rep's key in the fulltext cache, and a stringbuf
   must be allocated to store the text.  Perform all allocations in
   POOL.  If rep is mutable, it must be for file contents. */
static svn_error_t *
rep_read_get_baton(struct rep_read_baton **rb_p,
                   svn_fs_t *fs,
                   representation_t *rep,
                   pair_cache_key_t fulltext_cache_key,
                   apr_pool_t *pool)
{
  struct rep_read_baton *b;

  b = apr_pcalloc(pool, sizeof(*b));
  b->fs = fs;
  b->base_window = NULL;
  b->chunk_index = 0;
  b->buf = NULL;
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);
  b->checksum_finalized = FALSE;
  memcpy(b->md5_digest, rep->md5_digest, sizeof(rep->md5_digest));
  b->len = rep->expanded_size;
  b->off = 0;
  b->fulltext_cache_key = fulltext_cache_key;
  b->pool = svn_pool_create(pool);
  b->filehandle_pool = svn_pool_create(pool);

  SVN_ERR(build_rep_list(&b->rs_list, &b->base_window,
                         &b->src_state, &b->len, fs, rep,
                         b->filehandle_pool));

  if (SVN_IS_VALID_REVNUM(fulltext_cache_key.revision))
    b->current_fulltext = svn_stringbuf_create_ensure
                            ((apr_size_t)b->len,
                             b->filehandle_pool);
  else
    b->current_fulltext = NULL;

  /* Save our output baton. */
  *rb_p = b;

  return SVN_NO_ERROR;
}

/* Skip forwards to THIS_CHUNK in REP_STATE and then read the next delta
   window into *NWIN.  Note that RS->CHUNK_INDEX will be THIS_CHUNK rather
   than THIS_CHUNK + 1 when this function returns. */
static svn_error_t *
read_delta_window(svn_txdelta_window_t **nwin, int this_chunk,
                  rep_state_t *rs, apr_pool_t *pool)
{
  svn_boolean_t is_cached;
  apr_off_t start_offset;
  apr_off_t end_offset;
  SVN_ERR_ASSERT(rs->chunk_index <= this_chunk);

  SVN_ERR(dbg_log_access(rs->file->fs, rs->revision, rs->offset,
                         NULL, SVN_FS_FS__ITEM_TYPE_ANY_REP, pool));

  /* Read the next window.  But first, try to find it in the cache. */
  SVN_ERR(get_cached_window(nwin, rs, this_chunk, &is_cached, pool));
  if (is_cached)
    return SVN_NO_ERROR;

  /* someone has to actually read the data from file.  Open it */
  SVN_ERR(auto_open_shared_file(rs->file));
  SVN_ERR(auto_set_start_offset(rs, pool));
  SVN_ERR(auto_read_diff_version(rs, pool));

  /* RS->FILE may be shared between RS instances -> make sure we point
   * to the right data. */
  start_offset = rs->start + rs->current;
  SVN_ERR(aligned_seek(rs->file->file, start_offset, pool));

  /* Skip windows to reach the current chunk if we aren't there yet. */
  while (rs->chunk_index < this_chunk)
    {
      SVN_ERR(svn_txdelta_skip_svndiff_window(rs->file->file, rs->ver,
                                              pool));
      rs->chunk_index++;
      SVN_ERR(svn_fs_fs__get_file_offset(&start_offset, rs->file->file,
                                         pool));
      rs->current = start_offset - rs->start;
      if (rs->current >= rs->size)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Reading one svndiff window read "
                                  "beyond the end of the "
                                  "representation"));
    }

  /* Actually read the next window. */
  SVN_ERR(svn_txdelta_read_svndiff_window(nwin, rs->file->stream, rs->ver,
                                          pool));
  SVN_ERR(svn_fs_fs__get_file_offset(&end_offset, rs->file->file, pool));
  rs->current = end_offset - rs->start;
  if (rs->current > rs->size)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Reading one svndiff window read beyond "
                              "the end of the representation"));

  /* the window has not been cached before, thus cache it now
   * (if caching is used for them at all) */
  if (SVN_IS_VALID_REVNUM(rs->revision))
    SVN_ERR(set_cached_window(*nwin, rs, pool));

  return SVN_NO_ERROR;
}

/* Read SIZE bytes from the representation RS and return it in *NWIN. */
static svn_error_t *
read_plain_window(svn_stringbuf_t **nwin, rep_state_t *rs,
                  apr_size_t size, apr_pool_t *pool)
{
  apr_off_t offset;
  
  /* RS->FILE may be shared between RS instances -> make sure we point
   * to the right data. */
  SVN_ERR(auto_open_shared_file(rs->file));
  SVN_ERR(auto_set_start_offset(rs, pool));

  offset = rs->start + rs->current;
  SVN_ERR(aligned_seek(rs->file->file, offset, pool));

  /* Read the plain data. */
  *nwin = svn_stringbuf_create_ensure(size, pool);
  SVN_ERR(svn_io_file_read_full2(rs->file->file, (*nwin)->data, size,
                                 NULL, NULL, pool));
  (*nwin)->data[size] = 0;

  /* Update RS. */
  rs->current += (apr_off_t)size;

  return SVN_NO_ERROR;
}

/* Get the undeltified window that is a result of combining all deltas
   from the current desired representation identified in *RB with its
   base representation.  Store the window in *RESULT. */
static svn_error_t *
get_combined_window(svn_stringbuf_t **result,
                    struct rep_read_baton *rb)
{
  apr_pool_t *pool, *new_pool, *window_pool;
  int i;
  apr_array_header_t *windows;
  svn_stringbuf_t *source, *buf = rb->base_window;
  rep_state_t *rs;

  /* Read all windows that we need to combine. This is fine because
     the size of each window is relatively small (100kB) and skip-
     delta limits the number of deltas in a chain to well under 100.
     Stop early if one of them does not depend on its predecessors. */
  window_pool = svn_pool_create(rb->pool);
  windows = apr_array_make(window_pool, 0, sizeof(svn_txdelta_window_t *));
  for (i = 0; i < rb->rs_list->nelts; ++i)
    {
      svn_txdelta_window_t *window;

      rs = APR_ARRAY_IDX(rb->rs_list, i, rep_state_t *);
      SVN_ERR(read_delta_window(&window, rb->chunk_index, rs, window_pool));

      APR_ARRAY_PUSH(windows, svn_txdelta_window_t *) = window;
      if (window->src_ops == 0)
        {
          ++i;
          break;
        }
    }

  /* Combine in the windows from the other delta reps. */
  pool = svn_pool_create(rb->pool);
  for (--i; i >= 0; --i)
    {
      svn_txdelta_window_t *window;

      rs = APR_ARRAY_IDX(rb->rs_list, i, rep_state_t *);
      window = APR_ARRAY_IDX(windows, i, svn_txdelta_window_t *);

      /* Maybe, we've got a PLAIN start representation.  If we do, read
         as much data from it as the needed for the txdelta window's source
         view.
         Note that BUF / SOURCE may only be NULL in the first iteration. */
      source = buf;
      if (source == NULL && rb->src_state != NULL)
        SVN_ERR(read_plain_window(&source, rb->src_state, window->sview_len,
                                  pool));

      /* Combine this window with the current one. */
      new_pool = svn_pool_create(rb->pool);
      buf = svn_stringbuf_create_ensure(window->tview_len, new_pool);
      buf->len = window->tview_len;

      svn_txdelta_apply_instructions(window, source ? source->data : NULL,
                                     buf->data, &buf->len);
      if (buf->len != window->tview_len)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("svndiff window length is "
                                  "corrupt"));

      /* Cache windows only if the whole rep content could be read as a
         single chunk.  Only then will no other chunk need a deeper RS
         list than the cached chunk. */
      if (   (rb->chunk_index == 0) && (rs->current == rs->size)
          && SVN_IS_VALID_REVNUM(rs->revision))
        SVN_ERR(set_cached_combined_window(buf, rs, new_pool));

      rs->chunk_index++;

      /* Cycle pools so that we only need to hold three windows at a time. */
      svn_pool_destroy(pool);
      pool = new_pool;
    }

  svn_pool_destroy(window_pool);

  *result = buf;
  return SVN_NO_ERROR;
}

/* Returns whether or not the expanded fulltext of the file is cachable
 * based on its size SIZE.  The decision depends on the cache used by RB.
 */
static svn_boolean_t
fulltext_size_is_cachable(fs_fs_data_t *ffd, svn_filesize_t size)
{
  return (size < APR_SIZE_MAX)
      && svn_cache__is_cachable(ffd->fulltext_cache, (apr_size_t)size);
}

/* Close method used on streams returned by read_representation().
 */
static svn_error_t *
rep_read_contents_close(void *baton)
{
  struct rep_read_baton *rb = baton;

  svn_pool_destroy(rb->pool);
  svn_pool_destroy(rb->filehandle_pool);

  return SVN_NO_ERROR;
}

/* Return the next *LEN bytes of the rep and store them in *BUF. */
static svn_error_t *
get_contents(struct rep_read_baton *rb,
             char *buf,
             apr_size_t *len)
{
  apr_size_t copy_len, remaining = *len;
  char *cur = buf;
  rep_state_t *rs;

  /* Special case for when there are no delta reps, only a plain
     text. */
  if (rb->rs_list->nelts == 0)
    {
      copy_len = remaining;
      rs = rb->src_state;

      if (rb->base_window != NULL)
        {
          /* We got the desired rep directly from the cache.
             This is where we need the pseudo rep_state created
             by build_rep_list(). */
          apr_size_t offset = (apr_size_t)rs->current;
          if (copy_len + offset > rb->base_window->len)
            copy_len = offset < rb->base_window->len
                     ? rb->base_window->len - offset
                     : 0ul;

          memcpy (cur, rb->base_window->data + offset, copy_len);
        }
      else
        {
          apr_off_t offset;
          if (((apr_off_t) copy_len) > rs->size - rs->current)
            copy_len = (apr_size_t) (rs->size - rs->current);

          SVN_ERR(auto_open_shared_file(rs->file));
          SVN_ERR(auto_set_start_offset(rs, rb->pool));

          offset = rs->start + rs->current;
          SVN_ERR(aligned_seek(rs->file->file, offset, rb->pool));
          SVN_ERR(svn_io_file_read_full2(rs->file->file, cur, copy_len,
                                         NULL, NULL, rb->pool));
        }

      rs->current += copy_len;
      *len = copy_len;
      return SVN_NO_ERROR;
    }

  while (remaining > 0)
    {
      /* If we have buffered data from a previous chunk, use that. */
      if (rb->buf)
        {
          /* Determine how much to copy from the buffer. */
          copy_len = rb->buf_len - rb->buf_pos;
          if (copy_len > remaining)
            copy_len = remaining;

          /* Actually copy the data. */
          memcpy(cur, rb->buf + rb->buf_pos, copy_len);
          rb->buf_pos += copy_len;
          cur += copy_len;
          remaining -= copy_len;

          /* If the buffer is all used up, clear it and empty the
             local pool. */
          if (rb->buf_pos == rb->buf_len)
            {
              svn_pool_clear(rb->pool);
              rb->buf = NULL;
            }
        }
      else
        {
          svn_stringbuf_t *sbuf = NULL;

          rs = APR_ARRAY_IDX(rb->rs_list, 0, rep_state_t *);
          if (rs->current == rs->size)
            break;

          /* Get more buffered data by evaluating a chunk. */
          SVN_ERR(get_combined_window(&sbuf, rb));

          rb->chunk_index++;
          rb->buf_len = sbuf->len;
          rb->buf = sbuf->data;
          rb->buf_pos = 0;
        }
    }

  *len = cur - buf;

  return SVN_NO_ERROR;
}

/* BATON is of type `rep_read_baton'; read the next *LEN bytes of the
   representation and store them in *BUF.  Sum as we read and verify
   the MD5 sum at the end. */
static svn_error_t *
rep_read_contents(void *baton,
                  char *buf,
                  apr_size_t *len)
{
  struct rep_read_baton *rb = baton;

  /* Get the next block of data. */
  SVN_ERR(get_contents(rb, buf, len));

  if (rb->current_fulltext)
    svn_stringbuf_appendbytes(rb->current_fulltext, buf, *len);

  /* Perform checksumming.  We want to check the checksum as soon as
     the last byte of data is read, in case the caller never performs
     a short read, but we don't want to finalize the MD5 context
     twice. */
  if (!rb->checksum_finalized)
    {
      SVN_ERR(svn_checksum_update(rb->md5_checksum_ctx, buf, *len));
      rb->off += *len;
      if (rb->off == rb->len)
        {
          svn_checksum_t *md5_checksum;
          svn_checksum_t expected;
          expected.kind = svn_checksum_md5;
          expected.digest = rb->md5_digest;

          rb->checksum_finalized = TRUE;
          SVN_ERR(svn_checksum_final(&md5_checksum, rb->md5_checksum_ctx,
                                     rb->pool));
          if (!svn_checksum_match(md5_checksum, &expected))
            return svn_error_create(SVN_ERR_FS_CORRUPT,
                    svn_checksum_mismatch_err(&expected, md5_checksum,
                        rb->pool,
                        _("Checksum mismatch while reading representation")),
                    NULL);
        }
    }

  if (rb->off == rb->len && rb->current_fulltext)
    {
      fs_fs_data_t *ffd = rb->fs->fsap_data;
      SVN_ERR(svn_cache__set(ffd->fulltext_cache, &rb->fulltext_cache_key,
                             rb->current_fulltext, rb->pool));
      rb->current_fulltext = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_contents(svn_stream_t **contents_p,
                        svn_fs_t *fs,
                        representation_t *rep,
                        apr_pool_t *pool)
{
  if (! rep)
    {
      *contents_p = svn_stream_empty(pool);
    }
  else
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      pair_cache_key_t fulltext_cache_key = { 0 };
      svn_filesize_t len = rep->expanded_size ? rep->expanded_size : rep->size;
      struct rep_read_baton *rb;

      fulltext_cache_key.revision = rep->revision;
      fulltext_cache_key.second = rep->offset;
      if (ffd->fulltext_cache && SVN_IS_VALID_REVNUM(rep->revision)
          && fulltext_size_is_cachable(ffd, len))
        {
          svn_stringbuf_t *fulltext;
          svn_boolean_t is_cached;
          SVN_ERR(svn_cache__get((void **) &fulltext, &is_cached,
                                 ffd->fulltext_cache, &fulltext_cache_key,
                                 pool));
          if (is_cached)
            {
              *contents_p = svn_stream_from_stringbuf(fulltext, pool);
              return SVN_NO_ERROR;
            }
        }
      else
        fulltext_cache_key.revision = SVN_INVALID_REVNUM;

      SVN_ERR(rep_read_get_baton(&rb, fs, rep, fulltext_cache_key, pool));

      *contents_p = svn_stream_create(rb, pool);
      svn_stream_set_read(*contents_p, rep_read_contents);
      svn_stream_set_close(*contents_p, rep_read_contents_close);
    }

  return SVN_NO_ERROR;
}

/* Baton for cache_access_wrapper. Wraps the original parameters of
 * svn_fs_fs__try_process_file_content().
 */
typedef struct cache_access_wrapper_baton_t
{
  svn_fs_process_contents_func_t func;
  void* baton;
} cache_access_wrapper_baton_t;

/* Wrapper to translate between svn_fs_process_contents_func_t and
 * svn_cache__partial_getter_func_t.
 */
static svn_error_t *
cache_access_wrapper(void **out,
                     const void *data,
                     apr_size_t data_len,
                     void *baton,
                     apr_pool_t *pool)
{
  cache_access_wrapper_baton_t *wrapper_baton = baton;

  SVN_ERR(wrapper_baton->func((const unsigned char *)data,
                              data_len - 1, /* cache adds terminating 0 */
                              wrapper_baton->baton,
                              pool));

  /* non-NULL value to signal the calling cache that all went well */
  *out = baton;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__try_process_file_contents(svn_boolean_t *success,
                                     svn_fs_t *fs,
                                     node_revision_t *noderev,
                                     svn_fs_process_contents_func_t processor,
                                     void* baton,
                                     apr_pool_t *pool)
{
  representation_t *rep = noderev->data_rep;
  if (rep)
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      pair_cache_key_t fulltext_cache_key = { 0 };

      fulltext_cache_key.revision = rep->revision;
      fulltext_cache_key.second = rep->offset;
      if (ffd->fulltext_cache && SVN_IS_VALID_REVNUM(rep->revision)
          && fulltext_size_is_cachable(ffd, rep->expanded_size))
        {
          cache_access_wrapper_baton_t wrapper_baton;
          void *dummy = NULL;

          wrapper_baton.func = processor;
          wrapper_baton.baton = baton;
          return svn_cache__get_partial(&dummy, success,
                                        ffd->fulltext_cache,
                                        &fulltext_cache_key,
                                        cache_access_wrapper,
                                        &wrapper_baton,
                                        pool);
        }
    }

  *success = FALSE;
  return SVN_NO_ERROR;
}


/* Baton used when reading delta windows. */
struct delta_read_baton
{
  rep_state_t *rs;
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];
};

/* This implements the svn_txdelta_next_window_fn_t interface. */
static svn_error_t *
delta_read_next_window(svn_txdelta_window_t **window, void *baton,
                       apr_pool_t *pool)
{
  struct delta_read_baton *drb = baton;

  *window = NULL;
  if (drb->rs->current < drb->rs->size)
    {
      SVN_ERR(read_delta_window(window, drb->rs->chunk_index, drb->rs, pool));
      drb->rs->chunk_index++;
    }

  return SVN_NO_ERROR;
}

/* This implements the svn_txdelta_md5_digest_fn_t interface. */
static const unsigned char *
delta_read_md5_digest(void *baton)
{
  struct delta_read_baton *drb = baton;
  return drb->md5_digest;
}

svn_error_t *
svn_fs_fs__get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                 svn_fs_t *fs,
                                 node_revision_t *source,
                                 node_revision_t *target,
                                 apr_pool_t *pool)
{
  svn_stream_t *source_stream, *target_stream;

  /* Try a shortcut: if the target is stored as a delta against the source,
     then just use that delta. */
  if (source && source->data_rep && target->data_rep)
    {
      rep_state_t *rep_state;
      svn_fs_fs__rep_header_t *rep_header;

      /* Read target's base rep if any. */
      SVN_ERR(create_rep_state(&rep_state, &rep_header, NULL,
                               target->data_rep, fs, pool));
      /* If that matches source, then use this delta as is. */
      if (rep_header->type == svn_fs_fs__rep_self_delta
          || (rep_header->type == svn_fs_fs__rep_delta
              && rep_header->base_revision == source->data_rep->revision
              && rep_header->base_offset == source->data_rep->offset))
        {
          /* Create the delta read baton. */
          struct delta_read_baton *drb = apr_pcalloc(pool, sizeof(*drb));
          drb->rs = rep_state;
          memcpy(drb->md5_digest, target->data_rep->md5_digest,
                 sizeof(drb->md5_digest));
          *stream_p = svn_txdelta_stream_create(drb, delta_read_next_window,
                                                delta_read_md5_digest, pool);
          return SVN_NO_ERROR;
        }
      else
        if (rep_state->file->file)
          SVN_ERR(svn_io_file_close(rep_state->file->file, pool));
    }

  /* Read both fulltexts and construct a delta. */
  if (source)
    SVN_ERR(svn_fs_fs__get_contents(&source_stream, fs, source->data_rep,
                                    pool));
  else
    source_stream = svn_stream_empty(pool);
  SVN_ERR(svn_fs_fs__get_contents(&target_stream, fs, target->data_rep, pool));

  /* Because source and target stream will already verify their content,
   * there is no need to do this once more.  In particular if the stream
   * content is being fetched from cache. */
  svn_txdelta2(stream_p, source_stream, target_stream, FALSE, pool);

  return SVN_NO_ERROR;
}

/* Fetch the contents of a directory into ENTRIES.  Values are stored
   as filename to string mappings; further conversion is necessary to
   convert them into svn_fs_dirent_t values. */
static svn_error_t *
get_dir_contents(apr_hash_t *entries,
                 svn_fs_t *fs,
                 node_revision_t *noderev,
                 apr_pool_t *pool)
{
  svn_stream_t *contents;

  if (noderev->data_rep && svn_fs_fs__id_txn_used(&noderev->data_rep->txn_id))
    {
      const char *filename
        = svn_fs_fs__path_txn_node_children(fs, noderev->id, pool);

      /* The representation is mutable.  Read the old directory
         contents from the mutable children file, followed by the
         changes we've made in this transaction. */
      SVN_ERR(svn_stream_open_readonly(&contents, filename, pool, pool));
      SVN_ERR(svn_hash_read2(entries, contents, SVN_HASH_TERMINATOR, pool));
      SVN_ERR(svn_hash_read_incremental(entries, contents, NULL, pool));
      SVN_ERR(svn_stream_close(contents));
    }
  else if (noderev->data_rep)
    {
      /* use a temporary pool for temp objects.
       * Also undeltify content before parsing it. Otherwise, we could only
       * parse it byte-by-byte.
       */
      apr_pool_t *text_pool = svn_pool_create(pool);
      apr_size_t len = noderev->data_rep->expanded_size
                     ? (apr_size_t)noderev->data_rep->expanded_size
                     : (apr_size_t)noderev->data_rep->size;
      svn_stringbuf_t *text;

      /* The representation is immutable.  Read it normally. */
      SVN_ERR(svn_fs_fs__get_contents(&contents, fs, noderev->data_rep,
                                      text_pool));
      SVN_ERR(svn_stringbuf_from_stream(&text, contents, len, text_pool));
      SVN_ERR(svn_stream_close(contents));

      /* de-serialize hash */
      contents = svn_stream_from_stringbuf(text, text_pool);
      SVN_ERR(svn_hash_read2(entries, contents, SVN_HASH_TERMINATOR, pool));

      svn_pool_destroy(text_pool);
    }

  return SVN_NO_ERROR;
}


/* Given a hash STR_ENTRIES with values as svn_string_t as specified
   in an FSFS directory contents listing, return a hash of dirents in
   *ENTRIES_P.  Use ID to generate more helpful error messages.
   Perform allocations in POOL. */
static svn_error_t *
parse_dir_entries(apr_hash_t **entries_p,
                  apr_hash_t *str_entries,
                  const svn_fs_id_t *id,
                  apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *entries_p = apr_hash_make(pool);

  /* Translate the string dir entries into real entries. */
  for (hi = apr_hash_first(pool, str_entries); hi; hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *str_val = svn__apr_hash_index_val(hi);
      char *str, *last_str;
      svn_fs_dirent_t *dirent = apr_pcalloc(pool, sizeof(*dirent));

      last_str = apr_pstrdup(pool, str_val->data);
      dirent->name = apr_pstrdup(pool, name);

      str = svn_cstring_tokenize(" ", &last_str);
      if (str == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Directory entry corrupt in '%s'"),
                                 svn_fs_fs__id_unparse(id, pool)->data);

      if (strcmp(str, SVN_FS_FS__KIND_FILE) == 0)
        {
          dirent->kind = svn_node_file;
        }
      else if (strcmp(str, SVN_FS_FS__KIND_DIR) == 0)
        {
          dirent->kind = svn_node_dir;
        }
      else
        {
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                   _("Directory entry corrupt in '%s'"),
                                   svn_fs_fs__id_unparse(id, pool)->data);
        }

      str = svn_cstring_tokenize(" ", &last_str);
      if (str == NULL)
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                   _("Directory entry corrupt in '%s'"),
                                   svn_fs_fs__id_unparse(id, pool)->data);

      dirent->id = svn_fs_fs__id_parse(str, strlen(str), pool);

      svn_hash_sets(*entries_p, dirent->name, dirent);
    }

  return SVN_NO_ERROR;
}

/* Return the cache object in FS responsible to storing the directory the
 * NODEREV plus the corresponding *KEY.  If no cache exists, return NULL.
 * PAIR_KEY must point to some key struct, which does not need to be
 * initialized.  We use it to avoid dynamic allocation.
 */
static svn_cache__t *
locate_dir_cache(svn_fs_t *fs,
                 const void **key,
                 pair_cache_key_t *pair_key,
                 node_revision_t *noderev,
                 apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (svn_fs_fs__id_is_txn(noderev->id))
    {
      /* data in txns requires the expensive fs_id-based addressing mode */
      *key = svn_fs_fs__id_unparse(noderev->id, pool)->data;
      return ffd->txn_dir_cache;
    }
  else
    {
      /* committed data can use simple rev,item pairs */
      if (noderev->data_rep)
        {
          pair_key->revision = noderev->data_rep->revision;
          pair_key->second = noderev->data_rep->offset;
          *key = pair_key;
        }
      else
        {
          /* no data rep -> empty directory.
             A NULL key causes a cache miss. */
          *key = NULL;
        }
      
      return ffd->dir_cache;
    }
}

svn_error_t *
svn_fs_fs__rep_contents_dir(apr_hash_t **entries_p,
                            svn_fs_t *fs,
                            node_revision_t *noderev,
                            apr_pool_t *pool)
{
  pair_cache_key_t pair_key = { 0 };
  const void *key;
  apr_hash_t *unparsed_entries, *parsed_entries;

  /* find the cache we may use */
  svn_cache__t *cache = locate_dir_cache(fs, &key, &pair_key, noderev, pool);
  if (cache)
    {
      svn_boolean_t found;

      SVN_ERR(svn_cache__get((void **)entries_p, &found, cache, key, pool));
      if (found)
        return SVN_NO_ERROR;
    }

  /* Read in the directory hash. */
  unparsed_entries = apr_hash_make(pool);
  SVN_ERR(get_dir_contents(unparsed_entries, fs, noderev, pool));
  SVN_ERR(parse_dir_entries(&parsed_entries, unparsed_entries,
                            noderev->id, pool));

  /* Update the cache, if we are to use one. */
  if (cache)
    SVN_ERR(svn_cache__set(cache, key, parsed_entries, pool));

  *entries_p = parsed_entries;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__rep_contents_dir_entry(svn_fs_dirent_t **dirent,
                                  svn_fs_t *fs,
                                  node_revision_t *noderev,
                                  const char *name,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_boolean_t found = FALSE;

  /* find the cache we may use */
  pair_cache_key_t pair_key = { 0 };
  const void *key;
  svn_cache__t *cache = locate_dir_cache(fs, &key, &pair_key, noderev,
                                         scratch_pool);
  if (cache)
    {
      /* Cache lookup. */
      SVN_ERR(svn_cache__get_partial((void **)dirent,
                                     &found,
                                     cache,
                                     key,
                                     svn_fs_fs__extract_dir_entry,
                                     (void*)name,
                                     result_pool));
    }

  /* fetch data from disk if we did not find it in the cache */
  if (! found)
    {
      apr_hash_t *entries;
      svn_fs_dirent_t *entry;
      svn_fs_dirent_t *entry_copy = NULL;

      /* read the dir from the file system. It will probably be put it
         into the cache for faster lookup in future calls. */
      SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, noderev,
                                          scratch_pool));

      /* find desired entry and return a copy in POOL, if found */
      entry = svn_hash_gets(entries, name);
      if (entry != NULL)
        {
          entry_copy = apr_palloc(result_pool, sizeof(*entry_copy));
          entry_copy->name = apr_pstrdup(result_pool, entry->name);
          entry_copy->id = svn_fs_fs__id_copy(entry->id, result_pool);
          entry_copy->kind = entry->kind;
        }

      *dirent = entry_copy;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_proplist(apr_hash_t **proplist_p,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool)
{
  apr_hash_t *proplist;
  svn_stream_t *stream;

  if (noderev->prop_rep && svn_fs_fs__id_txn_used(&noderev->prop_rep->txn_id))
    {
      const char *filename
        = svn_fs_fs__path_txn_node_props(fs, noderev->id, pool);
      proplist = apr_hash_make(pool);

      SVN_ERR(svn_stream_open_readonly(&stream, filename, pool, pool));
      SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool));
      SVN_ERR(svn_stream_close(stream));
    }
  else if (noderev->prop_rep)
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      representation_t *rep = noderev->prop_rep;
      pair_cache_key_t key = { 0 };

      key.revision = rep->revision;
      key.second = rep->offset;
      if (ffd->properties_cache && SVN_IS_VALID_REVNUM(rep->revision))
        {
          svn_boolean_t is_cached;
          SVN_ERR(svn_cache__get((void **) proplist_p, &is_cached,
                                 ffd->properties_cache, &key, pool));
          if (is_cached)
            return SVN_NO_ERROR;
        }

      proplist = apr_hash_make(pool);
      SVN_ERR(svn_fs_fs__get_contents(&stream, fs, noderev->prop_rep, pool));
      SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool));
      SVN_ERR(svn_stream_close(stream));

      if (ffd->properties_cache && SVN_IS_VALID_REVNUM(rep->revision))
        SVN_ERR(svn_cache__set(ffd->properties_cache, &key, proplist, pool));
    }
  else
    {
      /* return an empty prop list if the node doesn't have any props */
      proplist = apr_hash_make(pool);
    }

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

/* Fetch the list of change in revision REV in FS and return it in *CHANGES.
 * Allocate the result in POOL.
 */
svn_error_t *
svn_fs_fs__get_changes(apr_array_header_t **changes,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  apr_off_t changes_offset;
  apr_file_t *revision_file;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* try cache lookup first */

  if (ffd->changes_cache)
    {
      svn_boolean_t found;
      SVN_ERR(svn_cache__get((void **) changes, &found, ffd->changes_cache,
                             &rev, pool));
      if (found)
        return SVN_NO_ERROR;
    }

  /* read changes from revision file */

  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, pool));
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&revision_file, fs, rev, pool));

  SVN_ERR(get_root_changes_offset(NULL, &changes_offset, revision_file, fs,
                                  rev, pool));

  SVN_ERR(aligned_seek(revision_file, changes_offset, pool));
  SVN_ERR(svn_fs_fs__read_changes(changes,
                      svn_stream_from_aprfile2(revision_file, TRUE, pool),
                                  pool));

  SVN_ERR(svn_io_file_close(revision_file, pool));

  /* cache for future reference */

  if (ffd->changes_cache)
    SVN_ERR(svn_cache__set(ffd->changes_cache, &rev, *changes, pool));

  SVN_ERR(dbg_log_access(fs, rev, changes_offset, *changes,
                         SVN_FS_FS__ITEM_TYPE_CHANGES, pool));

  return SVN_NO_ERROR;
}

