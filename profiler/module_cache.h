// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_MODULE_CACHE_H_
#define BASE_PROFILER_MODULE_CACHE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/files/file_path.h"
#include "build/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#endif

namespace base {

// Supports cached lookup of modules by address, with caching based on module
// address ranges.
//
// Cached lookup is necessary on Mac for performance, due to an inefficient
// dladdr implementation. See https://crrev.com/487092.
//
// Cached lookup is beneficial on Windows to minimize use of the loader
// lock. Note however that the cache retains a handle to looked-up modules for
// its lifetime, which may result in pinning modules in memory that were
// transiently loaded by the OS.
class BASE_EXPORT ModuleCache {
 public:
  // Module represents a binary module (executable or library) and its
  // associated state.
  class BASE_EXPORT Module {
   public:
    Module() = default;
    virtual ~Module() = default;

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    // Gets the base address of the module.
    virtual uintptr_t GetBaseAddress() const = 0;

    // Gets the opaque binary string that uniquely identifies a particular
    // program version with high probability. This is parsed from headers of the
    // loaded module.
    // For binaries generated by GNU tools:
    //   Contents of the .note.gnu.build-id field.
    // On Windows:
    //   GUID + AGE in the debug image headers of a module.
    virtual std::string GetId() const = 0;

    // Gets the debug basename of the module. This is the basename of the PDB
    // file on Windows and the basename of the binary on other platforms.
    virtual FilePath GetDebugBasename() const = 0;

    // Gets the size of the module.
    virtual size_t GetSize() const = 0;

    // True if this is a native module.
    virtual bool IsNative() const = 0;
  };

  ModuleCache();
  ~ModuleCache();

  // Gets the module containing |address| or nullptr if |address| is not within
  // a module. The returned module remains owned by and has the same lifetime as
  // the ModuleCache object.
  const Module* GetModuleForAddress(uintptr_t address);
  std::vector<const Module*> GetModules() const;

  // Add a non-native module to the cache. Non-native modules represent regions
  // of non-native executable code, like v8 generated code or compiled
  // Java.
  //
  // Note that non-native modules may be embedded within native modules, as in
  // the case of v8 builtin code compiled within Chrome. In that case
  // GetModuleForAddress() will return the non-native module rather than the
  // native module for the memory region it occupies.
  void AddNonNativeModule(std::unique_ptr<const Module> module);

  void InjectNativeModuleForTesting(std::unique_ptr<const Module> module);

 private:
  // Looks for a module containing |address| in |modules| returns the module if
  // found, or null if not.
  static const Module* FindModuleForAddress(
      const std::vector<std::unique_ptr<const Module>>& modules,
      uintptr_t address);

  // Creates a Module object for the specified memory address. Returns null if
  // the address does not belong to a module.
  static std::unique_ptr<const Module> CreateModuleForAddress(
      uintptr_t address);

  // Unsorted vector of cached native modules. The number of loaded modules is
  // generally much less than 100, and more frequently seen modules will tend to
  // be added earlier and thus be closer to the front to the vector. So linear
  // search to find modules should be acceptable.
  std::vector<std::unique_ptr<const Module>> native_modules_;

  // Unsorted vector of non-native modules. Separate from native_modules_ to
  // support preferential lookup of non-native modules embedded in native
  // modules. See comment on AddNonNativeModule().
  std::vector<std::unique_ptr<const Module>> non_native_modules_;
};

}  // namespace base

#endif  // BASE_PROFILER_MODULE_CACHE_H_
