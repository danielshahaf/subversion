/*
 * conflict-callbacks.c: conflict resolution callbacks specific to the
 * commandline client.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "cl.h"

#include "svn_private_config.h"




svn_cl__conflict_baton_t *
svn_cl__conflict_baton_make(svn_cl__accept_t accept_which,
                            apr_hash_t *config,
                            const char *editor_cmd,
                            svn_cmdline_prompt_baton_t *pb,
                            apr_pool_t *pool)
{
  svn_cl__conflict_baton_t *b = apr_palloc(pool, sizeof(*b));
  b->accept_which = accept_which;
  b->config = config;
  b->editor_cmd = editor_cmd;
  b->external_failed = FALSE;
  b->pb = pb;
  return b;
}

svn_cl__accept_t
svn_cl__accept_from_word(const char *word)
{
  if (strcmp(word, SVN_CL__ACCEPT_POSTPONE) == 0)
    return svn_cl__accept_postpone;
  if (strcmp(word, SVN_CL__ACCEPT_BASE) == 0)
    return svn_cl__accept_base;
  if (strcmp(word, SVN_CL__ACCEPT_MINE) == 0)
    return svn_cl__accept_mine;
  if (strcmp(word, SVN_CL__ACCEPT_THEIRS) == 0)
    return svn_cl__accept_theirs;
  if (strcmp(word, SVN_CL__ACCEPT_EDIT) == 0)
    return svn_cl__accept_edit;
  if (strcmp(word, SVN_CL__ACCEPT_LAUNCH) == 0)
    return svn_cl__accept_launch;
  /* word is an invalid action. */
  return svn_cl__accept_invalid;
}


/* A conflict callback which does nothing; useful for debugging and/or
   printing a description of the conflict. */
svn_error_t *
svn_cl__ignore_conflicts(svn_wc_conflict_result_t **result,
                         const svn_wc_conflict_description_t *description,
                         void *baton,
                         apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, _("Discovered a conflict.\n\n")));
  SVN_ERR(svn_cmdline_printf(pool, "\n\n"));

  *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                          NULL, pool);
  return SVN_NO_ERROR;
}


/* Implement svn_wc_conflict_resolver_func_t; resolves based on
   --accept option if given, else by prompting. */
svn_error_t *
svn_cl__conflict_handler(svn_wc_conflict_result_t **result,
                         const svn_wc_conflict_description_t *desc,
                         void *baton,
                         apr_pool_t *pool)
{
  svn_cl__conflict_baton_t *b = baton;
  svn_error_t *err;
  apr_pool_t *subpool;

  /* Start out assuming we're going to postpone the conflict. */
  *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                          NULL, pool);

  switch (b->accept_which)
    {
    case svn_cl__accept_invalid:
      /* No --accept option, fall through to prompting. */
      break;
    case svn_cl__accept_postpone:
      (*result)->choice = svn_wc_conflict_choose_postpone;
      return SVN_NO_ERROR;
    case svn_cl__accept_base:
      (*result)->choice = svn_wc_conflict_choose_base;
      return SVN_NO_ERROR;
    case svn_cl__accept_mine:
      (*result)->choice = svn_wc_conflict_choose_mine;
      return SVN_NO_ERROR;
    case svn_cl__accept_theirs:
      (*result)->choice = svn_wc_conflict_choose_theirs;
      return SVN_NO_ERROR;
    case svn_cl__accept_edit:
      if (desc->merged_file)
        {
          if (b->external_failed)
            {
              (*result)->choice = svn_wc_conflict_choose_postpone;
              return SVN_NO_ERROR;
            }

          err = svn_cl__edit_file_externally(desc->merged_file,
                                             b->editor_cmd, b->config, pool);
          if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                          err->message ? err->message :
                                          _("No editor found,"
                                            " leaving all conflicts.")));
              svn_error_clear(err);
              b->external_failed = TRUE;
            }
          else if (err && (err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                          err->message ? err->message :
                                          _("Error running editor,"
                                            " leaving all conflicts.")));
              svn_error_clear(err);
              b->external_failed = TRUE;
            }
          else if (err)
            return err;
          (*result)->choice = svn_wc_conflict_choose_merged;
          return SVN_NO_ERROR;
        }
      /* else, fall through to prompting. */
      break;
    case svn_cl__accept_launch:
      if (desc->base_file && desc->their_file
          && desc->my_file && desc->merged_file)
        {
          if (b->external_failed)
            {
              (*result)->choice = svn_wc_conflict_choose_postpone;
              return SVN_NO_ERROR;
            }

          err = svn_cl__merge_file_externally(desc->base_file,
                                              desc->their_file,
                                              desc->my_file,
                                              desc->merged_file,
                                              b->config,
                                              pool);
          if (err && err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                          err->message ? err->message :
                                          _("No merge tool found.\n")));
              svn_error_clear(err);
              b->external_failed = TRUE;
            }
          else if (err && err->apr_err == SVN_ERR_EXTERNAL_PROGRAM)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                          err->message ? err->message :
                                          _("Error running merge tool.")));
              svn_error_clear(err);
              b->external_failed = TRUE;
            }
          else if (err)
            return err;
          (*result)->choice = svn_wc_conflict_choose_merged;
          return SVN_NO_ERROR;
        }
      /* else, fall through to prompting. */
      break;
    }

  /* We're in interactive mode and either the user gave no --accept
     option or the option did not apply; let's prompt. */
  subpool = svn_pool_create(pool);

  /* Handle the most common cases, which is either:

     Conflicting edits on a file's text, or
     Conflicting edits on a property.
  */
  if (((desc->node_kind == svn_node_file)
       && (desc->action == svn_wc_conflict_action_edit)
       && (desc->reason == svn_wc_conflict_reason_edited))
      || (desc->kind == svn_wc_conflict_kind_property))
  {
      const char *answer;
      char *prompt;
      svn_boolean_t performed_edit = FALSE;

      if (desc->kind == svn_wc_conflict_kind_text)
        SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                    _("Conflict discovered in '%s'.\n"),
                                    desc->path));
      else if (desc->kind == svn_wc_conflict_kind_property)
        {
          SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                      _("Property conflict for '%s' discovered"
                                        " on '%s'.\n"),
                                      desc->property_name, desc->path));
          if (! desc->merged_file)
            {
              svn_stringbuf_t *myval = NULL, *theirval = NULL;

              /* libsvn_wc failed to merge the propvals; at least let
                 the user see them, so they're not selecting (m)ine or
                 (t)heirs blindly! */

              if (desc->my_file)
                SVN_ERR(svn_stringbuf_from_file(&myval, desc->my_file,
                                                subpool));
              if (desc->their_file)
                SVN_ERR(svn_stringbuf_from_file(&theirval, desc->their_file,
                                                subpool));

              SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                        _("Their value is '%s', your value is '%s'.\n"),
                          theirval ? theirval->data : "NULL",
                          myval ? myval->data : "NULL"));
            }
        }
      else
        /* We don't recognize any other sort of conflict yet */
        return SVN_NO_ERROR;

      while (TRUE)
        {
          svn_pool_clear(subpool);

          prompt = apr_pstrdup(subpool, _("Select: (p)ostpone"));
          if (desc->merged_file)
            prompt = apr_pstrcat(subpool, prompt, _(", (d)iff, (e)dit"),
                                 NULL);
          else
            prompt = apr_pstrcat(subpool, prompt, _(", (m)ine, (t)heirs"),
                                 NULL);
          if (performed_edit)
            prompt = apr_pstrcat(subpool, prompt, _(", (r)esolved"), NULL);
          prompt = apr_pstrcat(subpool, prompt,
                               _(", (h)elp for more options : "), NULL);

          SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, b->pb, subpool));

          if ((strcmp(answer, "h") == 0) || (strcmp(answer, "?") == 0))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
              _("  (p)ostpone - mark the conflict to be resolved later\n"
                "  (d)iff     - show all changes made to merged file\n"
                "  (e)dit     - change merged file in an editor\n"
                "  (r)esolved - accept merged version of file\n"
                "  (m)ine     - accept my version of file\n"
                "  (t)heirs   - accept their version of file\n"
                "  (l)aunch   - use third-party tool to resolve conflict\n"
                "  (h)elp     - show this list\n\n")));
            }
          if (strcmp(answer, "p") == 0)
            {
              /* Do nothing, let file be marked conflicted. */
              (*result)->choice = svn_wc_conflict_choose_postpone;
              break;
            }
          if (strcmp(answer, "m") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_mine;
              break;
            }
          if (strcmp(answer, "t") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_theirs;
              break;
            }
          if (strcmp(answer, "d") == 0)
            {
              if (desc->merged_file && desc->base_file)
                {
                  svn_diff_t *diff;
                  svn_stream_t *output;
                  svn_diff_file_options_t *options =
                      svn_diff_file_options_create(subpool);
                  options->ignore_eol_style = TRUE;
                  SVN_ERR(svn_stream_for_stdout(&output, subpool));
                  SVN_ERR(svn_diff_file_diff_2(&diff, desc->base_file,
                                               desc->merged_file,
                                               options, subpool));
                  SVN_ERR(svn_diff_file_output_unified2(output, diff,
                                                        desc->base_file,
                                                        desc->merged_file,
                                                        NULL, NULL,
                                                        APR_LOCALE_CHARSET,
                                                        subpool));
                  performed_edit = TRUE;
                }
              else
                SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                            _("Invalid option; there's no "
                                              "merged version to diff.\n\n")));
            }
          if (strcmp(answer, "e") == 0)
            {
              if (desc->merged_file)
                {
                  err = svn_cl__edit_file_externally(desc->merged_file,
                                                     b->editor_cmd,
                                                     b->config, subpool);
                  if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR))
                    {
                      SVN_ERR(svn_cmdline_fprintf(stderr, subpool, "%s\n",
                                                  err->message ? err->message :
                                                  _("No editor found.")));
                      svn_error_clear(err);
                    }
                  else if (err && (err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
                    {
                      SVN_ERR(svn_cmdline_fprintf(stderr, subpool, "%s\n",
                                                  err->message ? err->message :
                                                  _("Error running editor.")));
                      svn_error_clear(err);
                    }
                  else if (err)
                    return err;
                  else
                    performed_edit = TRUE;
                }
              else
                SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                            _("Invalid option; there's no "
                                              "merged version to edit.\n\n")));
            }
          if (strcmp(answer, "l") == 0)
            {
              if (desc->base_file && desc->their_file &&
                  desc->my_file && desc->merged_file)
                {
                  err = svn_cl__merge_file_externally(desc->base_file,
                                                      desc->their_file,
                                                      desc->my_file,
                                                      desc->merged_file,
                                                      b->config,
                                                      pool);
                  if (err && err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL)
                    {
                      SVN_ERR(svn_cmdline_fprintf(stderr, subpool, "%s\n",
                                                  err->message ? err->message :
                                                  _("No merge tool found.\n")));
                      svn_error_clear(err);
                    }
                  else if (err && err->apr_err == SVN_ERR_EXTERNAL_PROGRAM)
                    {
                      SVN_ERR(svn_cmdline_fprintf(stderr, subpool, "%s\n",
                                                  err->message ? err->message :
                                             _("Error running merge tool.")));
                      svn_error_clear(err);
                    }
                  else if (err)
                    return err;
                  else
                    performed_edit = TRUE;
                }
              else
                SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                            _("Invalid option.\n\n")));
            }
          if (strcmp(answer, "r") == 0)
            {
              /* We only allow the user accept the merged version of
                 the file if they've edited it, or at least looked at
                 the diff. */
              if (performed_edit)
                {
                  (*result)->choice = svn_wc_conflict_choose_merged;
                  break;
                }
              else
                SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                            _("Invalid option.\n\n")));
            }
        }
    }
  /*
    Dealing with obstruction of additions can be tricky.  The
    obstructing item could be unversioned, versioned, or even
    schedule-add.  Here's a matrix of how the caller should behave,
    based on results we return.

                         Unversioned       Versioned       Schedule-Add

      choose_mine       skip addition,    skip addition     skip addition
                        add existing item

      choose_theirs     destroy file,    schedule-delete,   revert add,
                        add new item.    add new item.      rm file,
                                                            add new item

      postpone               [              bail out                 ]

   */
  else if ((desc->action == svn_wc_conflict_action_add)
           && (desc->reason == svn_wc_conflict_reason_obstructed))
    {
      const char *answer;
      const char *prompt;

      SVN_ERR(svn_cmdline_fprintf(
                   stderr, subpool,
                   _("Conflict discovered when trying to add '%s'.\n"
                     "An object of the same name already exists.\n"),
                   desc->path));
      prompt = _("Select: (p)ostpone, (m)ine, (t)heirs, (h)elp :");

      while (1)
        {
          svn_pool_clear(subpool);

          SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, b->pb, subpool));

          if ((strcmp(answer, "h") == 0) || (strcmp(answer, "?") == 0))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
              _("  (p)ostpone - resolve the conflict later\n"
                "  (m)ine     - accept pre-existing item\n"
                "  (t)heirs   - accept incoming item\n"
                "  (h)elp     - show this list\n\n")));
            }
          if (strcmp(answer, "p") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_postpone;
              break;
            }
          if (strcmp(answer, "m") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_mine;
              break;
            }
          if (strcmp(answer, "t") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_theirs;
              break;
            }
        }
    }

  else /* other types of conflicts -- do nothing about them. */
    {
      (*result)->choice = svn_wc_conflict_choose_postpone;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}
