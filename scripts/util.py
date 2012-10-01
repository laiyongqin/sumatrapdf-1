import os.path, re, shutil, struct, subprocess, sys, bz2, tempfile, hashlib, string
import zipfile2 as zipfile

def import_boto():
  global Key, S3Connection, bucket_lister, awscreds
  try:
    from boto.s3.key import Key
    from boto.s3.connection import S3Connection
    from boto.s3.bucketlistresultset import bucket_lister
  except:
    print("You need boto library (http://code.google.com/p/boto/)")
    print("svn checkout http://boto.googlecode.com/svn/trunk/ boto")
    print("cd boto; python setup.py install")
    raise

  try:
    import awscreds
  except:
    print "awscreds.py file needed with access and secret globals for aws access"
    sys.exit(1)

def log(s):
  print(s)
  sys.stdout.flush()

def group(list, size):
  i = 0
  while list[i:]:
    yield list[i:i + size]
    i += size

def uniquify(array):
  return list(set(array))

def strip_empty_lines(s):
  lines = [l.strip() for l in s.split("\n") if len(l.strip()) > 0]
  return string.join(lines, "\n")

def test_for_flag(args, arg, has_data=False):
  if arg not in args:
    if not has_data:
      return False
    for argx in args:
      if argx.startswith(arg + "="):
        args.remove(argx)
        return argx[len(arg) + 1:]
    return None

  if not has_data:
    args.remove(arg)
    return True

  ix = args.index(arg)
  if ix == len(args) - 1:
    return None
  data = args[ix + 1]
  args.pop(ix + 1)
  args.pop(ix)
  return data

S3_BUCKET = "kjkpub"
g_s3conn = None

def s3connection():
  global g_s3conn
  if g_s3conn is None:
    import_boto()
    g_s3conn = S3Connection(awscreds.access, awscreds.secret, True)
  return g_s3conn

def s3PubBucket():
  return s3connection().get_bucket(S3_BUCKET)

def ul_cb(sofar, total):
  print("So far: %d, total: %d" % (sofar , total))

def s3UploadFilePublic(local_path, remote_path):
  log("s3 upload '%s' as '%s'" % (local_path, remote_path))
  k = s3PubBucket().new_key(remote_path)
  k.set_contents_from_filename(local_path, cb=ul_cb)
  k.make_public()

def s3UploadDataPublic(data, remote_path):
  log("s3 upload data as '%s'" % remote_path)
  k = s3PubBucket().new_key(remote_path)
  k.set_contents_from_string(data)
  k.make_public()

def s3UploadDataPublicWithContentType(data, remote_path):
  # writing to a file to force boto to set Content-Type based on file extension.
  # TODO: there must be a simpler way
  tmp_name = os.path.basename(remote_path)
  tmp_path = os.path.join(tempfile.gettempdir(), tmp_name)
  open(tmp_path, "w").write(data)
  s3UploadFilePublic(tmp_path, remote_path)
  os.remove(tmp_path)

def s3DownloadToFile(remote_path, local_path):
  log("s3 download '%s' as '%s'" % (remote_path, local_path))
  k = s3PubBucket().new_key(remote_path)
  k.get_contents_to_filename(local_path)

def s3List(s3dir):
  b = s3PubBucket()
  return bucket_lister(b, s3dir)

def s3Delete(remote_path):
  log("s3 delete '%s'" % remote_path)
  s3PubBucket().new_key(remote_path).delete()
  
def s3_exist(remote_path):
  return s3PubBucket().get_key(remote_path)

def ensure_s3_doesnt_exist(remote_path):
  if not s3_exist(remote_path):
    return
  print("'%s' already exists in s3" % remote_path)
  sys.exit(1)

def file_sha1(fp):
  data = open(fp, "rb").read()
  m = hashlib.sha1()
  m.update(data)
  return m.hexdigest()

def ensure_path_exists(path):
  if not os.path.exists(path):
    print("path '%s' doesn't exist" % path)
    sys.exit(1)

def verify_started_in_right_directory():
  if os.path.exists(os.path.join("scripts", "build-release.py")): return
  if os.path.exists(os.path.join(os.getcwd(), "scripts", "build-release.py")): return
  print("This script must be run from top of the source tree")
  sys.exit(1)

def run_cmd(*args):
  cmd = " ".join(args)
  print("run_cmd_throw: '%s'" % cmd)
  # this magic disables the modal dialog that windows shows if the process crashes
  # TODO: it doesn't seem to work, maybe because it was actually a crash in a process
  # sub-launched from the process I'm launching. I had to manually disable this in
  # registry, as per http://stackoverflow.com/questions/396369/how-do-i-disable-the-debug-close-application-dialog-on-windows-vista:
  # DWORD HKLM or HKCU\Software\Microsoft\Windows\Windows Error Reporting\DontShowUI = "1"
  # DWORD HKLM or HKCU\Software\Microsoft\Windows\Windows Error Reporting\Disabled = "1"
  # see: http://msdn.microsoft.com/en-us/library/bb513638.aspx
  if sys.platform.startswith("win"):
    import ctypes
    SEM_NOGPFAULTERRORBOX = 0x0002 # From MSDN
    ctypes.windll.kernel32.SetErrorMode(SEM_NOGPFAULTERRORBOX);
    subprocess_flags = 0x8000000 #win32con.CREATE_NO_WINDOW?
  else:
    subprocess_flags = 0
  cmdproc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess_flags)
  res = cmdproc.communicate()
  return (res[0], res[1], cmdproc.returncode)

# like run_cmd() but throws an exception on failure
def run_cmd_throw(*args):
  cmd = " ".join(args)
  print("run_cmd_throw: '%s'" % cmd)

  # see comment in run_cmd()
  if sys.platform.startswith("win"):
    import ctypes
    SEM_NOGPFAULTERRORBOX = 0x0002 # From MSDN
    ctypes.windll.kernel32.SetErrorMode(SEM_NOGPFAULTERRORBOX);
    subprocess_flags = 0x8000000 #win32con.CREATE_NO_WINDOW?
  else:
    subprocess_flags = 0
  cmdproc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess_flags)
  res = cmdproc.communicate()
  errcode = cmdproc.returncode
  if 0 != errcode:
    print("Failed with error code %d" % errcode)
    print("Stdout:")
    print(res[0])
    print("Stderr:")
    print(res[1])
    raise Exception("'%s' failed with error code %d" % (cmd, errcode))
  return (res[0], res[1])

# Parse output of svn info and return revision number indicated by
# "Last Changed Rev" field or, if that doesn't exist, by "Revision" field
def parse_svninfo_out(txt):
  ver = re.findall(r'(?m)^Last Changed Rev: (\d+)', txt)
  if ver: return ver[0]
  ver = re.findall(r'(?m)^Revision: (\d+)', txt)
  if ver: return ver[0]
  raise Exception("parse_svn_info_out() failed to parse '%s'" % txt)

# version line is in the format:
# #define CURR_VERSION 1.1
def extract_sumatra_version(file_path):
  content = open(file_path).read()
  ver = re.findall(r'CURR_VERSION (\d+(?:\.\d+)*)', content)[0]
  return ver

def zip_file(dst_zip_file, src, src_name=None, compress=True, append=False):
  mode = "w"
  if append: mode = "a"
  if compress:
    zf = zipfile.ZipFile(dst_zip_file, mode, zipfile.ZIP_DEFLATED)
  else:
    zf = zipfile.ZipFile(dst_zip_file, mode, zipfile.ZIP_STORED)
  if src_name is None:
    src_name = os.path.basename(src)
  zf.write(src, src_name)
  zf.close()

# build the .zip with with installer data, will be included as part of 
# Installer.exe resources
def build_installer_data(dir):
  zf = zipfile.ZipFile(os.path.join(dir, "InstallerData.zip"), "w", zipfile.ZIP_BZIP2)
  exe = os.path.join(dir, "SumatraPDF-no-MuPDF.exe")
  zf.write(exe, "SumatraPDF.exe")
  for f in ["libmupdf.dll", "npPdfViewer.dll", "PdfFilter.dll", "PdfPreview.dll", "uninstall.exe"]:
    zf.write(os.path.join(dir, f), f)
  font_path = os.path.join("mupdf", "fonts", "droid", "DroidSansFallback.ttf")
  zf.write(font_path, "DroidSansFallback.ttf")
  zf.close()
