/*
 * propget-cmd.c -- Print properties and values of files/dirs
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_xml.h"
#include "cl.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"


/*** Code. ***/

static svn_error_t *
stream_write(svn_stream_t *out,
             const char *data,
             apr_size_t len)
{
  apr_size_t write_len = len;

  /* We're gonna bail on an incomplete write here only because we know
     that this stream is really stdout, which should never be blocking
     on us. */
  SVN_ERR(svn_stream_write(out, data, &write_len));
  if (write_len != len)
    return svn_error_create(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                            _("Error writing to stream"));
  return SVN_NO_ERROR;
}


static svn_error_t *
print_properties_xml(const char *pname,
                     apr_hash_t *props,
                     apr_array_header_t *inherited_props,
                     apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = NULL;
  int i;
  svn_stringbuf_t *sb;

  if (inherited_props && inherited_props->nelts)
    {
      iterpool = svn_pool_create(pool);

      for (i = 0; i < inherited_props->nelts; i++)
        {
          svn_prop_inherited_item_t *iprop =
           APR_ARRAY_IDX(inherited_props, i, svn_prop_inherited_item_t *);
          svn_string_t *propval = svn__apr_hash_index_val(
            apr_hash_first(pool, iprop->prop_hash));

          sb = NULL;
          svn_pool_clear(iterpool);
          svn_xml_make_open_tag(&sb, iterpool, svn_xml_normal, "target",
                            "path", iprop->path_or_url, NULL);

          svn_cmdline__print_xml_prop(&sb, pname, propval, TRUE, iterpool);
          svn_xml_make_close_tag(&sb, iterpool, "target");

          SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
        }
    }

  if (iterpool == NULL)
    iterpool = svn_pool_create(iterpool);

  for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
    {
      const char *filename = svn__apr_hash_index_key(hi);
      svn_string_t *propval = svn__apr_hash_index_val(hi);

      sb = NULL;
      svn_pool_clear(iterpool);

      svn_xml_make_open_tag(&sb, iterpool, svn_xml_normal, "target",
                        "path", filename, NULL);
      svn_cmdline__print_xml_prop(&sb, pname, propval, FALSE, iterpool);
      svn_xml_make_close_tag(&sb, iterpool, "target");

      SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
    }

  if (iterpool)
    svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Print the property PNAME_UTF with the value PROPVAL set on ABSPATH_OR_URL
   to the stream OUT.

   If INHERITED_PROPERTY is true then the property described is inherited,
   otherwise it is explicit.

   WC_PATH_PREFIX is the absolute path of the current working directory (and
   is ignored if ABSPATH_OR_URL is a URL).

   All other arguments are as per print_properties. */
static svn_error_t *
print_single_prop(svn_string_t *propval,
                  const char *abspath_or_URL,
                  const char *wc_path_prefix,
                  svn_stream_t *out,
                  const char *pname_utf8,
                  svn_boolean_t print_filenames,
                  svn_boolean_t omit_newline,
                  svn_boolean_t like_proplist,
                  svn_boolean_t inherited_property,
                  apr_pool_t *scratch_pool)
{
  if (print_filenames)
    {
      const char *header;

      /* Print the file name. */

      if (! svn_path_is_url(abspath_or_URL))
        abspath_or_URL = svn_cl__local_style_skip_ancestor(wc_path_prefix,
                                                           abspath_or_URL,
                                                           scratch_pool);

      /* In verbose mode, print exactly same as "proplist" does;
       * otherwise, print a brief header. */
      if (inherited_property)
        header = apr_psprintf(scratch_pool, like_proplist
                              ? _("Properties inherited from '%s':\n")
                              : "%s - ", abspath_or_URL);
      else
        header = apr_psprintf(scratch_pool, like_proplist
                              ? _("Properties on '%s':\n")
                              : "%s - ", abspath_or_URL);
      SVN_ERR(svn_cmdline_cstring_from_utf8(&header, header, scratch_pool));
      SVN_ERR(svn_subst_translate_cstring2(header, &header,
                                           APR_EOL_STR,  /* 'native' eol */
                                           FALSE, /* no repair */
                                           NULL,  /* no keywords */
                                           FALSE, /* no expansion */
                                           scratch_pool));
      SVN_ERR(stream_write(out, header, strlen(header)));
    }

  if (like_proplist)
    {
      /* Print the property name and value just as "proplist -v" does */
      apr_hash_t *hash = apr_hash_make(scratch_pool);

      apr_hash_set(hash, pname_utf8, APR_HASH_KEY_STRING, propval);
      SVN_ERR(svn_cl__print_prop_hash(out, hash, FALSE, scratch_pool));
    }
  else
    {
      /* If this is a special Subversion property, it is stored as
         UTF8, so convert to the native format. */
      if (svn_prop_needs_translation(pname_utf8))
        SVN_ERR(svn_subst_detranslate_string(&propval, propval,
                                             TRUE, scratch_pool));

      SVN_ERR(stream_write(out, propval->data, propval->len));

      if (! omit_newline)
        SVN_ERR(stream_write(out, APR_EOL_STR,
                             strlen(APR_EOL_STR)));
    }
  return SVN_NO_ERROR;
}

/* Print the properties in PROPS and/or *INHERITED_PROPS to the stream OUT.
   PROPS is a hash mapping (const char *) path to (svn_string_t) property
   value.  INHERITED_PROPS is a depth-first ordered array of
   svn_prop_inherited_item_t * structures.
   
   PROPS may be an empty hash, but is never null.  INHERITED_PROPS may be
   null.
   
   If IS_URL is true, all paths in PROPS are URLs, else all paths are local
   paths.
   
   PNAME_UTF8 is the property name of all the properties.
   
   If PRINT_FILENAMES is true, print the item's path before each property.
   
   If OMIT_NEWLINE is true, don't add a newline at the end of each property.
   
   If LIKE_PROPLIST is true, print everything in a more verbose format
   like "svn proplist -v" does. */
static svn_error_t *
print_properties(svn_stream_t *out,
                 const char *pname_utf8,
                 apr_hash_t *props,
                 apr_array_header_t *inherited_props,
                 svn_boolean_t print_filenames,
                 svn_boolean_t omit_newline,
                 svn_boolean_t like_proplist,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);
  const char *path_prefix;

  SVN_ERR(svn_dirent_get_absolute(&path_prefix, "", pool));

  if (inherited_props)
    {
      int i;

      svn_pool_clear(iterpool);

      for (i = 0; i < inherited_props->nelts; i++)
        {
          svn_prop_inherited_item_t *iprop =
            APR_ARRAY_IDX(inherited_props, i, svn_prop_inherited_item_t *);
          svn_string_t *propval = svn__apr_hash_index_val(apr_hash_first(pool,
                                                          iprop->prop_hash));
          SVN_ERR(print_single_prop(propval,
                                    iprop->path_or_url,
                                    path_prefix, out, pname_utf8,
                                    print_filenames, omit_newline,
                                    like_proplist, TRUE, iterpool));
        }
    }

  for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
    {
      const char *filename = svn__apr_hash_index_key(hi);
      svn_string_t *propval = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(print_single_prop(propval, filename, path_prefix,
                                out, pname_utf8, print_filenames,
                                omit_newline, like_proplist, FALSE,
                                iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propget(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  svn_stream_t *out;

  if (opt_state->verbose && (opt_state->revprop || opt_state->strict
                             || opt_state->xml))
    return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                            _("--verbose cannot be used with --revprop or "
                              "--strict or --xml"));

  /* PNAME is first argument (and PNAME_UTF8 will be a UTF-8 version
     thereof) */
  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  pname = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_utf_cstring_to_utf8(&pname_utf8, pname, pool));
  if (! svn_prop_name_is_valid(pname_utf8))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is not a valid Subversion property name"),
                             pname_utf8);

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  /* Open a stream to stdout. */
  SVN_ERR(svn_stream_for_stdout(&out, pool));

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;
      svn_string_t *propval;

      if (opt_state->show_inherited_props)
        return svn_error_create(
          SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("--show-inherited-props can't be used with --revprop"));

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, ctx, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_get(pname_utf8, &propval,
                                     URL, &(opt_state->start_revision),
                                     &rev, ctx, pool));

      if (propval != NULL)
        {
          if (opt_state->xml)
            {
              svn_stringbuf_t *sb = NULL;
              char *revstr = apr_psprintf(pool, "%ld", rev);

              SVN_ERR(svn_cl__xml_print_header("properties", pool));

              svn_xml_make_open_tag(&sb, pool, svn_xml_normal,
                                    "revprops",
                                    "rev", revstr, NULL);

              svn_cmdline__print_xml_prop(&sb, pname_utf8, propval, FALSE,
                                          pool);

              svn_xml_make_close_tag(&sb, pool, "revprops");

              SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
              SVN_ERR(svn_cl__xml_print_footer("properties", pool));
            }
          else
            {
              svn_string_t *printable_val = propval;

              /* If this is a special Subversion property, it is stored as
                 UTF8 and LF, so convert to the native locale and eol-style. */

              if (svn_prop_needs_translation(pname_utf8))
                SVN_ERR(svn_subst_detranslate_string(&printable_val, propval,
                                                     TRUE, pool));

              SVN_ERR(stream_write(out, printable_val->data,
                                   printable_val->len));
              if (! opt_state->strict)
                SVN_ERR(stream_write(out, APR_EOL_STR, strlen(APR_EOL_STR)));
            }
        }
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      int i;

      if (opt_state->xml)
        SVN_ERR(svn_cl__xml_print_header("properties", subpool));

      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      /* Strict mode only makes sense for a single target.  So make
         sure we have only a single target, and that we're not being
         asked to recurse on that target. */
      if (opt_state->strict
          && ((targets->nelts > 1) || (opt_state->depth != svn_depth_empty)))
        return svn_error_create
          (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
           _("Strict output of property values only available for single-"
             "target, non-recursive propget operations"));

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          apr_hash_t *props;
          svn_boolean_t print_filenames;
          svn_boolean_t omit_newline;
          svn_boolean_t like_proplist;
          const char *truepath;
          svn_opt_revision_t peg_revision;
          apr_array_header_t *inherited_props;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          /* Check for a peg revision. */
          SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                     subpool));

          if (!svn_path_is_url(truepath))
            SVN_ERR(svn_dirent_get_absolute(&truepath, truepath, subpool));

          SVN_ERR(svn_client_propget5(
            &props,
            opt_state->show_inherited_props ? &inherited_props : NULL,
            pname_utf8, truepath,
            &peg_revision,
            &(opt_state->start_revision),
            NULL, opt_state->depth,
            opt_state->changelists, ctx, subpool,
            subpool));

          /* Any time there is more than one thing to print, or where
             the path associated with a printed thing is not obvious,
             we'll print filenames.  That is, unless we've been told
             not to do so with the --strict option. */
          print_filenames = ((opt_state->depth > svn_depth_empty
                              || targets->nelts > 1
                              || apr_hash_count(props) > 1
                              || opt_state->verbose)
                             && (! opt_state->strict));
          omit_newline = opt_state->strict;
          like_proplist = opt_state->verbose && !opt_state->strict;

          if (opt_state->xml)
            SVN_ERR(print_properties_xml(
              pname_utf8, props,
              opt_state->show_inherited_props ? inherited_props : NULL,
              subpool));
          else
            SVN_ERR(print_properties(
              out, pname_utf8,
              props,
              opt_state->show_inherited_props ? inherited_props : NULL,
              print_filenames,
              omit_newline, like_proplist, subpool));
        }

      if (opt_state->xml)
        SVN_ERR(svn_cl__xml_print_footer("properties", subpool));

      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
