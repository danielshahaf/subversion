/*
 *  dump_editor.c: The svn_delta_editor_t editor used by svnrdump to
 *  dump revisions.
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

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_subst.h"
#include "svn_dirent_uri.h"

#include "private/svn_subr_private.h"
#include "private/svn_dep_compat.h"
#include "private/svn_editor.h"

#include "svnrdump.h"
#include <assert.h>

#define ARE_VALID_COPY_ARGS(p,r) ((p) && SVN_IS_VALID_REVNUM(r))

#if 0
#define LDR_DBG(x) SVN_DBG(x)
#else
#define LDR_DBG(x) while(0)
#endif

/* A directory baton used by all directory-related callback functions
 * in the dump editor.  */
struct dir_baton
{
  struct dump_edit_baton *eb;
  struct dir_baton *parent_dir_baton;

  /* Pool for per-directory allocations */
  apr_pool_t *pool;

  /* is this directory a new addition to this revision? */
  svn_boolean_t added;

  /* has this directory been written to the output stream? */
  svn_boolean_t written_out;

  /* the path to this directory */
  const char *repos_relpath; /* a relpath */

  /* Copyfrom info for the node, if any. */
  const char *copyfrom_path; /* a relpath */
  svn_revnum_t copyfrom_rev;

  /* Properties which were modified during change_dir_prop. */
  apr_hash_t *props;

  /* Properties which were deleted during change_dir_prop. */
  apr_hash_t *deleted_props;

  /* Hash of paths that need to be deleted, though some -might- be
     replaced.  Maps const char * paths to this dir_baton. Note that
     they're full paths, because that's what the editor driver gives
     us, although they're all really within this directory. */
  apr_hash_t *deleted_entries;

  /* Flags to trigger dumping props and record termination newlines. */
  svn_boolean_t dump_props;
  svn_boolean_t dump_newlines;
};

/* A file baton used by all file-related callback functions in the dump
 * editor */
struct file_baton
{
  struct dump_edit_baton *eb;
  struct dir_baton *parent_dir_baton;

  /* Pool for per-file allocations */
  apr_pool_t *pool;

  /* the path to this file */
  const char *repos_relpath; /* a relpath */

  /* Properties which were modified during change_file_prop. */
  apr_hash_t *props;

  /* Properties which were deleted during change_file_prop. */
  apr_hash_t *deleted_props;

  /* The checksum of the file the delta is being applied to */
  const char *base_checksum;

  /* Copy state and source information (if any). */
  svn_boolean_t is_copy;
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

  /* The action associate with this node. */
  enum svn_node_action action;

  /* Flags to trigger dumping props and text. */
  svn_boolean_t dump_text;
  svn_boolean_t dump_props;
};

/* A handler baton to be used in window_handler().  */
struct handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;
};

/* The baton used by the dump editor. */
struct dump_edit_baton {
  /* The output stream we write the dumpfile to */
  svn_stream_t *stream;

  /* A backdoor ra session to fetch additional information during the edit. */
  svn_ra_session_t *ra_session;

  /* The repository relpath of the anchor of the editor when driven
     via the RA update mechanism; NULL otherwise. (When the editor is
     driven via the RA "replay" mechanism instead, the editor is
     always anchored at the repository, we don't need to prepend an
     anchor path to the dumped node paths, and open_root() doesn't
     need to manufacture directory additions.)  */
  const char *update_anchor_relpath;

  /* Pool for per-revision allocations */
  apr_pool_t *pool;

  /* Temporary file used for textdelta application along with its
     absolute path; these two variables should be allocated in the
     per-edit-session pool */
  const char *delta_abspath;
  apr_file_t *delta_file;

  /* The revision we're currently dumping. */
  svn_revnum_t current_revision;

  /* The kind (file or directory) and baton of the item whose block of
     dump stream data has not been fully completed; NULL if there's no
     such item. */
  svn_node_kind_t pending_kind;
  void *pending_baton;
};

/* Make a directory baton to represent the directory at PATH (relative
 * to the EDIT_BATON).
 *
 * COPYFROM_PATH/COPYFROM_REV are the path/revision against which this
 * directory should be compared for changes. If the copyfrom
 * information is valid, the directory will be compared against its
 * copy source.
 *
 * PB is the directory baton of this directory's parent, or NULL if
 * this is the top-level directory of the edit.  ADDED indicates if
 * this directory is newly added in this revision.  Perform all
 * allocations in POOL.  */
static struct dir_baton *
make_dir_baton(const char *path,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_rev,
               void *edit_baton,
               struct dir_baton *pb,
               svn_boolean_t added,
               apr_pool_t *pool)
{
  struct dump_edit_baton *eb = edit_baton;
  struct dir_baton *new_db = apr_pcalloc(pool, sizeof(*new_db));
  const char *repos_relpath;

  /* Construct the full path of this node. */
  if (pb)
    repos_relpath = svn_relpath_canonicalize(path, pool);
  else
    repos_relpath = "";

  /* Strip leading slash from copyfrom_path so that the path is
     canonical and svn_relpath_join can be used */
  if (copyfrom_path)
    copyfrom_path = svn_relpath_canonicalize(copyfrom_path, pool);

  new_db->eb = eb;
  new_db->parent_dir_baton = pb;
  new_db->pool = pool;
  new_db->repos_relpath = repos_relpath;
  new_db->copyfrom_path = copyfrom_path
                            ? svn_relpath_canonicalize(copyfrom_path, pool)
                            : NULL;
  new_db->copyfrom_rev = copyfrom_rev;
  new_db->added = added;
  new_db->written_out = FALSE;
  new_db->props = apr_hash_make(pool);
  new_db->deleted_props = apr_hash_make(pool);
  new_db->deleted_entries = apr_hash_make(pool);

  return new_db;
}

/* Make a file baton to represent the directory at PATH (relative to
 * PB->eb).  PB is the directory baton of this directory's parent, or
 * NULL if this is the top-level directory of the edit.  Perform all
 * allocations in POOL.  */
static struct file_baton *
make_file_baton(const char *path,
                struct dir_baton *pb,
                apr_pool_t *pool)
{
  struct file_baton *new_fb = apr_pcalloc(pool, sizeof(*new_fb));

  new_fb->eb = pb->eb;
  new_fb->parent_dir_baton = pb;
  new_fb->pool = pool;
  new_fb->repos_relpath = svn_relpath_canonicalize(path, pool);
  new_fb->props = apr_hash_make(pool);
  new_fb->deleted_props = apr_hash_make(pool);
  new_fb->is_copy = FALSE;
  new_fb->copyfrom_path = NULL;
  new_fb->copyfrom_rev = SVN_INVALID_REVNUM;
  new_fb->action = svn_node_action_change;

  return new_fb;
}

/* Return in *HEADER and *CONTENT the headers and content for PROPS. */
static svn_error_t *
get_props_content(svn_stringbuf_t **header,
                  svn_stringbuf_t **content,
                  apr_hash_t *props,
                  apr_hash_t *deleted_props,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_stream_t *content_stream;
  apr_hash_t *normal_props;
  const char *buf;

  *content = svn_stringbuf_create_empty(result_pool);
  *header = svn_stringbuf_create_empty(result_pool);

  content_stream = svn_stream_from_stringbuf(*content, scratch_pool);

  SVN_ERR(svn_rdump__normalize_props(&normal_props, props, scratch_pool));
  SVN_ERR(svn_hash_write_incremental(normal_props, deleted_props,
                                     content_stream, "PROPS-END",
                                     scratch_pool));
  SVN_ERR(svn_stream_close(content_stream));

  /* Prop-delta: true */
  *header = svn_stringbuf_createf(result_pool, SVN_REPOS_DUMPFILE_PROP_DELTA
                                  ": true\n");

  /* Prop-content-length: 193 */
  buf = apr_psprintf(scratch_pool, SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH
                     ": %" APR_SIZE_T_FMT "\n", (*content)->len);
  svn_stringbuf_appendcstr(*header, buf);

  return SVN_NO_ERROR;
}

/* Extract and dump properties stored in PROPS and property deletions
 * stored in DELETED_PROPS. If TRIGGER_VAR is not NULL, it is set to
 * FALSE.
 *
 * If PROPSTRING is non-NULL, set *PROPSTRING to a string containing
 * the content block of the property changes; otherwise, dump that to
 * the stream, too.
 */
static svn_error_t *
do_dump_props(svn_stringbuf_t **propstring,
              svn_stream_t *stream,
              apr_hash_t *props,
              apr_hash_t *deleted_props,
              svn_boolean_t *trigger_var,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *header;
  svn_stringbuf_t *content;
  apr_size_t len;

  if (trigger_var && !*trigger_var)
    return SVN_NO_ERROR;

  SVN_ERR(get_props_content(&header, &content, props, deleted_props,
                            result_pool, scratch_pool));
  len = header->len;
  SVN_ERR(svn_stream_write(stream, header->data, &len));

  if (propstring)
    {
      *propstring = content;
    }
  else
    {
      /* Content-length: 14 */
      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                SVN_REPOS_DUMPFILE_CONTENT_LENGTH
                                ": %" APR_SIZE_T_FMT "\n\n",
                                content->len));

      len = content->len;
      SVN_ERR(svn_stream_write(stream, content->data, &len));

      /* No text is going to be dumped. Write a couple of newlines and
         wait for the next node/ revision. */
      SVN_ERR(svn_stream_puts(stream, "\n\n"));

      /* Cleanup so that data is never dumped twice. */
      apr_hash_clear(props);
      apr_hash_clear(deleted_props);
      if (trigger_var)
        *trigger_var = FALSE;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
do_dump_newlines(struct dump_edit_baton *eb,
                 svn_boolean_t *trigger_var,
                 apr_pool_t *pool)
{
  if (trigger_var && *trigger_var)
    {
      SVN_ERR(svn_stream_puts(eb->stream, "\n\n"));
      *trigger_var = FALSE;
    }
  return SVN_NO_ERROR;
}

/*
 * Write out a node record for PATH of type KIND under EB->FS_ROOT.
 * ACTION describes what is happening to the node (see enum
 * svn_node_action). Write record to writable EB->STREAM, using
 * EB->BUFFER to write in chunks.
 *
 * If the node was itself copied, IS_COPY is TRUE and the
 * path/revision of the copy source are in COPYFROM_PATH/COPYFROM_REV.
 * If IS_COPY is FALSE, yet COPYFROM_PATH/COPYFROM_REV are valid, this
 * node is part of a copied subtree.
 */
static svn_error_t *
dump_node(struct dump_edit_baton *eb,
          const char *repos_relpath,
          struct dir_baton *db,
          struct file_baton *fb,
          enum svn_node_action action,
          svn_boolean_t is_copy,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_rev,
          apr_pool_t *pool)
{
  const char *node_relpath = repos_relpath;

  assert(svn_relpath_is_canonical(repos_relpath));
  assert(!copyfrom_path || svn_relpath_is_canonical(copyfrom_path));
  assert(! (db && fb));

  /* Add the edit root relpath prefix if necessary. */
  if (eb->update_anchor_relpath)
    node_relpath = svn_relpath_join(eb->update_anchor_relpath,
                                    node_relpath, pool);

  /* Node-path: ... */
  SVN_ERR(svn_stream_printf(eb->stream, pool,
                            SVN_REPOS_DUMPFILE_NODE_PATH ": %s\n",
                            node_relpath));

  /* Node-kind: "file" | "dir" */
  if (fb)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_KIND ": file\n"));
  else if (db)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_KIND ": dir\n"));


  /* Write the appropriate Node-action header */
  switch (action)
    {
    case svn_node_action_change:
      /* We are here after a change_file_prop or change_dir_prop. They
         set up whatever dump_props they needed to- nothing to
         do here but print node action information.

         Node-action: change.  */
      SVN_ERR(svn_stream_puts(eb->stream,
                              SVN_REPOS_DUMPFILE_NODE_ACTION ": change\n"));
      break;

    case svn_node_action_replace:
      if (is_copy)
        {
          /* Delete the original, and then re-add the replacement as a
             copy using recursive calls into this function. */
          SVN_ERR(dump_node(eb, repos_relpath, db, fb, svn_node_action_delete,
                            FALSE, NULL, SVN_INVALID_REVNUM, pool));
          SVN_ERR(dump_node(eb, repos_relpath, db, fb, svn_node_action_add,
                            is_copy, copyfrom_path, copyfrom_rev, pool));
        }
      else
        {
          /* Node-action: replace */
          SVN_ERR(svn_stream_puts(eb->stream,
                                  SVN_REPOS_DUMPFILE_NODE_ACTION
                                  ": replace\n"));

          /* Wait for a change_*_prop to be called before dumping
             anything */
          if (fb)
            fb->dump_props = TRUE;
          else if (db)
            db->dump_props = TRUE;
        }
      break;

    case svn_node_action_delete:
      /* Node-action: delete */
      SVN_ERR(svn_stream_puts(eb->stream,
                              SVN_REPOS_DUMPFILE_NODE_ACTION ": delete\n"));

      /* We can leave this routine quietly now. Nothing more to do-
         print a couple of newlines because we're not dumping props or
         text. */
      SVN_ERR(svn_stream_puts(eb->stream, "\n\n"));

      break;

    case svn_node_action_add:
      /* Node-action: add */
      SVN_ERR(svn_stream_puts(eb->stream,
                              SVN_REPOS_DUMPFILE_NODE_ACTION ": add\n"));

      if (is_copy)
        {
          /* Node-copyfrom-rev / Node-copyfrom-path */
          SVN_ERR(svn_stream_printf(eb->stream, pool,
                                    SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV
                                    ": %ld\n"
                                    SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH
                                    ": %s\n",
                                    copyfrom_rev, copyfrom_path));

          /* Ugly hack: If a directory was copied from a previous
             revision, nothing like close_file() will be called to write two
             blank lines. If change_dir_prop() is called, props are dumped
             (along with the necessary PROPS-END\n\n and we're good. So
             set DUMP_NEWLINES here to print the newlines unless
             change_dir_prop() is called next otherwise the `svnadmin load`
             parser will fail.  */
          if (db)
            db->dump_newlines = TRUE;
        }
      else
        {
          /* fb->dump_props (for files) is handled in close_file()
             which is called immediately.

             However, directories are not closed until all the work
             inside them has been done; db->dump_props (for directories)
             is handled (via dump_pending()) in all the functions that
             can possibly be called after add_directory():

               - add_directory()
               - open_directory()
               - delete_entry()
               - close_directory()
               - add_file()
               - open_file()

             change_dir_prop() is a special case. */
          if (fb)
            fb->dump_props = TRUE;
          else if (db)
            db->dump_props = TRUE;
        }

      break;
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
dump_mkdir(struct dump_edit_baton *eb,
           const char *repos_relpath,
           apr_pool_t *pool)
{
  svn_stringbuf_t *prop_header, *prop_content;
  apr_size_t len;
  const char *buf;

  /* Node-path: ... */
  SVN_ERR(svn_stream_printf(eb->stream, pool,
                            SVN_REPOS_DUMPFILE_NODE_PATH ": %s\n",
                            repos_relpath));

  /* Node-kind: dir */
  SVN_ERR(svn_stream_printf(eb->stream, pool,
                            SVN_REPOS_DUMPFILE_NODE_KIND ": dir\n"));

  /* Node-action: add */
  SVN_ERR(svn_stream_puts(eb->stream,
                          SVN_REPOS_DUMPFILE_NODE_ACTION ": add\n"));

  /* Dump the (empty) property block. */
  SVN_ERR(get_props_content(&prop_header, &prop_content,
                            apr_hash_make(pool), apr_hash_make(pool),
                            pool, pool));
  len = prop_header->len;
  SVN_ERR(svn_stream_write(eb->stream, prop_header->data, &len));
  len = prop_content->len;
  buf = apr_psprintf(pool, SVN_REPOS_DUMPFILE_CONTENT_LENGTH
                     ": %" APR_SIZE_T_FMT "\n", len);
  SVN_ERR(svn_stream_puts(eb->stream, buf));
  SVN_ERR(svn_stream_puts(eb->stream, "\n"));
  SVN_ERR(svn_stream_write(eb->stream, prop_content->data, &len));

  /* Newlines to tie it all off. */
  SVN_ERR(svn_stream_puts(eb->stream, "\n\n"));

  return SVN_NO_ERROR;
}

/* Dump pending items from the specified node, to allow starting the dump
   of a child node */
static svn_error_t *
dump_pending(struct dump_edit_baton *eb,
             apr_pool_t *scratch_pool)
{
  if (! eb->pending_baton)
    return SVN_NO_ERROR;

  if (eb->pending_kind == svn_node_dir)
    {
      struct dir_baton *db = eb->pending_baton;

      /* Some pending properties to dump? */
      SVN_ERR(do_dump_props(NULL, eb->stream, db->props, db->deleted_props,
                            &(db->dump_props), db->pool, scratch_pool));

      /* Some pending newlines to dump? */
      SVN_ERR(do_dump_newlines(eb, &(db->dump_newlines), scratch_pool));
    }
  else if (eb->pending_kind == svn_node_file)
    {
      struct file_baton *fb = eb->pending_baton;

      /* Some pending properties to dump? */
      SVN_ERR(do_dump_props(NULL, eb->stream, fb->props, fb->deleted_props,
                            &(fb->dump_props), fb->pool, scratch_pool));
    }
  else
    abort();

  /* Anything that was pending is pending no longer. */
  eb->pending_baton = NULL;
  eb->pending_kind = svn_node_none;

  return SVN_NO_ERROR;
}



/*** Editor Function Implementations ***/

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct dump_edit_baton *eb = edit_baton;
  struct dir_baton *new_db = NULL;

  /* Clear the per-revision pool after each revision */
  svn_pool_clear(eb->pool);

  LDR_DBG(("open_root %p\n", *root_baton));

  if (eb->update_anchor_relpath)
    {
      int i;
      const char *parent_path = eb->update_anchor_relpath;
      apr_array_header_t *dirs_to_add =
        apr_array_make(pool, 4, sizeof(const char *));
      apr_pool_t *iterpool = svn_pool_create(pool);

      while (! svn_path_is_empty(parent_path))
        {
          APR_ARRAY_PUSH(dirs_to_add, const char *) = parent_path;
          parent_path = svn_relpath_dirname(parent_path, pool);
        }

      for (i = dirs_to_add->nelts; i; --i)
        {
          const char *dir_to_add =
            APR_ARRAY_IDX(dirs_to_add, i - 1, const char *);

          svn_pool_clear(iterpool);

          /* For parents of the source directory, we just manufacture
             the adds ourselves. */
          if (i > 1)
            {
              SVN_ERR(dump_mkdir(eb, dir_to_add, iterpool));
            }
          else
            {
              /* ... but for the source directory itself, we'll defer
                 to letting the typical plumbing handle this task. */
              new_db = make_dir_baton(NULL, NULL, SVN_INVALID_REVNUM,
                                      edit_baton, NULL, TRUE, pool);
              SVN_ERR(dump_node(eb, new_db->repos_relpath, new_db,
                                NULL, svn_node_action_add, FALSE,
                                NULL, SVN_INVALID_REVNUM, pool));

              /* Remember that we've started but not yet finished
                 handling this directory. */
              new_db->written_out = TRUE;
              eb->pending_baton = new_db;
              eb->pending_kind = svn_node_dir;
            }
        }
      svn_pool_destroy(iterpool);
    }

  if (! new_db)
    {
      new_db = make_dir_baton(NULL, NULL, SVN_INVALID_REVNUM,
                              edit_baton, NULL, FALSE, pool);
    }

  *root_baton = new_db;
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;

  LDR_DBG(("delete_entry %s\n", path));

  SVN_ERR(dump_pending(pb->eb, pool));

  /* We don't dump this deletion immediate.  Rather, we add this path
     to the deleted_entries of the parent directory baton.  That way,
     we can tell (later) an addition from a replacement.  All the real
     deletions get handled in close_directory().  */
  svn_hash_sets(pb->deleted_entries, apr_pstrdup(pb->eb->pool, path), pb);

  return SVN_NO_ERROR;
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  void *val;
  struct dir_baton *new_db;
  svn_boolean_t is_copy;

  LDR_DBG(("add_directory %s\n", path));

  SVN_ERR(dump_pending(pb->eb, pool));

  new_db = make_dir_baton(path, copyfrom_path, copyfrom_rev, pb->eb,
                          pb, TRUE, pb->eb->pool);

  /* This might be a replacement -- is the path already deleted? */
  val = svn_hash_gets(pb->deleted_entries, path);

  /* Detect an add-with-history */
  is_copy = ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev);

  /* Dump the node */
  SVN_ERR(dump_node(pb->eb, new_db->repos_relpath, new_db, NULL,
                    val ? svn_node_action_replace : svn_node_action_add,
                    is_copy,
                    is_copy ? new_db->copyfrom_path : NULL,
                    is_copy ? copyfrom_rev : SVN_INVALID_REVNUM,
                    pool));

  if (val)
    /* Delete the path, it's now been dumped */
    svn_hash_sets(pb->deleted_entries, path, NULL);

  /* Remember that we've started, but not yet finished handling this
     directory. */
  new_db->written_out = TRUE;
  pb->eb->pending_baton = new_db;
  pb->eb->pending_kind = svn_node_dir;

  *child_baton = new_db;
  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *new_db;
  const char *copyfrom_path = NULL;
  svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

  LDR_DBG(("open_directory %s\n", path));

  SVN_ERR(dump_pending(pb->eb, pool));

  /* If the parent directory has explicit comparison path and rev,
     record the same for this one. */
  if (ARE_VALID_COPY_ARGS(pb->copyfrom_path, pb->copyfrom_rev))
    {
      copyfrom_path = svn_relpath_join(pb->copyfrom_path,
                                       svn_relpath_basename(path, NULL),
                                       pb->eb->pool);
      copyfrom_rev = pb->copyfrom_rev;
    }

  new_db = make_dir_baton(path, copyfrom_path, copyfrom_rev, pb->eb, pb,
                          FALSE, pb->eb->pool);

  *child_baton = new_db;
  return SVN_NO_ERROR;
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  apr_hash_index_t *hi;
  svn_boolean_t this_pending;

  LDR_DBG(("close_directory %p\n", dir_baton));

  /* Remember if this directory is the one currently pending. */
  this_pending = (db->eb->pending_baton == db);

  SVN_ERR(dump_pending(db->eb, pool));

  /* If this directory was pending, then dump_pending() should have
     taken care of all the props and such.  Of course, the only way
     that would be the case is if this directory was added/replaced.

     Otherwise, if stuff for this directory has already been written
     out (at some point in the past, prior to our handling other
     nodes), we might need to generate a second "change" record just
     to carry the information we've since learned about the
     directory. */
  if ((! this_pending) && (db->dump_props))
    {
      SVN_ERR(dump_node(db->eb, db->repos_relpath, db, NULL,
                        svn_node_action_change, FALSE,
                        NULL, SVN_INVALID_REVNUM, pool));
      db->eb->pending_baton = db;
      db->eb->pending_kind = svn_node_dir;
      SVN_ERR(dump_pending(db->eb, pool));
    }

  /* Dump the deleted directory entries */
  for (hi = apr_hash_first(pool, db->deleted_entries); hi;
       hi = apr_hash_next(hi))
    {
      const char *path = svn__apr_hash_index_key(hi);

      SVN_ERR(dump_node(db->eb, path, NULL, NULL, svn_node_action_delete,
                        FALSE, NULL, SVN_INVALID_REVNUM, pool));
    }

  /* ### should be unnecessary */
  apr_hash_clear(db->deleted_entries);

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb;
  void *val;

  LDR_DBG(("add_file %s\n", path));

  SVN_ERR(dump_pending(pb->eb, pool));

  /* Make the file baton. */
  fb = make_file_baton(path, pb, pool);

  /* This might be a replacement -- is the path already deleted? */
  val = svn_hash_gets(pb->deleted_entries, path);

  /* Detect add-with-history. */
  if (ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev))
    {
      fb->copyfrom_path = svn_relpath_canonicalize(copyfrom_path, fb->pool);
      fb->copyfrom_rev = copyfrom_rev;
      fb->is_copy = TRUE;
    }
  fb->action = val ? svn_node_action_replace : svn_node_action_add;

  /* Delete the path, it's now been dumped. */
  if (val)
    svn_hash_sets(pb->deleted_entries, path, NULL);

  *file_baton = fb;
  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t ancestor_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb;

  LDR_DBG(("open_file %s\n", path));

  SVN_ERR(dump_pending(pb->eb, pool));

  /* Make the file baton. */
  fb = make_file_baton(path, pb, pool);

  /* If the parent directory has explicit copyfrom path and rev,
     record the same for this one. */
  if (ARE_VALID_COPY_ARGS(pb->copyfrom_path, pb->copyfrom_rev))
    {
      fb->copyfrom_path = svn_relpath_join(pb->copyfrom_path,
                                           svn_relpath_basename(path, NULL),
                                           pb->eb->pool);
      fb->copyfrom_rev = pb->copyfrom_rev;
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *parent_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  svn_boolean_t this_pending;

  LDR_DBG(("change_dir_prop %p\n", parent_baton));

  /* This directory is not pending, but something else is, so handle
     the "something else".  */
  this_pending = (db->eb->pending_baton == db);
  if (! this_pending)
    SVN_ERR(dump_pending(db->eb, pool));

  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  if (value)
    svn_hash_sets(db->props,
                  apr_pstrdup(db->pool, name),
                  svn_string_dup(value, db->pool));
  else
    svn_hash_sets(db->deleted_props, apr_pstrdup(db->pool, name), "");

  /* Make sure we eventually output the props, and disable printing
     a couple of extra newlines */
  db->dump_newlines = FALSE;
  db->dump_props = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  LDR_DBG(("change_file_prop %p\n", file_baton));

  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  if (value)
    svn_hash_sets(fb->props,
                  apr_pstrdup(fb->pool, name),
                  svn_string_dup(value, fb->pool));
  else
    svn_hash_sets(fb->deleted_props, apr_pstrdup(fb->pool, name), "");

  /* Dump the property headers and wait; close_file might need
     to write text headers too depending on whether
     apply_textdelta is called */
  fb->dump_props = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  static svn_error_t *err;

  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && !err)
    return SVN_NO_ERROR;

  if (err)
    SVN_ERR(err);

  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton, const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct dump_edit_baton *eb = fb->eb;
  struct handler_baton *hb;
  svn_stream_t *delta_filestream;

  LDR_DBG(("apply_textdelta %p\n", file_baton));

  /* This is custom handler_baton, allocated from a separate pool.  */
  hb = apr_pcalloc(eb->pool, sizeof(*hb));

  /* Use a temporary file to measure the Text-content-length */
  delta_filestream = svn_stream_from_aprfile2(eb->delta_file, TRUE, pool);

  /* Prepare to write the delta to the delta_filestream */
  svn_txdelta_to_svndiff3(&(hb->apply_handler), &(hb->apply_baton),
                          delta_filestream, 0,
                          SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, pool);

  /* Record that there's text to be dumped, and its base checksum. */
  fb->dump_text = TRUE;
  fb->base_checksum = apr_pstrdup(eb->pool, base_checksum);

  /* The actual writing takes place when this function has
     finished. Set handler and handler_baton now so for
     window_handler() */
  *handler = window_handler;
  *handler_baton = hb;

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct dump_edit_baton *eb = fb->eb;
  apr_finfo_t *info = apr_pcalloc(pool, sizeof(apr_finfo_t));
  svn_stringbuf_t *propstring;

  LDR_DBG(("close_file %p\n", file_baton));

  SVN_ERR(dump_pending(eb, pool));

  /* Dump the node. */
  SVN_ERR(dump_node(eb, fb->repos_relpath, NULL, fb,
                    fb->action, fb->is_copy, fb->copyfrom_path,
                    fb->copyfrom_rev, pool));

  /* Some pending properties to dump?  We'll dump just the headers for
     now, then dump the actual propchange content only after dumping
     the text headers too (if present). */
  SVN_ERR(do_dump_props(&propstring, eb->stream, fb->props, fb->deleted_props,
                        &(fb->dump_props), pool, pool));

  /* Dump the text headers */
  if (fb->dump_text)
    {
      apr_status_t err;

      /* Text-delta: true */
      SVN_ERR(svn_stream_puts(eb->stream,
                              SVN_REPOS_DUMPFILE_TEXT_DELTA
                              ": true\n"));

      err = apr_file_info_get(info, APR_FINFO_SIZE, eb->delta_file);
      if (err)
        SVN_ERR(svn_error_wrap_apr(err, NULL));

      if (fb->base_checksum)
        /* Text-delta-base-md5: */
        SVN_ERR(svn_stream_printf(eb->stream, pool,
                                  SVN_REPOS_DUMPFILE_TEXT_DELTA_BASE_MD5
                                  ": %s\n",
                                  fb->base_checksum));

      /* Text-content-length: 39 */
      SVN_ERR(svn_stream_printf(eb->stream, pool,
                                SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH
                                ": %lu\n",
                                (unsigned long)info->size));

      /* Text-content-md5: 82705804337e04dcd0e586bfa2389a7f */
      SVN_ERR(svn_stream_printf(eb->stream, pool,
                                SVN_REPOS_DUMPFILE_TEXT_CONTENT_MD5
                                ": %s\n",
                                text_checksum));
    }

  /* Content-length: 1549 */
  /* If both text and props are absent, skip this header */
  if (fb->dump_props)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_CONTENT_LENGTH
                              ": %ld\n\n",
                              (unsigned long)info->size + propstring->len));
  else if (fb->dump_text)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_CONTENT_LENGTH
                              ": %ld\n\n",
                              (unsigned long)info->size));

  /* Dump the props now */
  if (fb->dump_props)
    {
      SVN_ERR(svn_stream_write(eb->stream, propstring->data,
                               &(propstring->len)));

      /* Cleanup */
      fb->dump_props = FALSE;
      apr_hash_clear(fb->props);
      apr_hash_clear(fb->deleted_props);
    }

  /* Dump the text */
  if (fb->dump_text)
    {
      /* Seek to the beginning of the delta file, map it to a stream,
         and copy the stream to eb->stream. Then close the stream and
         truncate the file so we can reuse it for the next textdelta
         application. Note that the file isn't created, opened or
         closed here */
      svn_stream_t *delta_filestream;
      apr_off_t offset = 0;

      SVN_ERR(svn_io_file_seek(eb->delta_file, APR_SET, &offset, pool));
      delta_filestream = svn_stream_from_aprfile2(eb->delta_file, TRUE, pool);
      SVN_ERR(svn_stream_copy3(delta_filestream, eb->stream, NULL, NULL, pool));

      /* Cleanup */
      SVN_ERR(svn_stream_close(delta_filestream));
      SVN_ERR(svn_io_file_trunc(eb->delta_file, 0, pool));
    }

  /* Write a couple of blank lines for matching output with `svnadmin
     dump` */
  SVN_ERR(svn_stream_puts(eb->stream, "\n\n"));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit(void *edit_baton, apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  struct dump_edit_baton *eb = baton;
  svn_stream_t *fstream;
  svn_error_t *err;

  if (path[0] == '/')
    path += 1;

  if (! SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->current_revision - 1;

  SVN_ERR(svn_stream_open_unique(&fstream, filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 result_pool, scratch_pool));

  err = svn_ra_get_file(eb->ra_session, path, base_revision,
                        fstream, NULL, NULL, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      SVN_ERR(svn_stream_close(fstream));

      *filename = NULL;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  SVN_ERR(svn_stream_close(fstream));

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct dump_edit_baton *eb = baton;
  svn_node_kind_t node_kind;

  if (path[0] == '/')
    path += 1;

  if (! SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->current_revision - 1;

  SVN_ERR(svn_ra_check_path(eb->ra_session, path, base_revision, &node_kind,
                            scratch_pool));

  if (node_kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(eb->ra_session, path, base_revision,
                              NULL, NULL, props, result_pool));
    }
  else if (node_kind == svn_node_dir)
    {
      apr_array_header_t *tmp_props;

      SVN_ERR(svn_ra_get_dir2(eb->ra_session, NULL, NULL, props, path,
                              base_revision, 0 /* Dirent fields */,
                              result_pool));
      tmp_props = svn_prop_hash_to_array(*props, result_pool);
      SVN_ERR(svn_categorize_props(tmp_props, NULL, NULL, &tmp_props,
                                   result_pool));
      *props = svn_prop_array_to_hash(tmp_props, result_pool);
    }
  else
    {
      *props = apr_hash_make(result_pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_kind_func(svn_node_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  struct dump_edit_baton *eb = baton;

  if (path[0] == '/')
    path += 1;

  if (! SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->current_revision - 1;

  SVN_ERR(svn_ra_check_path(eb->ra_session, path, base_revision, kind,
                            scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_rdump__get_dump_editor(const svn_delta_editor_t **editor,
                           void **edit_baton,
                           svn_revnum_t revision,
                           svn_stream_t *stream,
                           svn_ra_session_t *ra_session,
                           const char *update_anchor_relpath,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *pool)
{
  struct dump_edit_baton *eb;
  svn_delta_editor_t *de;
  svn_delta_shim_callbacks_t *shim_callbacks =
                                        svn_delta_shim_callbacks_default(pool);

  eb = apr_pcalloc(pool, sizeof(struct dump_edit_baton));
  eb->stream = stream;
  eb->ra_session = ra_session;
  eb->update_anchor_relpath = update_anchor_relpath;
  eb->current_revision = revision;
  eb->pending_kind = svn_node_none;

  /* Create a special per-revision pool */
  eb->pool = svn_pool_create(pool);

  /* Open a unique temporary file for all textdelta applications in
     this edit session. The file is automatically closed and cleaned
     up when the edit session is done. */
  SVN_ERR(svn_io_open_unique_file3(&(eb->delta_file), &(eb->delta_abspath),
                                   NULL, svn_io_file_del_on_close, pool, pool));

  de = svn_delta_default_editor(pool);
  de->open_root = open_root;
  de->delete_entry = delete_entry;
  de->add_directory = add_directory;
  de->open_directory = open_directory;
  de->close_directory = close_directory;
  de->change_dir_prop = change_dir_prop;
  de->change_file_prop = change_file_prop;
  de->apply_textdelta = apply_textdelta;
  de->add_file = add_file;
  de->open_file = open_file;
  de->close_file = close_file;
  de->close_edit = close_edit;

  /* Set the edit_baton and editor. */
  *edit_baton = eb;
  *editor = de;

  /* Wrap this editor in a cancellation editor. */
  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                            de, eb, editor, edit_baton, pool));

  shim_callbacks->fetch_base_func = fetch_base_func;
  shim_callbacks->fetch_props_func = fetch_props_func;
  shim_callbacks->fetch_kind_func = fetch_kind_func;
  shim_callbacks->fetch_baton = eb;

  SVN_ERR(svn_editor__insert_shims(editor, edit_baton, *editor, *edit_baton,
                                   NULL, NULL, shim_callbacks, pool, pool));

  return SVN_NO_ERROR;
}
