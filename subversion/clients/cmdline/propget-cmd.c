/*
 * propget-cmd.c -- Display status information in current directory
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propget (apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  int i;

  /* PNAME is first argument (and PNAME_UTF8 will be a UTF-8 version
     thereof) */
  SVN_ERR (svn_opt_parse_num_args (&args, os, 1, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, NULL, pool));
  
  /* suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target (targets, pool);

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL, *target;
      svn_client_auth_baton_t *auth_baton;
      svn_string_t *propval;

      /* All property commands insist on a specific revision when
         operating on a revprop. */
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        return svn_cl__revprop_no_rev_error (pool);

      /* Else some revision was specified, so proceed. */

      auth_baton = svn_cl__make_auth_baton (opt_state, pool);

      /* Either we have a URL target, or an implicit wc-path ('.')
         which needs to be converted to a URL. */
      if (targets->nelts <= 0)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL,
                                "No URL target available.");
      target = ((const char **) (targets->elts))[0];
      SVN_ERR (svn_cl__get_url_from_target (&URL, target, pool));  
      if (URL == NULL)
        return svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, 0, NULL,
                                "Either a URL or versioned item is required.");
  
      /* Let libsvn_client do the real work. */
      SVN_ERR (svn_client_revprop_get (pname_utf8, &propval,
                                       URL, &(opt_state->start_revision),
                                       auth_baton, &rev, pool));

      if (propval != NULL)
        {
          svn_string_t *printable_val = propval;

          /* If this is a special Subversion property, it is stored as
             UTF8 and LF, so convert to the native locale and eol-style. */
          
          if (svn_cl__prop_needs_translation (pname_utf8))
            SVN_ERR (svn_cl__detranslate_string (&printable_val, propval,
                                                 pool));
          
          printf ("%s\n", printable_val->data);
        }
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      /* ### This check will go away when svn_client_propget takes
         a revision arg and can access the repository, see issue #943. */ 
      if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
        return svn_error_create
          (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
           "Revision argument to propget not yet supported (see issue #943)");

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          apr_hash_t *props;
          apr_hash_index_t *hi;
          svn_boolean_t print_filenames = FALSE;
          
          SVN_ERR (svn_client_propget (&props, pname_utf8, target,
                                       &(opt_state->start_revision),
                                       opt_state->recursive, pool));
          
          print_filenames = (opt_state->recursive || targets->nelts > 1
                             || apr_hash_count (props) > 1);
          
          for (hi = apr_hash_first (pool, props); hi; hi = apr_hash_next (hi))
            {
              const void *key;
              void *val;
              const char *filename; 
              svn_string_t *propval;
              const char *filename_native;
              
              apr_hash_this (hi, &key, NULL, &val);
              filename = key;
              propval = val;
              
              /* If this is a special Subversion property, it is stored as
                 UTF8, so convert to the native format. */
              if (svn_cl__prop_needs_translation (pname_utf8))
                SVN_ERR (svn_cl__detranslate_string (&propval, propval, pool));

              /* ### this won't handle binary property values */
              if (print_filenames) 
                {
                  SVN_ERR (svn_utf_cstring_from_utf8 (&filename_native,
                                                      filename, pool));
                  printf ("%s - %s\n", filename_native, propval->data);
                } 
              else 
                {
                  printf ("%s\n", propval->data);
                }
            }
        }
    }

  return SVN_NO_ERROR;
}
