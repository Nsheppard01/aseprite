/* Aseprite
 * Copyright (C) 2001-2013  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Some of the original code to handle PIDLs come from the
   MiniExplorer example of the Vaca library:
   http://vaca.sourceforge.net/
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/file_system.h"

#include "base/fs.h"
#include "base/path.h"
#include "base/string.h"
#include "she/surface.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

#include <allegro.h>

// define this macro to solve the problem of for_each_file Allegro
// routine which does not support to wrap 64-bits pointers in its
// user-data parameter
#define WORKAROUND_64BITS_SUPPORT

// in Windows we can use PIDLS
#if defined ALLEGRO_WINDOWS
  // uncomment this if you don't want to use PIDLs in windows
  #define USE_PIDLS
#endif

#if defined ALLEGRO_UNIX || defined ALLEGRO_MACOSX || defined ALLEGRO_DJGPP || defined ALLEGRO_MINGW32
  #include <sys/stat.h>
#endif
#if defined ALLEGRO_UNIX || defined ALLEGRO_MACOSX || defined ALLEGRO_MINGW32
  #include <sys/unistd.h>

#endif

#if defined USE_PIDLS
  #include <winalleg.h>

  #include <shlobj.h>
  #include <shlwapi.h>
#endif

//////////////////////////////////////////////////////////////////////

#ifdef USE_PIDLS
  // ..using Win32 Shell (PIDLs)

  #define IS_FOLDER(fi)                                 \
    (((fi)->attrib & SFGAO_FOLDER) == SFGAO_FOLDER)

  #define MYPC_CSLID  "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"

#else
  // ..using Allegro (for_each_file)

  #define IS_FOLDER(fi)                                 \
    (((fi)->attrib & FA_DIREC) == FA_DIREC)

  #if (DEVICE_SEPARATOR != 0) && (DEVICE_SEPARATOR != '\0')
    #define HAVE_DRIVES
  #else
    #define CASE_SENSITIVE
  #endif

  #ifndef FA_ALL
    #define FA_ALL     FA_RDONLY | FA_DIREC | FA_ARCH | FA_HIDDEN | FA_SYSTEM
  #endif
  #define FA_TO_SHOW   FA_RDONLY | FA_DIREC | FA_ARCH | FA_SYSTEM

#endif

//////////////////////////////////////////////////////////////////////

#ifndef MAX_PATH
  #define MAX_PATH 4096
#endif

#define NOTINITIALIZED  "{__not_initialized_path__}"

namespace app {

// a position in the file-system
class FileItem : public IFileItem {
public:
  std::string keyname;
  std::string filename;
  std::string displayname;
  FileItem* parent;
  FileItemList children;
  unsigned int version;
  bool removed;
#ifdef USE_PIDLS
  LPITEMIDLIST pidl;            // relative to parent
  LPITEMIDLIST fullpidl;        // relative to the Desktop folder
                                // (like a full path-name, because the
                                // desktop is the root on Windows)
  SFGAOF attrib;
#else
  int attrib;
#endif

  FileItem(FileItem* parent);
  ~FileItem();

  void insertChildSorted(FileItem* child);
  int compare(const FileItem& that) const;

  bool operator<(const FileItem& that) const { return compare(that) < 0; }
  bool operator>(const FileItem& that) const { return compare(that) > 0; }
  bool operator==(const FileItem& that) const { return compare(that) == 0; }
  bool operator!=(const FileItem& that) const { return compare(that) != 0; }

  // IFileItem interface

  bool isFolder() const;
  bool isBrowsable() const;

  std::string getKeyName() const;
  std::string getFileName() const;
  std::string getDisplayName() const;

  IFileItem* getParent() const;
  const FileItemList& getChildren();
  void createDirectory(const std::string& dirname);

  bool hasExtension(const std::string& csv_extensions);

  she::Surface* getThumbnail();
  void setThumbnail(she::Surface* thumbnail);

};

typedef std::map<std::string, FileItem*> FileItemMap;
typedef std::map<std::string, she::Surface*> ThumbnailMap;

// the root of the file-system
static FileItem* rootitem = NULL;
static FileItemMap* fileitems_map;
static ThumbnailMap* thumbnail_map;
static unsigned int current_file_system_version = 0;

#ifdef USE_PIDLS
  static IMalloc* shl_imalloc = NULL;
  static IShellFolder* shl_idesktop = NULL;
#else
  #ifdef WORKAROUND_64BITS_SUPPORT
  static FileItem* for_each_child_callback_param;
  #endif
#endif

/* a more easy PIDLs interface (without using the SH* & IL* routines of W2K) */
#ifdef USE_PIDLS
  static void update_by_pidl(FileItem* fileitem);
  static LPITEMIDLIST concat_pidl(LPITEMIDLIST pidlHead, LPITEMIDLIST pidlTail);
  static UINT get_pidl_size(LPITEMIDLIST pidl);
  static LPITEMIDLIST get_next_pidl(LPITEMIDLIST pidl);
  static LPITEMIDLIST get_last_pidl(LPITEMIDLIST pidl);
  static LPITEMIDLIST clone_pidl(LPITEMIDLIST pidl);
  static LPITEMIDLIST remove_last_pidl(LPITEMIDLIST pidl);
  static void free_pidl(LPITEMIDLIST pidl);
  static std::string get_key_for_pidl(LPITEMIDLIST pidl);

  static FileItem* get_fileitem_by_fullpidl(LPITEMIDLIST pidl, bool create_if_not);
  static void put_fileitem(FileItem* fileitem);
#else
  static FileItem* get_fileitem_by_path(const std::string& path, bool create_if_not);
  static void for_each_child_callback(const char *filename, int attrib, int param);
  static std::string remove_backslash_if_needed(const std::string& filename);
  static std::string get_key_for_filename(const std::string& filename);
  static void put_fileitem(FileItem* fileitem);
#endif

FileSystemModule* FileSystemModule::m_instance = NULL;

FileSystemModule::FileSystemModule()
{
  ASSERT(m_instance == NULL);
  m_instance = this;

  fileitems_map = new FileItemMap;
  thumbnail_map = new ThumbnailMap;

#ifdef USE_PIDLS
  /* get the IMalloc interface */
  HRESULT hr = SHGetMalloc(&shl_imalloc);
  if (hr != S_OK)
    throw std::runtime_error("Error initializing file system. Report this problem. (SHGetMalloc failed.)");

  /* get desktop IShellFolder interface */
  hr = SHGetDesktopFolder(&shl_idesktop);
  if (hr != S_OK)
    throw std::runtime_error("Error initializing file system. Report this problem. (SHGetDesktopFolder failed.)");
#endif

  // first version of the file system
  ++current_file_system_version;

  // get the root element of the file system (this will create
  // the 'rootitem' FileItem)
  getRootFileItem();

  PRINTF("File system module installed\n");
}

FileSystemModule::~FileSystemModule()
{
  PRINTF("File system module: uninstalling\n");
  ASSERT(m_instance == this);

  for (FileItemMap::iterator
         it=fileitems_map->begin(); it!=fileitems_map->end(); ++it) {
    delete it->second;
  }
  fileitems_map->clear();

  for (ThumbnailMap::iterator
         it=thumbnail_map->begin(); it!=thumbnail_map->end(); ++it) {
    it->second->dispose();
  }
  thumbnail_map->clear();

#ifdef USE_PIDLS
  // relase desktop IShellFolder interface
  shl_idesktop->Release();

  // release IMalloc interface
  shl_imalloc->Release();
  shl_imalloc = NULL;
#endif

  delete fileitems_map;
  delete thumbnail_map;

  PRINTF("File system module: uninstalled\n");
  m_instance = NULL;
}

FileSystemModule* FileSystemModule::instance()
{
  return m_instance;
}

void FileSystemModule::refresh()
{
  ++current_file_system_version;
}

IFileItem* FileSystemModule::getRootFileItem()
{
  FileItem* fileitem;

  if (rootitem)
    return rootitem;

  fileitem = new FileItem(NULL);
  rootitem = fileitem;

  //PRINTF("FS: Creating root fileitem %p\n", rootitem);

#ifdef USE_PIDLS
  {
    // get the desktop PIDL
    LPITEMIDLIST pidl = NULL;

    if (SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidl) != S_OK) {
      // TODO do something better
      ASSERT(false);
      exit(1);
    }
    fileitem->pidl = pidl;
    fileitem->fullpidl = pidl;
    fileitem->attrib = SFGAO_FOLDER;
    shl_idesktop->GetAttributesOf(1, (LPCITEMIDLIST *)&pidl, &fileitem->attrib);

    update_by_pidl(fileitem);
  }
#else
  {
    const char* root;

#if defined HAVE_DRIVES
    root = "C:\\";
#else
    root = "/";
#endif

    fileitem->filename = root;
    fileitem->displayname = root;
    fileitem->attrib = FA_DIREC;
  }
#endif

  // insert the file-item in the hash-table
  put_fileitem(fileitem);
  return fileitem;
}

IFileItem* FileSystemModule::getFileItemFromPath(const std::string& path)
{
  IFileItem* fileitem = NULL;

  //PRINTF("FS: get_fileitem_from_path(%s)\n", path.c_str());

#ifdef USE_PIDLS
  {
    ULONG cbEaten = 0UL;
    LPITEMIDLIST fullpidl = NULL;
    SFGAOF attrib = SFGAO_FOLDER;

    if (path.empty()) {
      fileitem = getRootFileItem();
      //PRINTF("FS: > %p (root)\n", fileitem);
      return fileitem;
    }

    if (shl_idesktop->ParseDisplayName
          (NULL, NULL,
           const_cast<LPWSTR>(base::from_utf8(path).c_str()),
           &cbEaten, &fullpidl, &attrib) != S_OK) {
      //PRINTF("FS: > (null)\n");
      return NULL;
    }

    fileitem = get_fileitem_by_fullpidl(fullpidl, true);
    free_pidl(fullpidl);
  }
#else
  {
    std::string buf = remove_backslash_if_needed(path);
    fileitem = get_fileitem_by_path(buf, true);
  }
#endif

  //PRINTF("FS: get_fileitem_from_path(%s) -> %p\n", path.c_str(), fileitem);

  return fileitem;
}

bool FileSystemModule::dirExists(const std::string& path)
{
  struct al_ffblk info;
  int ret;
  std::string path2 = base::join_path(path, "*.*");

  ret = al_findfirst(path2.c_str(), &info, FA_ALL);
  al_findclose(&info);

  return (ret == 0);
}

// ======================================================================
// FileItem class (IFileItem implementation)
// ======================================================================

bool FileItem::isFolder() const
{
  return IS_FOLDER(this);
}

bool FileItem::isBrowsable() const
{
  ASSERT(this->filename != NOTINITIALIZED);

#ifdef USE_PIDLS
  return IS_FOLDER(this)
    && (base::get_file_extension(this->filename) != "zip")
    && ((!this->filename.empty() && (*this->filename.begin()) != ':') ||
        (this->filename == MYPC_CSLID));
#else
  return IS_FOLDER(this);
#endif
}

std::string FileItem::getKeyName() const
{
  ASSERT(this->keyname != NOTINITIALIZED);

  return this->keyname;
}

std::string FileItem::getFileName() const
{
  ASSERT(this->filename != NOTINITIALIZED);

  return this->filename;
}

std::string FileItem::getDisplayName() const
{
  ASSERT(this->displayname != NOTINITIALIZED);

  return this->displayname;
}

IFileItem* FileItem::getParent() const
{
  if (this == rootitem)
    return NULL;
  else {
    ASSERT(this->parent);
    return this->parent;
  }
}

const FileItemList& FileItem::getChildren()
{
  // Is the file-item a folder?
  if (IS_FOLDER(this) &&
      // if the children list is empty, or the file-system version
      // change (it's like to say: the current this->children list
      // is outdated)...
      (this->children.empty() ||
       current_file_system_version > this->version)) {
    FileItemList::iterator it;
    FileItem* child;

    // we have to mark current items as deprecated
    for (it=this->children.begin();
         it!=this->children.end(); ++it) {
      child = static_cast<FileItem*>(*it);
      child->removed = true;
    }

    //PRINTF("FS: Loading files for %p (%s)\n", fileitem, fileitem->displayname);
#ifdef USE_PIDLS
    {
      IShellFolder* pFolder = NULL;
      HRESULT hr;

      if (this == rootitem)
        pFolder = shl_idesktop;
      else {
        hr = shl_idesktop->BindToObject(this->fullpidl,
          NULL, IID_IShellFolder, (LPVOID *)&pFolder);

        if (hr != S_OK)
          pFolder = NULL;
      }

      if (pFolder != NULL) {
        IEnumIDList *pEnum = NULL;
        ULONG c, fetched;

        /* get the interface to enumerate subitems */
        hr = pFolder->EnumObjects(win_get_window(),
          SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &pEnum);

        if (hr == S_OK && pEnum != NULL) {
          LPITEMIDLIST itempidl[256];
          SFGAOF attribs[256];

          /* enumerate the items in the folder */
          while (pEnum->Next(256, itempidl, &fetched) == S_OK && fetched > 0) {
            /* request the SFGAO_FOLDER attribute to know what of the
               item is a folder */
            for (c=0; c<fetched; ++c) {
              attribs[c] = SFGAO_FOLDER;
              pFolder->GetAttributesOf(1, (LPCITEMIDLIST *)itempidl, attribs+c);
            }

            /* generate the FileItems */
            for (c=0; c<fetched; ++c) {
              LPITEMIDLIST fullpidl = concat_pidl(this->fullpidl,
                                                  itempidl[c]);

              child = get_fileitem_by_fullpidl(fullpidl, false);
              if (!child) {
                child = new FileItem(this);

                child->pidl = itempidl[c];
                child->fullpidl = fullpidl;
                child->attrib = attribs[c];

                update_by_pidl(child);
                put_fileitem(child);
              }
              else {
                ASSERT(child->parent == this);
                free_pidl(fullpidl);
                free_pidl(itempidl[c]);
              }

              this->insertChildSorted(child);
            }
          }

          pEnum->Release();
        }

        if (pFolder != shl_idesktop)
          pFolder->Release();
      }
    }
#else
    {
      char buf[MAX_PATH], path[MAX_PATH], tmp[32];

      ustrcpy(path, this->filename.c_str());
      put_backslash(path);

      replace_filename(buf,
                       path,
                       uconvert_ascii("*.*", tmp),
                       sizeof(buf));

#ifdef WORKAROUND_64BITS_SUPPORT
      // we cannot use the for_each_file's 'param' to wrap a 64-bits pointer
      for_each_child_callback_param = this;
      for_each_file(buf, FA_TO_SHOW, for_each_child_callback, 0);
#else
      for_each_file(buf, FA_TO_SHOW,
                    for_each_child_callback,
                    (int)this);
#endif
  }
#endif

    // check old file-items (maybe removed directories or file-items)
    for (it=this->children.begin();
         it!=this->children.end(); ) {
      child = static_cast<FileItem*>(*it);
      ASSERT(child != NULL);

      if (child && child->removed) {
        it = this->children.erase(it);

        fileitems_map->erase(fileitems_map->find(child->keyname));
        delete child;
      }
      else
        ++it;
    }

    // now this file-item is updated
    this->version = current_file_system_version;
  }

  return this->children;
}

void FileItem::createDirectory(const std::string& dirname)
{
  base::make_directory(base::join_path(filename, dirname));

  // Invalidate the children list.
  this->version = 0;
}

bool FileItem::hasExtension(const std::string& csv_extensions)
{
  ASSERT(this->filename != NOTINITIALIZED);

  return base::has_file_extension(this->filename, csv_extensions);
}

she::Surface* FileItem::getThumbnail()
{
  ThumbnailMap::iterator it = thumbnail_map->find(this->filename);
  if (it != thumbnail_map->end())
    return it->second;
  else
    return NULL;
}

void FileItem::setThumbnail(she::Surface* thumbnail)
{
  // destroy the current thumbnail of the file (if exists)
  ThumbnailMap::iterator it = thumbnail_map->find(this->filename);
  if (it != thumbnail_map->end()) {
    it->second->dispose();
    thumbnail_map->erase(it);
  }

  // insert the new one in the map
  thumbnail_map->insert(std::make_pair(this->filename, thumbnail));
}

FileItem::FileItem(FileItem* parent)
{
  //PRINTF("FS: Creating %p fileitem with parent %p\n", this, parent);

  this->keyname = NOTINITIALIZED;
  this->filename = NOTINITIALIZED;
  this->displayname = NOTINITIALIZED;
  this->parent = parent;
  this->version = current_file_system_version;
  this->removed = false;
#ifdef USE_PIDLS
  this->pidl = NULL;
  this->fullpidl = NULL;
  this->attrib = 0;
#else
  this->attrib = 0;
#endif
}

FileItem::~FileItem()
{
  PRINTF("FS: Destroying FileItem() with parent %p\n", parent);

#ifdef USE_PIDLS
  if (this->fullpidl && this->fullpidl != this->pidl) {
    free_pidl(this->fullpidl);
    this->fullpidl = NULL;
  }

  if (this->pidl) {
    free_pidl(this->pidl);
    this->pidl = NULL;
  }
#endif
}

void FileItem::insertChildSorted(FileItem* child)
{
  // this file-item wasn't removed from the last lookup
  child->removed = false;

  // if the fileitem is already in the list we can go back
  if (std::find(children.begin(), children.end(), child) != children.end())
    return;

  bool inserted = false;

  for (FileItemList::iterator
         it=children.begin(); it!=children.end(); ++it) {
    if (*static_cast<FileItem*>(*it) > *child) {
      children.insert(it, child);
      inserted = true;
      break;
    }
  }

  if (!inserted)
    children.push_back(child);
}

/**
 * Compares two FileItems.
 *
 * Based on 'ustricmp' of Allegro. It makes sure that eg "foo.bar"
 * comes before "foo-1.bar", and also that "foo9.bar" comes before
 * "foo10.bar".
 */
int FileItem::compare(const FileItem& that) const
{
  if (IS_FOLDER(this)) {
    if (!IS_FOLDER(&that))
      return -1;
  }
  else if (IS_FOLDER(&that))
    return 1;

  {
    int c1, c2;
    int x1, x2;
    char *t1, *t2;
    const char* s1 = this->displayname.c_str();
    const char* s2 = that.displayname.c_str();

    for (;;) {
      c1 = utolower(ugetxc(&s1));
      c2 = utolower(ugetxc(&s2));

      if ((c1 >= '0') && (c1 <= '9') && (c2 >= '0') && (c2 <= '9')) {
        x1 = ustrtol(s1 - ucwidth(c1), &t1, 10);
        x2 = ustrtol(s2 - ucwidth(c2), &t2, 10);
        if (x1 != x2)
          return x1 - x2;
        else if (t1 - s1 != t2 - s2)
          return (t2 - s2) - (t1 - s1);
        s1 = t1;
        s2 = t2;
      }
      else if (c1 != c2) {
        if (!c1)
          return -1;
        else if (!c2)
          return 1;
        else if (c1 == '.')
          return -1;
        else if (c2 == '.')
          return 1;
        return c1 - c2;
      }

      if (!c1)
        return 0;
    }
  }

  return -1;
}

//////////////////////////////////////////////////////////////////////
// PIDLS: Only for Win32
//////////////////////////////////////////////////////////////////////

#ifdef USE_PIDLS

/* updates the names of the file-item through its PIDL */
static void update_by_pidl(FileItem* fileitem)
{
  STRRET strret;
  WCHAR pszName[MAX_PATH];
  IShellFolder *pFolder = NULL;
  HRESULT hr;

  if (fileitem == rootitem)
    pFolder = shl_idesktop;
  else {
    ASSERT(fileitem->parent);
    hr = shl_idesktop->BindToObject(fileitem->parent->fullpidl,
      NULL, IID_IShellFolder, (LPVOID *)&pFolder);
    if (hr != S_OK)
      pFolder = NULL;
  }

  /****************************************/
  /* get the file name */

  if (pFolder != NULL &&
      pFolder->GetDisplayNameOf(fileitem->pidl,
                                SHGDN_NORMAL | SHGDN_FORPARSING,
                                &strret) == S_OK) {
    StrRetToBuf(&strret, fileitem->pidl, pszName, MAX_PATH);
    fileitem->filename = base::to_utf8(pszName);
  }
  else if (shl_idesktop->GetDisplayNameOf(fileitem->fullpidl,
                                          SHGDN_NORMAL | SHGDN_FORPARSING,
                                          &strret) == S_OK) {
    StrRetToBuf(&strret, fileitem->fullpidl, pszName, MAX_PATH);
    fileitem->filename = base::to_utf8(pszName);
  }
  else
    fileitem->filename = "ERR";

  /****************************************/
  /* get the name to display */

  if (fileitem->isFolder() &&
      pFolder &&
      pFolder->GetDisplayNameOf(fileitem->pidl,
                                SHGDN_INFOLDER,
                                &strret) == S_OK) {
    StrRetToBuf(&strret, fileitem->pidl, pszName, MAX_PATH);
    fileitem->displayname = base::to_utf8(pszName);
  }
  else if (fileitem->isFolder() &&
           shl_idesktop->GetDisplayNameOf(fileitem->fullpidl,
                                          SHGDN_INFOLDER,
                                          &strret) == S_OK) {
    StrRetToBuf(&strret, fileitem->fullpidl, pszName, MAX_PATH);
    fileitem->displayname = base::to_utf8(pszName);
  }
  else {
    fileitem->displayname = base::get_file_name(fileitem->filename);
  }

  if (pFolder != NULL && pFolder != shl_idesktop) {
    pFolder->Release();
  }
}

static LPITEMIDLIST concat_pidl(LPITEMIDLIST pidlHead, LPITEMIDLIST pidlTail)
{
  LPITEMIDLIST pidlNew;
  UINT cb1, cb2;

  ASSERT(pidlHead);
  ASSERT(pidlTail);

  cb1 = get_pidl_size(pidlHead) - sizeof(pidlHead->mkid.cb);
  cb2 = get_pidl_size(pidlTail);

  pidlNew = (LPITEMIDLIST)shl_imalloc->Alloc(cb1 + cb2);
  if (pidlNew) {
    CopyMemory(pidlNew, pidlHead, cb1);
    CopyMemory(((LPSTR)pidlNew) + cb1, pidlTail, cb2);
  }

  return pidlNew;
}

static UINT get_pidl_size(LPITEMIDLIST pidl)
{
  UINT cbTotal = 0;

  if (pidl) {
    cbTotal += sizeof(pidl->mkid.cb); /* null terminator */

    while (pidl) {
      cbTotal += pidl->mkid.cb;
      pidl = get_next_pidl(pidl);
    }
  }

  return cbTotal;
}

static LPITEMIDLIST get_next_pidl(LPITEMIDLIST pidl)
{
  if (pidl != NULL && pidl->mkid.cb > 0) {
    pidl = (LPITEMIDLIST)(((LPBYTE)(pidl)) + pidl->mkid.cb);
    if (pidl->mkid.cb > 0)
      return pidl;
  }

  return NULL;
}

static LPITEMIDLIST get_last_pidl(LPITEMIDLIST pidl)
{
  LPITEMIDLIST pidlLast = pidl;
  LPITEMIDLIST pidlNew = NULL;

  while (pidl) {
    pidlLast = pidl;
    pidl = get_next_pidl(pidl);
  }

  if (pidlLast) {
    ULONG sz = get_pidl_size(pidlLast);
    pidlNew = (LPITEMIDLIST)shl_imalloc->Alloc(sz);
    CopyMemory(pidlNew, pidlLast, sz);
  }

  return pidlNew;
}

static LPITEMIDLIST clone_pidl(LPITEMIDLIST pidl)
{
  ULONG sz = get_pidl_size(pidl);
  LPITEMIDLIST pidlNew = (LPITEMIDLIST)shl_imalloc->Alloc(sz);

  CopyMemory(pidlNew, pidl, sz);

  return pidlNew;
}

static LPITEMIDLIST remove_last_pidl(LPITEMIDLIST pidl)
{
  LPITEMIDLIST pidlFirst = pidl;
  LPITEMIDLIST pidlLast = pidl;

  while (pidl) {
    pidlLast = pidl;
    pidl = get_next_pidl(pidl);
  }

  if (pidlLast)
    pidlLast->mkid.cb = 0;

  return pidlFirst;
}

static void free_pidl(LPITEMIDLIST pidl)
{
  shl_imalloc->Free(pidl);
}

static std::string get_key_for_pidl(LPITEMIDLIST pidl)
{
#if 0
  char *key = base_malloc(get_pidl_size(pidl)+1);
  UINT c, i = 0;

  while (pidl) {
    for (c=0; c<pidl->mkid.cb; ++c) {
      if (pidl->mkid.abID[c])
        key[i++] = pidl->mkid.abID[c];
      else
        key[i++] = 1;
    }
    pidl = get_next_pidl(pidl);
  }
  key[i] = 0;

  return key;
#else
  STRRET strret;
  WCHAR pszName[MAX_PATH];
  WCHAR key[4096] = { 0 };
  int len;

  // Go pidl by pidl from the fullpidl to the root (desktop)
  //PRINTF("FS: ***\n");
  pidl = clone_pidl(pidl);
  while (pidl->mkid.cb > 0) {
    if (shl_idesktop->GetDisplayNameOf(pidl,
                                       SHGDN_INFOLDER | SHGDN_FORPARSING,
                                       &strret) == S_OK) {
      if (StrRetToBuf(&strret, pidl, pszName, MAX_PATH) != S_OK)
        pszName[0] = 0;

      //PRINTF("FS: + %s\n", pszName);

      len = wcslen(pszName);
      if (len > 0) {
        if (*key) {
          if (pszName[len-1] != L'\\') {
            memmove(key+len+1, key, sizeof(WCHAR)*(wcslen(key)+1));
            key[len] = L'\\';
          }
          else
            memmove(key+len, key, sizeof(WCHAR)*(wcslen(key)+1));
        }
        else
          key[len] = 0;

        memcpy(key, pszName, sizeof(WCHAR)*len);
      }
    }
    remove_last_pidl(pidl);
  }
  free_pidl(pidl);

  //PRINTF("FS: =%s\n***\n", key);
  return base::to_utf8(key);
#endif
}

static FileItem* get_fileitem_by_fullpidl(LPITEMIDLIST fullpidl, bool create_if_not)
{
  FileItemMap::iterator it = fileitems_map->find(get_key_for_pidl(fullpidl));
  if (it != fileitems_map->end())
    return it->second;

  if (!create_if_not)
    return NULL;

  // new file-item
  FileItem* fileitem = new FileItem(NULL);
  fileitem->fullpidl = clone_pidl(fullpidl);

  fileitem->attrib = SFGAO_FOLDER;
  HRESULT hr = shl_idesktop->GetAttributesOf(1, (LPCITEMIDLIST *)&fileitem->fullpidl,
                                             &fileitem->attrib);
  if (hr == S_OK) {
    LPITEMIDLIST parent_fullpidl = clone_pidl(fileitem->fullpidl);
    remove_last_pidl(parent_fullpidl);

    fileitem->pidl = get_last_pidl(fileitem->fullpidl);
    fileitem->parent = get_fileitem_by_fullpidl(parent_fullpidl, true);

    free_pidl(parent_fullpidl);
  }

  update_by_pidl(fileitem);
  put_fileitem(fileitem);

  //PRINTF("FS: fileitem %p created %s with parent %p\n", fileitem, fileitem->keyname.c_str(), fileitem->parent);

  return fileitem;
}

/**
 * Inserts the @a fileitem in the hash map of items.
 */
static void put_fileitem(FileItem* fileitem)
{
  ASSERT(fileitem->filename != NOTINITIALIZED);
  ASSERT(fileitem->keyname == NOTINITIALIZED);

  fileitem->keyname = get_key_for_pidl(fileitem->fullpidl);

  ASSERT(fileitem->keyname != NOTINITIALIZED);

#ifdef DEBUGMODE
  FileItemMap::iterator it = fileitems_map->find(get_key_for_pidl(fileitem->fullpidl));
  ASSERT(it == fileitems_map->end());
#endif

  // insert this file-item in the hash-table
  fileitems_map->insert(std::make_pair(fileitem->keyname, fileitem));
}

#else

//////////////////////////////////////////////////////////////////////
// Allegro for_each_file: Portable
//////////////////////////////////////////////////////////////////////

static FileItem* get_fileitem_by_path(const std::string& path, bool create_if_not)
{
  if (path.empty())
    return rootitem;

  FileItemMap::iterator it = fileitems_map->find(get_key_for_filename(path));
  if (it != fileitems_map->end())
    return it->second;

  if (!create_if_not)
    return NULL;

  // get the attributes of the file
  int attrib = 0;
  if (!file_exists(path.c_str(), FA_ALL, &attrib)) {
    if (!FileSystemModule::instance()->dirExists(path))
      return NULL;
    attrib = FA_DIREC;
  }

  // new file-item
  FileItem* fileitem = new FileItem(NULL);

  fileitem->filename = path;
  fileitem->displayname = base::get_file_name(path);
  fileitem->attrib = attrib;

  // get the parent
  {
    std::string parent_path = remove_backslash_if_needed(base::join_path(base::get_file_path(path), ""));
    fileitem->parent = get_fileitem_by_path(parent_path, true);
  }

  put_fileitem(fileitem);

  return fileitem;
}

static void for_each_child_callback(const char *filename, int attrib, int param)
{
#ifdef WORKAROUND_64BITS_SUPPORT
  FileItem* fileitem = for_each_child_callback_param;
#else
  FileItem* fileitem = (FileItem*)param;
#endif
  FileItem* child;
  const char *filename_without_path = get_filename(filename);

  if (*filename_without_path == '.' &&
      (ustrcmp(filename_without_path, ".") == 0 ||
       ustrcmp(filename_without_path, "..") == 0))
    return;

  child = get_fileitem_by_path(filename, false);
  if (!child) {
    ASSERT(fileitem != NULL);
    child = new FileItem(fileitem);

    child->filename = filename;
    child->displayname = filename_without_path;
    child->attrib = attrib;

    put_fileitem(child);
  }
  else {
    ASSERT(child->parent == fileitem);
  }

  fileitem->insertChildSorted(child);
}

static std::string remove_backslash_if_needed(const std::string& filename)
{
  if (!filename.empty() && base::is_path_separator(*(filename.end()-1))) {
    int len = filename.size();
#ifdef HAVE_DRIVES
    // if the name is C:\ or something like that, the backslash isn't
    // removed
    if (len == 3 && filename[1] == ':')
      return filename;
#else
    // this is just the root '/' slash
    if (len == 1)
      return filename;
#endif
    return base::remove_path_separator(filename);
  }
  return filename;
}

static std::string get_key_for_filename(const std::string& filename)
{
  std::string buf(filename);

#if !defined CASE_SENSITIVE
  buf = base::string_to_lower(buf);
#endif
  buf = base::fix_path_separators(buf);

  return buf;
}

static void put_fileitem(FileItem* fileitem)
{
  ASSERT(fileitem->filename != NOTINITIALIZED);
  ASSERT(fileitem->keyname == NOTINITIALIZED);

  fileitem->keyname = get_key_for_filename(fileitem->filename);

  ASSERT(fileitem->keyname != NOTINITIALIZED);

  // insert this file-item in the hash-table
  fileitems_map->insert(std::make_pair(fileitem->keyname, fileitem));
}

#endif

} // namespace app
