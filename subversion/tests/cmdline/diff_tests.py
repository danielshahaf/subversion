import sys, re, os, time, shutil
def make_diff_header(path, old_tag, new_tag):
  versions described in parentheses by OLD_TAG and NEW_TAG. Return the header
  as an array of newline-terminated strings."""
    "--- " + path_as_shown + "\t(" + old_tag + ")\n",
    "+++ " + path_as_shown + "\t(" + new_tag + ")\n",
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
    ]
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
    ]
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
    ]
    return [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
    ]
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
    ]
  
      print('Sought: %s' % excluded)
      print('Found:  %s' % line)
    None, 'diff', '-r', '1', os.path.join(wc_dir, 'A', 'D'))
    None, 'diff', '-r', '1', '-N', os.path.join(wc_dir, 'A', 'D'))
    None, 'diff', '-r', '1', os.path.join(wc_dir, 'A', 'D', 'G'))
  svntest.main.file_append(os.path.join(wc_dir, 'A', 'D', 'foo'), "a new file")
    1, 'diff', os.path.join(wc_dir, 'A', 'D', 'foo'))
  theta_path = os.path.join(wc_dir, 'A', 'theta')
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')
  iota_path = os.path.join(sbox.wc_dir, 'iota')
  newfile_path = os.path.join(sbox.wc_dir, 'A', 'D', 'newfile')
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')
  A_path = os.path.join(sbox.wc_dir, 'A')
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')
  A_alpha = os.path.join(sbox.wc_dir, 'A', 'B', 'E', 'alpha')
  A2_alpha = os.path.join(sbox.wc_dir, 'A2', 'B', 'E', 'alpha')
  A_alpha = os.path.join(sbox.wc_dir, 'A', 'B', 'E', 'alpha')
  A2_alpha = os.path.join(sbox.wc_dir, 'A2', 'B', 'E', 'alpha')
  A_path = os.path.join(sbox.wc_dir, 'A')
  iota_path = os.path.join(sbox.wc_dir, 'iota')
  iota_copy_path = os.path.join(sbox.wc_dir, 'A', 'iota')
  iota_path = os.path.join(sbox.wc_dir, 'iota')
  prefix_path = os.path.join(sbox.wc_dir, 'prefix_mydir')
  other_prefix_path = os.path.join(sbox.wc_dir, 'prefix_other')
    print("src is '%s' instead of '%s' and dest is '%s' instead of '%s'" %
@XFail()
  iota_path = os.path.join(sbox.wc_dir, 'iota')
  iota_path = os.path.join(wc_dir, 'iota')
  # Check a repos->wc diff
  svntest.main.file_write(os.path.join(sbox.wc_dir, 'A', 'mu'),
  C_path = os.path.join(wc_dir, "A", "C")
  D_path = os.path.join(wc_dir, "A", "D")
  svntest.main.file_append(os.path.join(wc_dir, "A", "mu"), "New mu content")
                       os.path.join(wc_dir, 'iota'))
  tau_path = os.path.join(wc_dir, "A", "D", "G", "tau")
  newfile_path = os.path.join(wc_dir, 'newfile')
  svntest.main.run_svn(None, "mkdir", os.path.join(wc_dir, 'newdir'))
  # 3) Test working copy summarize
  svntest.actions.run_and_verify_diff_summarize_xml(
    ".*Summarizing diff can only compare repository to repository",
    None, wc_dir, None, None, wc_dir)

  paths = ['iota',]
  items = ['none',]
  kinds = ['file',]
  props = ['modified',]
    [], wc_dir, paths, items, props, kinds, '-c2',
    os.path.join(wc_dir, 'iota'))
  paths = ['A/mu', 'iota', 'A/D/G/tau', 'newfile', 'A/B/lambda',
           'newdir',]
  items = ['modified', 'none', 'modified', 'added', 'deleted', 'added',]
  kinds = ['file','file','file','file','file', 'dir',]
  props = ['none', 'modified', 'modified', 'none', 'none', 'none',]

  paths = ['A/mu', 'iota', 'A/D/G/tau', 'newfile', 'A/B/lambda',
           'newdir',]
  items = ['modified', 'none', 'modified', 'added', 'deleted', 'added',]
  kinds = ['file','file','file','file','file', 'dir',]
  props = ['none', 'modified', 'modified', 'none', 'none', 'none',]

  iota_path = os.path.join(sbox.wc_dir, 'iota')
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  new_path = os.path.join(wc_dir, 'new')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  lambda_copied_path = os.path.join(wc_dir, 'A', 'B', 'lambda_copied')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  alpha_copied_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha_copied')
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  new_path = os.path.join(wc_dir, 'new')
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  new_path = os.path.join(wc_dir, 'new')
  iota_path = os.path.join(wc_dir, 'iota')
  iota_path = os.path.join(wc_dir, 'iota')
  iota_path = os.path.join(wc_dir, 'iota')
  new_path = os.path.join(wc_dir, 'new')
  iota_path = os.path.join(wc_dir, 'iota')
  new_path = os.path.join(wc_dir, 'new')
  A_path = os.path.join(wc_dir, 'A')
  B_abs_path = os.path.abspath(os.path.join(wc_dir, 'A', 'B'))