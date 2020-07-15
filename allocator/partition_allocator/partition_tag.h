// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_TAG_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_TAG_H_

#include "base/allocator/partition_allocator/partition_alloc_constants.h"
#include "base/allocator/partition_allocator/partition_cookie.h"
#include "base/allocator/partition_allocator/partition_tag_bitmap.h"
#include "base/notreached.h"
#include "build/build_config.h"

namespace base {

namespace internal {

#if ENABLE_TAG_FOR_CHECKED_PTR2

// Use 16 bits for the partition tag.
// TODO(tasak): add a description about the partition tag.
using PartitionTag = uint16_t;

static constexpr PartitionTag kTagTemporaryInitialValue = 0x0BAD;

// Allocate extra 16 bytes for the partition tag. 14 bytes are unused
// (reserved).
static constexpr size_t kInSlotTagBufferSize = 16;

#if DCHECK_IS_ON()
// The layout inside the slot is |tag|cookie|object|(empty)|cookie|.
static constexpr size_t kPartitionTagOffset =
    kInSlotTagBufferSize + kCookieSize;
#else
// The layout inside the slot is |tag|object|(empty)|.
static constexpr size_t kPartitionTagOffset = kInSlotTagBufferSize;
#endif

ALWAYS_INLINE size_t PartitionTagSizeAdjustAdd(size_t size) {
  PA_DCHECK(size + kInSlotTagBufferSize > size);
  return size + kInSlotTagBufferSize;
}

ALWAYS_INLINE size_t PartitionTagSizeAdjustSubtract(size_t size) {
  PA_DCHECK(size >= kInSlotTagBufferSize);
  return size - kInSlotTagBufferSize;
}

ALWAYS_INLINE PartitionTag* PartitionTagPointer(void* ptr) {
  return reinterpret_cast<PartitionTag*>(reinterpret_cast<char*>(ptr) -
                                         kPartitionTagOffset);
}

ALWAYS_INLINE void* PartitionTagPointerAdjustSubtract(void* ptr) {
  return reinterpret_cast<void*>(reinterpret_cast<char*>(ptr) -
                                 kInSlotTagBufferSize);
}

ALWAYS_INLINE void* PartitionTagPointerAdjustAdd(void* ptr) {
  return reinterpret_cast<void*>(reinterpret_cast<char*>(ptr) +
                                 kInSlotTagBufferSize);
}

ALWAYS_INLINE void PartitionTagSetValue(void* ptr, PartitionTag value) {
  *PartitionTagPointer(ptr) = value;
}

ALWAYS_INLINE PartitionTag PartitionTagGetValue(void* ptr) {
  return *PartitionTagPointer(ptr);
}

ALWAYS_INLINE void PartitionTagClearValue(void* ptr) {
  PA_DCHECK(PartitionTagGetValue(ptr));
  *PartitionTagPointer(ptr) = 0;
}

#elif ENABLE_TAG_FOR_MTE_CHECKED_PTR

// Use 8 bits for the partition tag.
// TODO(tasak): add a description about the partition tag.
using PartitionTag = uint8_t;

static_assert(
    sizeof(PartitionTag) == tag_bitmap::kPartitionTagSize,
    "sizeof(PartitionTag) must be equal to bitmap::kPartitionTagSize.");

static constexpr PartitionTag kTagTemporaryInitialValue = 0xAD;

static constexpr size_t kInSlotTagBufferSize = 0;

ALWAYS_INLINE size_t PartitionTagSizeAdjustAdd(size_t size) {
  return size;
}

ALWAYS_INLINE size_t PartitionTagSizeAdjustSubtract(size_t size) {
  return size;
}

ALWAYS_INLINE PartitionTag* PartitionTagPointer(void* ptr) {
  // See the comment explaining the layout in partition_tag_bitmap.h.
  uintptr_t pointer_as_uintptr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t bitmap_base =
      (pointer_as_uintptr & kSuperPageBaseMask) + kPartitionPageSize;
  uintptr_t offset =
      (pointer_as_uintptr & kSuperPageOffsetMask) - kPartitionPageSize;
  // Not to depend on partition_address_space.h and PartitionAllocGigaCage
  // feature, use "offset" to see whether the given ptr is_direct_mapped or not.
  // DirectMap object should cause this PA_DCHECK's failure, as tags aren't
  // currently supported there.
  PA_DCHECK(offset >= kReservedTagBitmapSize);
  size_t bitmap_offset = (offset - kReservedTagBitmapSize) >>
                         tag_bitmap::kBytesPerPartitionTagShift
                             << tag_bitmap::kPartitionTagSizeShift;
  return reinterpret_cast<PartitionTag* const>(bitmap_base + bitmap_offset);
}

ALWAYS_INLINE void* PartitionTagPointerAdjustSubtract(void* ptr) {
  return ptr;
}

ALWAYS_INLINE void* PartitionTagPointerAdjustAdd(void* ptr) {
  return ptr;
}

ALWAYS_INLINE void PartitionTagSetValue(void* ptr, PartitionTag value) {
  *PartitionTagPointer(ptr) = value;
}

ALWAYS_INLINE PartitionTag PartitionTagGetValue(void* ptr) {
  return *PartitionTagPointer(ptr);
}

ALWAYS_INLINE void PartitionTagClearValue(void* ptr) {
  PA_DCHECK(PartitionTagGetValue(ptr));
  *PartitionTagPointer(ptr) = 0;
}

#else  // !ENABLE_TAG_FOR_CHECKED_PTR2

using PartitionTag = uint16_t;

static constexpr PartitionTag kTagTemporaryInitialValue = 0;
static constexpr size_t kInSlotTagBufferSize = 0;

ALWAYS_INLINE size_t PartitionTagSizeAdjustAdd(size_t size) {
  return size;
}

ALWAYS_INLINE size_t PartitionTagSizeAdjustSubtract(size_t size) {
  return size;
}

ALWAYS_INLINE PartitionTag* PartitionTagPointer(void* ptr) {
  NOTREACHED();
  return nullptr;
}

ALWAYS_INLINE void* PartitionTagPointerAdjustSubtract(void* ptr) {
  return ptr;
}

ALWAYS_INLINE void* PartitionTagPointerAdjustAdd(void* ptr) {
  return ptr;
}

ALWAYS_INLINE void PartitionTagSetValue(void*, PartitionTag) {}

ALWAYS_INLINE PartitionTag PartitionTagGetValue(void*) {
  return 0;
}

ALWAYS_INLINE void PartitionTagClearValue(void* ptr) {}

#endif  // !ENABLE_TAG_FOR_CHECKED_PTR2

}  // namespace internal
}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_TAG_H_