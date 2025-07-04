/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_DRM_H
#define TU_DRM_H

#include "tu_common.h"

struct tu_u_trace_syncobj;
struct vdrm_bo;

enum tu_bo_alloc_flags {
   TU_BO_ALLOC_NO_FLAGS = 0,
   TU_BO_ALLOC_ALLOW_DUMP = 1 << 0,
   TU_BO_ALLOC_GPU_READ_ONLY = 1 << 1,
   TU_BO_ALLOC_REPLAYABLE = 1 << 2,
   TU_BO_ALLOC_INTERNAL_RESOURCE = 1 << 3,
   TU_BO_ALLOC_DMABUF = 1 << 4,
   TU_BO_ALLOC_SHAREABLE = 1 << 5,
};

/* Define tu_timeline_sync type based on drm syncobj for a point type
 * for vk_sync_timeline, and the logic to handle is mostly copied from
 * anv_bo_sync since it seems it can be used by similar way to anv.
 */
enum tu_timeline_sync_state {
   /** Indicates that this is a new (or newly reset fence) */
   TU_TIMELINE_SYNC_STATE_RESET,

   /** Indicates that this fence has been submitted to the GPU but is still
    * (as far as we know) in use by the GPU.
    */
   TU_TIMELINE_SYNC_STATE_SUBMITTED,

   TU_TIMELINE_SYNC_STATE_SIGNALED,
};

enum tu_mem_sync_op {
   TU_MEM_SYNC_CACHE_TO_GPU,
   TU_MEM_SYNC_CACHE_FROM_GPU,
};

struct tu_bo {
   uint32_t gem_handle;
#ifdef TU_HAS_VIRTIO
   uint32_t res_id;
#endif
   uint64_t size;
   uint64_t iova;
   void *map;
   const char *name; /* pointer to device->bo_sizes's entry's name */
   int32_t refcnt;

   uint32_t submit_bo_list_idx;
   uint32_t dump_bo_list_idx;

#ifdef TU_HAS_KGSL
   /* We have to store fd returned by ion_fd_data
    * in order to be able to mmap this buffer and to
    * export file descriptor.
    */
   int shared_fd;
#endif

   bool implicit_sync : 1;
   bool never_unmap : 1;
   bool cached_non_coherent : 1;

   bool dump;

   /* Pointer to the vk_object_base associated with the BO
    * for the purposes of VK_EXT_device_address_binding_report
    */
   struct vk_object_base *base;
};

struct tu_knl {
   const char *name;

   VkResult (*device_init)(struct tu_device *dev);
   void (*device_finish)(struct tu_device *dev);
   int (*device_get_gpu_timestamp)(struct tu_device *dev, uint64_t *ts);
   int (*device_get_suspend_count)(struct tu_device *dev, uint64_t *suspend_count);
   VkResult (*device_check_status)(struct tu_device *dev);
   int (*submitqueue_new)(struct tu_device *dev, int priority, uint32_t *queue_id);
   void (*submitqueue_close)(struct tu_device *dev, uint32_t queue_id);
   VkResult (*bo_init)(struct tu_device *dev, struct vk_object_base *base,
                       struct tu_bo **out_bo, uint64_t size, uint64_t client_iova,
                       VkMemoryPropertyFlags mem_property,
                       enum tu_bo_alloc_flags flags, const char *name);
   VkResult (*bo_init_dmabuf)(struct tu_device *dev, struct tu_bo **out_bo,
                              uint64_t size, int prime_fd);
   int (*bo_export_dmabuf)(struct tu_device *dev, struct tu_bo *bo);
   VkResult (*bo_map)(struct tu_device *dev, struct tu_bo *bo, void *placed_addr);
   void (*bo_allow_dump)(struct tu_device *dev, struct tu_bo *bo);
   void (*bo_finish)(struct tu_device *dev, struct tu_bo *bo);
   void (*bo_set_metadata)(struct tu_device *dev, struct tu_bo *bo,
                           void *metadata, uint32_t metadata_size);
   int (*bo_get_metadata)(struct tu_device *dev, struct tu_bo *bo,
                          void *metadata, uint32_t metadata_size);
   void *(*submit_create)(struct tu_device *device);
   void (*submit_finish)(struct tu_device *device, void *_submit);
   void (*submit_add_entries)(struct tu_device *device, void *_submit,
                              struct tu_cs_entry *entries,
                              unsigned num_entries);
   VkResult (*queue_submit)(struct tu_queue *queue, void *_submit,
                            struct vk_sync_wait *waits, uint32_t wait_count,
                            struct vk_sync_signal *signals, uint32_t signal_count,
                            struct tu_u_trace_submission_data *u_trace_submission_data);
   VkResult (*queue_wait_fence)(struct tu_queue *queue, uint32_t fence,
                                uint64_t timeout_ns);

   const struct vk_device_entrypoint_table *device_entrypoints;
};

struct tu_zombie_vma {
   int fence;
   uint32_t gem_handle;
#ifdef TU_HAS_VIRTIO
   uint32_t res_id;
#endif
   uint64_t iova;
   uint64_t size;
};

struct tu_timeline_sync {
   struct vk_sync base;

   enum tu_timeline_sync_state state;
   uint32_t syncobj;
};

VkResult
tu_bo_init_new_explicit_iova(struct tu_device *dev,
                             struct vk_object_base *base,
                             struct tu_bo **out_bo,
                             uint64_t size,
                             uint64_t client_iova,
                             VkMemoryPropertyFlags mem_property,
                             enum tu_bo_alloc_flags flags,
                             const char *name);

static inline VkResult
tu_bo_init_new(struct tu_device *dev, struct vk_object_base *base,
               struct tu_bo **out_bo, uint64_t size,
               enum tu_bo_alloc_flags flags, const char *name)
{
   return tu_bo_init_new_explicit_iova(
      dev, base, out_bo, size, 0,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      flags, name);
}

VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo **bo,
                  uint64_t size,
                  int fd);

int
tu_bo_export_dmabuf(struct tu_device *dev, struct tu_bo *bo);

void
tu_bo_finish(struct tu_device *dev, struct tu_bo *bo);

VkResult
tu_bo_map(struct tu_device *dev, struct tu_bo *bo, void *placed_addr);

VkResult
tu_bo_unmap(struct tu_device *dev, struct tu_bo *bo, bool reserve);

void
tu_bo_sync_cache(struct tu_device *dev,
                 struct tu_bo *bo,
                 VkDeviceSize offset,
                 VkDeviceSize size,
                 enum tu_mem_sync_op op);

uint32_t tu_get_l1_dcache_size();

void tu_bo_allow_dump(struct tu_device *dev, struct tu_bo *bo);

void tu_bo_set_metadata(struct tu_device *dev, struct tu_bo *bo,
                        void *metadata, uint32_t metadata_size);
int tu_bo_get_metadata(struct tu_device *dev, struct tu_bo *bo,
                       void *metadata, uint32_t metadata_size);

static inline struct tu_bo *
tu_bo_get_ref(struct tu_bo *bo)
{
   p_atomic_inc(&bo->refcnt);
   return bo;
}

VkResult tu_knl_kgsl_load(struct tu_instance *instance, int fd);

struct _drmVersion;
VkResult tu_knl_drm_msm_load(struct tu_instance *instance,
                             int fd, struct _drmVersion *version,
                             struct tu_physical_device **out);
VkResult tu_knl_drm_virtio_load(struct tu_instance *instance,
                                int fd, struct _drmVersion *version,
                                struct tu_physical_device **out);

VkResult
tu_enumerate_devices(struct vk_instance *vk_instance);
VkResult
tu_physical_device_try_create(struct vk_instance *vk_instance,
                              struct _drmDevice *drm_device,
                              struct vk_physical_device **out);

VkResult
tu_drm_device_init(struct tu_device *dev);

void
tu_drm_device_finish(struct tu_device *dev);

int
tu_device_get_gpu_timestamp(struct tu_device *dev,
                            uint64_t *ts);

int
tu_device_get_suspend_count(struct tu_device *dev,
                            uint64_t *suspend_count);

VkResult
tu_device_wait_u_trace(struct tu_device *dev, struct tu_u_trace_syncobj *syncobj);

VkResult
tu_device_check_status(struct vk_device *vk_device);

int
tu_drm_submitqueue_new(struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id);

void
tu_drm_submitqueue_close(struct tu_device *dev, uint32_t queue_id);

void *
tu_submit_create(struct tu_device *dev);

void
tu_submit_finish(struct tu_device *dev, void *submit);

void
tu_submit_add_entries(struct tu_device *dev, void *submit,
                      struct tu_cs_entry *entries,
                      unsigned num_entries);

VkResult
tu_queue_submit(struct tu_queue *queue, void *submit,
                struct vk_sync_wait *waits, uint32_t wait_count,
                struct vk_sync_signal *signals, uint32_t signal_count,
                struct tu_u_trace_submission_data *u_trace_submission_data);

VkResult
tu_queue_wait_fence(struct tu_queue *queue, uint32_t fence,
                    uint64_t timeout_ns);

#endif /* TU_DRM_H */
