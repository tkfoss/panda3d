/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlFileInterface.cxx
 * @author rdb
 * @date 2011-11-03
 */

#include "rmlFileInterface.h"
#include "virtualFileSystem.h"

RmlFileInterface::
RmlFileInterface(VirtualFileSystem *vfs) : _vfs(vfs) {
  if (_vfs == nullptr) {
    _vfs = VirtualFileSystem::get_global_ptr();
  }
}

Rml::FileHandle RmlFileInterface::
Open(const Rml::String &path) {
  if (rmlui_cat.is_debug()) {
    rmlui_cat.debug() << "Opening " << path << "\n";
  }

  Filename fn = Filename::from_os_specific(path);

  PT(VirtualFile) file = _vfs->get_file(fn);
  if (file == nullptr) {
    if (!_vfs->resolve_filename(fn, get_model_path())) {
      rmlui_cat.error() << "Could not resolve " << fn
        << " along the model-path\n";
      return nullptr;
    }
    file = _vfs->get_file(fn);
    if (file == nullptr) {
      rmlui_cat.error() << "Failed to get " << fn << "\n";
      return nullptr;
    }
  }

  std::istream *str = file->open_read_file(true);
  if (str == nullptr) {
    rmlui_cat.error() << "Failed to open " << fn << " for reading\n";
    return nullptr;
  }

  VirtualFileHandle *handle = new VirtualFileHandle;
  handle->_file = file;
  handle->_stream = str;
  return (Rml::FileHandle) handle;
}

void RmlFileInterface::
Close(Rml::FileHandle file) {
  VirtualFileHandle *handle = (VirtualFileHandle *) file;
  if (handle == nullptr) {
    return;
  }
  _vfs->close_read_file(handle->_stream);
  delete handle;
}

size_t RmlFileInterface::
Read(void *buffer, size_t size, Rml::FileHandle file) {
  VirtualFileHandle *handle = (VirtualFileHandle *) file;
  if (handle == nullptr) {
    return 0;
  }
  handle->_stream->read((char *) buffer, size);
  return handle->_stream->gcount();
}

bool RmlFileInterface::
Seek(Rml::FileHandle file, long offset, int origin) {
  VirtualFileHandle *handle = (VirtualFileHandle *) file;
  if (handle == nullptr) {
    return false;
  }
  switch (origin) {
  case SEEK_SET:
    handle->_stream->seekg(offset, std::ios::beg);
    break;
  case SEEK_CUR:
    handle->_stream->seekg(offset, std::ios::cur);
    break;
  case SEEK_END:
    handle->_stream->seekg(offset, std::ios::end);
    break;
  }
  return !handle->_stream->fail();
}

size_t RmlFileInterface::
Tell(Rml::FileHandle file) {
  VirtualFileHandle *handle = (VirtualFileHandle *) file;
  if (handle == nullptr) {
    return 0;
  }
  return handle->_stream->tellg();
}

size_t RmlFileInterface::
Length(Rml::FileHandle file) {
  VirtualFileHandle *handle = (VirtualFileHandle *) file;
  if (handle == nullptr) {
    return 0;
  }
  return handle->_file->get_file_size(handle->_stream);
}
