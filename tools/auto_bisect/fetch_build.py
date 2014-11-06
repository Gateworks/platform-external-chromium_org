# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module contains functions for fetching and extracting archived builds.

The builds may be stored in different places by different types of builders;
for example, builders on tryserver.chromium.perf stores builds in one place,
while builders on chromium.linux store builds in another.

This module can be either imported or run as a stand-alone script to download
and extract a build.

Usage: fetch_build.py <type> <revision> <output_dir> [options]
"""

import argparse
import errno
import os
import shutil
import sys
import zipfile

# Telemetry (src/tools/telemetry) is expected to be in the PYTHONPATH.
from telemetry.util import cloud_storage

import bisect_utils

# Possible builder types.
PERF_BUILDER = 'perf'
FULL_BUILDER = 'full'


def FetchBuild(builder_type, revision, output_dir, target_arch='ia32',
               target_platform='chromium', deps_patch_sha=None):
  """Downloads and extracts a build for a particular revision.

  If the build is successfully downloaded and extracted to |output_dir|, the
  downloaded archive file is also deleted.

  Args:
    revision: Revision string, e.g. a git commit hash or SVN revision.
    builder_type: Type of build archive.
    target_arch: Architecture, e.g. "ia32".
    target_platform: Platform name, e.g. "chromium" or "android".
    deps_patch_sha: SHA1 hash of a DEPS file, if we want to fetch a build for
        a Chromium revision with custom dependencies.

  Raises:
    IOError: Unzipping failed.
    OSError: Directory creation or deletion failed.
  """
  build_archive = BuildArchive.Create(
      builder_type, target_arch=target_arch, target_platform=target_platform)
  bucket = build_archive.BucketName()
  remote_path = build_archive.FilePath(revision, deps_patch_sha=deps_patch_sha)

  filename = FetchFromCloudStorage(bucket, remote_path, output_dir)
  if not filename:
    raise RuntimeError('Failed to fetch gs://%s/%s.' % (bucket, remote_path))

  Unzip(filename, output_dir)

  if os.path.exists(filename):
    os.remove(filename)


class BuildArchive(object):
  """Represents a place where builds of some type are stored.

  There are two pieces of information required to locate a file in Google
  Cloud Storage, bucket name and file path. Subclasses of this class contain
  specific logic about which bucket names and paths should be used to fetch
  a build.
  """

  @staticmethod
  def Create(builder_type, target_arch='ia32', target_platform='chromium'):
    if builder_type == PERF_BUILDER:
      return PerfBuildArchive(target_arch, target_platform)
    if builder_type == FULL_BUILDER:
      return FullBuildArchive(target_arch, target_platform)
    raise NotImplementedError('Builder type "%s" not supported.' % builder_type)

  def __init__(self, target_arch='ia32', target_platform='chromium'):
    if bisect_utils.IsLinuxHost() and target_platform == 'android':
      self._platform = 'android'
    elif bisect_utils.IsLinuxHost():
      self._platform = 'linux'
    elif bisect_utils.IsMacHost():
      self._platform = 'mac'
    elif bisect_utils.Is64BitWindows() and target_arch == 'x64':
      self._platform = 'win64'
    elif bisect_utils.IsWindowsHost():
      self._platform = 'win'
    else:
      raise NotImplementedError('Unknown platform "%s".' % sys.platform)

  def BucketName(self):
    raise NotImplementedError()

  def FilePath(self, revision, deps_patch_sha=None):
    """Returns the remote file path to download a build from.

    Args:
      revision: A Chromium revision; this could be a git commit hash or
          commit position or SVN revision number.
      deps_patch_sha: The SHA1 hash of a patch to the DEPS file, which
          uniquely identifies a change to use a particular revision of
          a dependency.

    Returns:
      A file path, which not does not include a bucket name.
    """
    raise NotImplementedError()

  def _ZipFileName(self, revision, deps_patch_sha=None):
    """Gets the file name of a zip archive for a particular revision.

    This returns a file name of the form full-build-<platform>_<revision>.zip,
    which is a format used by multiple types of builders that store archives.

    Args:
      revision: A git commit hash or other revision string.
      deps_patch_sha: SHA1 hash of a DEPS file patch.

    Returns:
      The archive file name.
    """
    base_name = 'full-build-%s' % self._PlatformName()
    if deps_patch_sha:
      revision = '%s_%s' % (revision, deps_patch_sha)
    return '%s_%s.zip' % (base_name, revision)

  def _PlatformName(self):
    """Return a string to be used in paths for the platform."""
    if self._platform in ('win', 'win64'):
      # Build archive for win64 is still stored with "win32" in the name.
      return 'win32'
    if self._platform in ('linux', 'android'):
      # Android builds are also stored with "linux" in the name.
      return 'linux'
    if self._platform == 'mac':
      return 'mac'
    raise NotImplementedError('Unknown platform "%s".' % sys.platform)


class PerfBuildArchive(BuildArchive):

  def BucketName(self):
    return 'chrome-perf'

  def FilePath(self, revision, deps_patch_sha=None):
    return '%s/%s' % (self._ArchiveDirectory(),
                      self._ZipFileName(revision, deps_patch_sha))

  def _ArchiveDirectory(self):
    """Returns the directory name to download builds from."""
    platform_to_directory = {
        'android': 'android_perf_rel',
        'linux': 'Linux Builder',
        'mac': 'Mac Builder',
        'win64': 'Win x64 Builder',
        'win': 'Win Builder',
    }
    assert self._platform in platform_to_directory
    return platform_to_directory.get(self._platform)


class FullBuildArchive(BuildArchive):

  def BucketName(self):
    platform_to_bucket = {
        'android': 'chromium-android',
        'linux': 'chromium-linux-archive',
        'mac': 'chromium-mac-archive',
        'win64': 'chromium-win-archive',
        'win': 'chromium-win-archive',
    }
    assert self._platform in platform_to_bucket
    return platform_to_bucket.get(self._platform)

  def FilePath(self, revision, deps_patch_sha=None):
    return '%s/%s' % (self._ArchiveDirectory(),
                      self._ZipFileName(revision, deps_patch_sha))

  def _ArchiveDirectory(self):
    """Returns the remote directory to download builds from."""
    platform_to_directory = {
        'android': 'android_main_rel',
        'linux': 'chromium.linux/Linux Builder',
        'mac': 'chromium.mac/Mac Builder',
        'win64': 'chromium.win/Win x64 Builder',
        'win': 'chromium.win/Win Builder',
    }
    assert self._platform in platform_to_directory
    return platform_to_directory.get(self._platform)


def FetchFromCloudStorage(bucket_name, source_path, destination_dir):
  """Fetches file(s) from the Google Cloud Storage.

  As a side-effect, this prints messages to stdout about what's happening.

  Args:
    bucket_name: Google Storage bucket name.
    source_path: Source file path.
    destination_dir: Destination file path.

  Returns:
    Local file path of downloaded file if it was downloaded. If the file does
    not exist in the given bucket, or if there was an error while downloading,
    None is returned.
  """
  target_file = os.path.join(destination_dir, os.path.basename(source_path))
  gs_url = 'gs://%s/%s' % (bucket_name, source_path)
  try:
    if cloud_storage.Exists(bucket_name, source_path):
      print 'Fetching file from %s...' % gs_url
      cloud_storage.Get(bucket_name, source_path, target_file)
      if os.path.exists(target_file):
        return target_file
    else:
      print 'File %s not found in cloud storage.' % gs_url
  except Exception as e:
    print 'Exception while fetching from cloud storage: %s' % e
    if os.path.exists(target_file):
      os.remove(target_file)
  return None


def Unzip(filename, output_dir, verbose=True):
  """Extracts a zip archive's contents into the given output directory.

  This was based on ExtractZip from build/scripts/common/chromium_utils.py.

  Args:
    filename: Name of the zip file to extract.
    output_dir: Path to the destination directory.
    verbose: Whether to print out what is being extracted.

  Raises:
    IOError: The unzip command had a non-zero exit code.
    RuntimeError: Failed to create the output directory.
  """
  _MakeDirectory(output_dir)

  # On Linux and Mac, we use the unzip command because it handles links and
  # file permissions bits, so achieving this behavior is easier than with
  # ZipInfo options.
  #
  # The Mac Version of unzip unfortunately does not support Zip64, whereas
  # the python module does, so we have to fall back to the python zip module
  # on Mac if the file size is greater than 4GB.
  mac_zip_size_limit = 2 ** 32  # 4GB
  if (bisect_utils.IsLinuxHost() or
      (bisect_utils.IsMacHost()
       and os.path.getsize(filename) < mac_zip_size_limit)):
    unzip_command = ['unzip', '-o']
    _UnzipUsingCommand(unzip_command, filename, output_dir)
    return

  # On Windows, try to use 7z if it is installed, otherwise fall back to the
  # Python zipfile module. If 7z is not installed, then this may fail if the
  # zip file is larger than 512MB.
  sevenzip_path = r'C:\Program Files\7-Zip\7z.exe'
  if bisect_utils.IsWindowsHost() and os.path.exists(sevenzip_path):
    unzip_command = [sevenzip_path, 'x', '-y']
    _UnzipUsingCommand(unzip_command, filename, output_dir)
    return

  _UnzipUsingZipFile(filename, output_dir, verbose)


def _UnzipUsingCommand(unzip_command, filename, output_dir):
  """Extracts a zip file using an external command.

  Args:
    unzip_command: An unzipping command, as a string list, without the filename.
    filename: Path to the zip file.
    output_dir: The directory which the contents should be extracted to.

  Raises:
    IOError: The command had a non-zero exit code.
  """
  absolute_filepath = os.path.abspath(filename)
  command = unzip_command + [absolute_filepath]
  return_code = _RunCommandInDirectory(output_dir, command)
  if return_code:
    _RemoveDirectoryTree(output_dir)
    raise IOError('Unzip failed: %s => %s' % (str(command), return_code))


def _RunCommandInDirectory(directory, command):
  """Changes to a directory, runs a command, then changes back."""
  saved_dir = os.getcwd()
  os.chdir(directory)
  return_code = bisect_utils.RunProcess(command)
  os.chdir(saved_dir)
  return return_code


def _UnzipUsingZipFile(filename, output_dir, verbose=True):
  """Extracts a zip file using the Python zipfile module."""
  assert bisect_utils.IsWindowsHost() or bisect_utils.IsMacHost()
  zf = zipfile.ZipFile(filename)
  for name in zf.namelist():
    if verbose:
      print 'Extracting %s' % name
    zf.extract(name, output_dir)
    if bisect_utils.IsMacHost():
      # Restore file permission bits.
      mode = zf.getinfo(name).external_attr >> 16
      os.chmod(os.path.join(output_dir, name), mode)


def _MakeDirectory(path):
  try:
    os.makedirs(path)
  except OSError as e:
    if e.errno != errno.EEXIST:
      raise


def _RemoveDirectoryTree(path):
  try:
    if os.path.exists(path):
      shutil.rmtree(path)
  except OSError, e:
    if e.errno != errno.ENOENT:
      raise


def Main(argv):
  """Downloads and extracts a build based on the command line arguments."""
  parser = argparse.ArgumentParser()
  parser.add_argument('builder_type')
  parser.add_argument('revision')
  parser.add_argument('output_dir')
  parser.add_argument('--target-arch', default='ia32')
  parser.add_argument('--target-platform', default='chromium')
  parser.add_argument('--deps-patch-sha')
  args = parser.parse_args(argv[1:])

  FetchBuild(
      args.builder_type, args.revision, args.output_dir,
      target_arch=args.target_arch, target_platform=args.target_platform,
      deps_patch_sha=args.deps_patch_sha)

  print 'Build has been downloaded to and extracted in %s.' % args.output_dir

  return 0


if __name__ == '__main__':
  sys.exit(Main(sys.argv))

