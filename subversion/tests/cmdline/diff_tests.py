import sys, re, os, time, shutil, logging

logger = logging.getLogger()
def make_diff_header(path, old_tag, new_tag, src_label=None, dst_label=None):
  versions described in parentheses by OLD_TAG and NEW_TAG. SRC_LABEL and
  DST_LABEL are paths or urls that are added to the diff labels if we're
  diffing against the repository or diffing two arbitrary paths.
  Return the header as an array of newline-terminated strings."""
  if src_label:
    src_label = src_label.replace('\\', '/')
    src_label = '\t(.../' + src_label + ')'
  else:
    src_label = ''
  if dst_label:
    dst_label = dst_label.replace('\\', '/')
    dst_label = '\t(.../' + dst_label + ')'
  else:
    dst_label = ''
    "--- " + path_as_shown + src_label + "\t(" + old_tag + ")\n",
    "+++ " + path_as_shown + dst_label + "\t(" + new_tag + ")\n",
  output = [
    "Index: " + path_as_shown + "\n",
    "===================================================================\n"
  ]
    output.extend([
    ])
    output.extend([
    ])
    output.extend([
    ])
    output.extend([
    ])
    output.extend([
    ])

      logger.warn('Sought: %s' % excluded)
      logger.warn('Found:  %s' % line)
    None, 'diff', '-r', '1', sbox.ospath('A/D'))
    None, 'diff', '-r', '1', '-N', sbox.ospath('A/D'))
    None, 'diff', '-r', '1', sbox.ospath('A/D/G'))
  svntest.main.file_append(sbox.ospath('A/D/foo'), "a new file")
    1, 'diff', sbox.ospath('A/D/foo'))
  theta_path = sbox.ospath('A/theta')
  mu_path = sbox.ospath('A/mu')
  iota_path = sbox.ospath('iota')
  newfile_path = sbox.ospath('A/D/newfile')
  mu_path = sbox.ospath('A/mu')
  A_path = sbox.ospath('A')
  mu_path = sbox.ospath('A/mu')
@Issue(2873)
  A_alpha = sbox.ospath('A/B/E/alpha')
  A2_alpha = sbox.ospath('A2/B/E/alpha')
  A_alpha = sbox.ospath('A/B/E/alpha')
  A2_alpha = sbox.ospath('A2/B/E/alpha')
  A_path = sbox.ospath('A')
  iota_path = sbox.ospath('iota')
  iota_copy_path = sbox.ospath('A/iota')
  iota_path = sbox.ospath('iota')
  prefix_path = sbox.ospath('prefix_mydir')
  other_prefix_path = sbox.ospath('prefix_other')
    logger.warn("src is '%s' instead of '%s' and dest is '%s' instead of '%s'" %

  iota_path = sbox.ospath('iota')
  iota_path = sbox.ospath('iota')
  # Check a wc->wc diff
  # Check a repos->wc diff of the moved-here node before commit
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', '--show-copies-as-adds',
    os.path.join('A', 'D', 'I'))
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'I', 'pi'),
                       'A') :
    raise svntest.Failure

  # Check a repos->wc diff of the moved-away node before commit
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', os.path.join('A', 'D', 'G'))
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'pi'),
                       'D') :
    raise svntest.Failure

  # Diff summarize of a newly added file
  expected_diff = svntest.wc.State(wc_dir, {
    'iota': Item(status='A '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c1')

  # Reverse summarize diff of a newly added file
  expected_diff = svntest.wc.State(wc_dir, {
    'iota': Item(status='D '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c-1')

  # Diff summarize of a newly added directory
  expected_diff = svntest.wc.State(wc_dir, {
    'A/D':          Item(status='A '),
    'A/D/gamma':    Item(status='A '),
    'A/D/H':        Item(status='A '),
    'A/D/H/chi':    Item(status='A '),
    'A/D/H/psi':    Item(status='A '),
    'A/D/H/omega':  Item(status='A '),
    'A/D/G':        Item(status='A '),
    'A/D/G/pi':     Item(status='A '),
    'A/D/G/rho':    Item(status='A '),
    'A/D/G/tau':    Item(status='A '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('A/D'), '-c1')

  # Reverse summarize diff of a newly added directory
  expected_diff = svntest.wc.State(wc_dir, {
    'A/D':          Item(status='D '),
    'A/D/gamma':    Item(status='D '),
    'A/D/H':        Item(status='D '),
    'A/D/H/chi':    Item(status='D '),
    'A/D/H/psi':    Item(status='D '),
    'A/D/H/omega':  Item(status='D '),
    'A/D/G':        Item(status='D '),
    'A/D/G/pi':     Item(status='D '),
    'A/D/G/rho':    Item(status='D '),
    'A/D/G/tau':    Item(status='D '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('A/D'), '-c-1')

  svntest.main.file_write(sbox.ospath('A/mu'),
  C_path = sbox.ospath('A/C')
  D_path = sbox.ospath('A/D')
  svntest.main.file_append(sbox.ospath('A/mu'), "New mu content")
                       sbox.ospath('iota'))
  tau_path = sbox.ospath('A/D/G/tau')
  newfile_path = sbox.ospath('newfile')
  svntest.main.run_svn(None, "mkdir", sbox.ospath('newdir'))
  # 3) Test working copy summarize
  paths = ['A/mu', 'iota', 'A/D/G/tau', 'newfile', 'A/B/lambda',
           'newdir',]
  items = ['modified', 'none', 'modified', 'added', 'deleted', 'added',]
  kinds = ['file','file','file','file','file', 'dir',]
  props = ['none', 'modified', 'modified', 'none', 'none', 'none',]

  svntest.actions.run_and_verify_diff_summarize_xml(
    [], wc_dir, paths, items, props, kinds, wc_dir)

  paths_iota = ['iota',]
  items_iota = ['none',]
  kinds_iota = ['file',]
  props_iota = ['modified',]
    [], wc_dir, paths_iota, items_iota, props_iota, kinds_iota, '-c2',
    sbox.ospath('iota'))
  iota_path = sbox.ospath('iota')
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')
  new_path = sbox.ospath('new')
  lambda_path = sbox.ospath('A/B/lambda')
  lambda_copied_path = sbox.ospath('A/B/lambda_copied')
  alpha_path = sbox.ospath('A/B/E/alpha')
  alpha_copied_path = sbox.ospath('A/B/E/alpha_copied')
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')
  new_path = sbox.ospath('new')
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')
  new_path = sbox.ospath('new')
  iota_path = sbox.ospath('iota')
  iota_path = sbox.ospath('iota')
  iota_path = sbox.ospath('iota')
  new_path = sbox.ospath('new')
  iota_path = sbox.ospath('iota')
  new_path = sbox.ospath('new')
@XFail()
@Issue(4010)
def diff_correct_wc_base_revnum(sbox):
  "diff WC-WC shows the correct base rev num"

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  svntest.main.file_write(iota_path, "")

  # Commit a local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Child's base is now 2; parent's is still 1.
  # Make a local mod.
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'Id', iota_path)

  expected_output = make_git_diff_header(iota_path, "iota",
                                         "revision 2", "working copy") + \
                    make_diff_prop_header("iota") + \
                    make_diff_prop_added("svn:keywords", "Id")

  # Diff the parent.
  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     '--git', wc_dir)

  # The same again, but specifying the target explicity. This should
  # give the same output.
  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     '--git', iota_path)

  A_path = sbox.ospath('A')
  B_abs_path = os.path.abspath(sbox.ospath('A/B'))
def diff_two_working_copies(sbox):
  "diff between two working copies"
  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a pristine working copy that will remain mostly unchanged
  wc_dir_old = sbox.add_wc_path('old')
  svntest.main.run_svn(None, 'co', sbox.repo_url, wc_dir_old)
  # Add a property to A/B/F in the pristine working copy
  svntest.main.run_svn(None, 'propset', 'newprop', 'propval-old\n',
                       os.path.join(wc_dir_old, 'A', 'B', 'F'))

  # Make changes to the first working copy:

  # removed nodes
  sbox.simple_rm('A/mu')
  sbox.simple_rm('A/D/H')

  # new nodes
  sbox.simple_mkdir('newdir')
  svntest.main.file_append(sbox.ospath('newdir/newfile'), 'new text\n')
  sbox.simple_add('newdir/newfile')
  sbox.simple_mkdir('newdir/newdir2') # should not show up in the diff

  # modified nodes
  sbox.simple_propset('newprop', 'propval', 'A/D')
  sbox.simple_propset('newprop', 'propval', 'A/D/gamma')
  svntest.main.file_append(sbox.ospath('A/B/lambda'), 'new text\n')

  # replaced nodes (files vs. directories) with property mods
  sbox.simple_rm('A/B/F')
  svntest.main.file_append(sbox.ospath('A/B/F'), 'new text\n')
  sbox.simple_add('A/B/F')
  sbox.simple_propset('newprop', 'propval-new\n', 'A/B/F')
  sbox.simple_rm('A/D/G/pi')
  sbox.simple_mkdir('A/D/G/pi')
  sbox.simple_propset('newprop', 'propval', 'A/D/G/pi')

  src_label = os.path.basename(wc_dir_old)
  dst_label = os.path.basename(wc_dir)
  expected_output = make_diff_header('newdir/newfile', 'working copy',
                                     'working copy',
                                     src_label, dst_label) + [
                      "@@ -0,0 +1 @@\n",
                      "+new text\n",
                    ] + make_diff_header('A/mu', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'mu'.\n",
                    ] + make_diff_header('A/B/F', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -0,0 +1 @@\n",
                      "+new text\n",
                    ] + make_diff_prop_header('A/B/F') + \
                        make_diff_prop_modified("newprop", "propval-old\n",
                                                "propval-new\n") + \
                    make_diff_header('A/B/lambda', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -1 +1,2 @@\n",
                      " This is the file 'lambda'.\n",
                      "+new text\n",
                    ] + make_diff_header('A/D', 'working copy', 'working copy',
                                         src_label, dst_label) + \
                        make_diff_prop_header('A/D') + \
                        make_diff_prop_added("newprop", "propval") + \
                    make_diff_header('A/D/gamma', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + \
                        make_diff_prop_header('A/D/gamma') + \
                        make_diff_prop_added("newprop", "propval") + \
                    make_diff_header('A/D/G/pi', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'pi'.\n",
                    ] + make_diff_prop_header('A/D/G/pi') + \
                        make_diff_prop_added("newprop", "propval") + \
                    make_diff_header('A/D/H/chi', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'chi'.\n",
                    ] + make_diff_header('A/D/H/omega', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'omega'.\n",
                    ] + make_diff_header('A/D/H/psi', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'psi'.\n",
                    ]
                    
  # Files in diff may be in any order.
  expected_output = svntest.verify.UnorderedOutput(expected_output)
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'diff', '--old', wc_dir_old,
                                     '--new', wc_dir)

def diff_deleted_url(sbox):
  "diff -cN of URL deleted in rN"
  sbox.build()
  wc_dir = sbox.wc_dir

  # remove A/D/H in r2
  sbox.simple_rm("A/D/H")
  sbox.simple_commit()

  # A diff of r2 with target A/D/H should show the removed children
  expected_output = make_diff_header("chi", "revision 1", "revision 2") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'chi'.\n",
                    ] + make_diff_header("omega", "revision 1",
                                         "revision 2") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'omega'.\n",
                    ] + make_diff_header("psi", "revision 1",
                                         "revision 2") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'psi'.\n",
                    ]

  # Files in diff may be in any order.
  expected_output = svntest.verify.UnorderedOutput(expected_output)
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'diff', '-c2',
                                     sbox.repo_url + '/A/D/H')

def diff_arbitrary_files_and_dirs(sbox):
  "diff arbitrary files and dirs"
  sbox.build()
  wc_dir = sbox.wc_dir

  # diff iota with A/mu
  expected_output = make_diff_header("mu", "working copy", "working copy",
                                     "iota", "A/mu") + [
                      "@@ -1 +1 @@\n",
                      "-This is the file 'iota'.\n",
                      "+This is the file 'mu'.\n"
                    ]
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'diff', '--old', sbox.ospath('iota'),
                                     '--new', sbox.ospath('A/mu'))

  # diff A/B/E with A/D
  expected_output = make_diff_header("G/pi", "working copy", "working copy",
                                     "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'pi'.\n"
                    ] + make_diff_header("G/rho", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'rho'.\n"
                    ] + make_diff_header("G/tau", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'tau'.\n"
                    ] + make_diff_header("H/chi", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'chi'.\n"
                    ] + make_diff_header("H/omega", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'omega'.\n"
                    ] + make_diff_header("H/psi", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'psi'.\n"
                    ] + make_diff_header("alpha", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'alpha'.\n"
                    ] + make_diff_header("beta", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'beta'.\n"
                    ] + make_diff_header("gamma", "working copy",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'gamma'.\n"
                    ]

  # Files in diff may be in any order.
  expected_output = svntest.verify.UnorderedOutput(expected_output)
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'diff', '--old', sbox.ospath('A/B/E'),
                                     '--new', sbox.ospath('A/D'))

def diff_properties_only(sbox):
  "diff --properties-only"

  sbox.build()
  wc_dir = sbox.wc_dir

  expected_output = \
    make_diff_header("iota", "revision 1", "revision 2") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_added("svn:eol-style", "native")

  expected_reverse_output = \
    make_diff_header("iota", "revision 2", "revision 1") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_deleted("svn:eol-style", "native")

  expected_rev1_output = \
    make_diff_header("iota", "revision 1", "working copy") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_added("svn:eol-style", "native")

  # Make a property change and a content change to 'iota'
  # Only the property change should be displayed by diff --properties-only
  sbox.simple_propset('svn:eol-style', 'native', 'iota')
  svntest.main.file_append(sbox.ospath('iota'), 'new text')

  sbox.simple_commit() # r2

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'diff', '--properties-only', '-r', '1:2',
                                     sbox.repo_url + '/iota')

  svntest.actions.run_and_verify_svn(None, expected_reverse_output, [],
                                     'diff', '--properties-only', '-r', '2:1',
                                     sbox.repo_url + '/iota')

  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, expected_rev1_output, [],
                                     'diff', '--properties-only', '-r', '1',
                                     'iota')

  svntest.actions.run_and_verify_svn(None, expected_rev1_output, [],
                                     'diff', '--properties-only',
                                     '-r', 'PREV', 'iota')
              diff_correct_wc_base_revnum,
              diff_two_working_copies,
              diff_deleted_url,
              diff_arbitrary_files_and_dirs,
              diff_properties_only,