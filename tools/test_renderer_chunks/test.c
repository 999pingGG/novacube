// (AI-assisted) Include the implementation to exercise actual private culling, packing and lifetime code.
// Vulkan/VMA endpoints and mesher output are deterministic fixtures; no GPU, SDL window or game is started.
#include "../../src/renderer.c"
#include "unused.inc"

static uint32_t nc__test_quads[2];
static uint32_t nc__test_faces[2];
static uint32_t nc__test_draw_calls;
static uint32_t nc__test_draw_records;
static uint32_t nc__test_next_descriptor = 1;
static bool nc__test_fail_allocation;

static VKAPI_ATTR void VKAPI_CALL nc__test_copy_buffer(VkCommandBuffer command_buffer,
        VkBuffer source, VkBuffer destination, uint32_t count, const VkBufferCopy* copies) {
    (void)command_buffer;
    for (uint32_t i = 0; i < count; i++) {
        memcpy((uint8_t*)destination + copies[i].dstOffset, (const uint8_t*)source + copies[i].srcOffset,
                (size_t)copies[i].size);
    }
}

static VKAPI_ATTR void VKAPI_CALL nc__test_barrier(VkCommandBuffer command_buffer,
        VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags,
        uint32_t memory_count, const VkMemoryBarrier* memory, uint32_t buffer_count,
        const VkBufferMemoryBarrier* buffers, uint32_t image_count, const VkImageMemoryBarrier* images) {
    (void)command_buffer; (void)source; (void)destination; (void)flags;
    (void)buffer_count; (void)buffers; (void)image_count; (void)images;
    assert(memory_count == 1 && (memory->dstAccessMask & VK_ACCESS_SHADER_READ_BIT));
}

void nc_set_error(const char* file, int line, const char* format, ...) {
    (void)file;
    (void)line;
    (void)format;
    assert(nc__test_fail_allocation);
}

VkResult vmaCreateBuffer(VmaAllocator allocator, const VkBufferCreateInfo* info,
        const VmaAllocationCreateInfo* allocation_info, VkBuffer* buffer, VmaAllocation* allocation,
        VmaAllocationInfo* result) {
    (void)allocator;
    (void)allocation_info;
    if (nc__test_fail_allocation) {
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    void* memory = malloc((size_t)info->size);
    memset(memory, 0xfe, (size_t)info->size);
    *buffer = (VkBuffer)memory;
    *allocation = (VmaAllocation)memory;
    *result = (VmaAllocationInfo){ .pMappedData =
            allocation_info->flags & VMA_ALLOCATION_CREATE_MAPPED_BIT ? memory : NULL };
    return VK_SUCCESS;
}

void vmaDestroyBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation) {
    (void)allocator;
    (void)buffer;
    free(allocation);
}

VkResult vmaFlushAllocation(VmaAllocator allocator, VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size) {
    (void)allocator;
    (void)offset;
    (void)size;
    assert(allocation);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL nc__test_allocate_sets(VkDevice device,
        const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
    (void)device;
    for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
        sets[i] = (VkDescriptorSet)(uintptr_t)nc__test_next_descriptor++;
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL nc__test_free_sets(VkDevice device, VkDescriptorPool pool,
        uint32_t count, const VkDescriptorSet* sets) {
    (void)device;
    (void)pool;
    (void)count;
    (void)sets;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL nc__test_update_sets(VkDevice device, uint32_t count,
        const VkWriteDescriptorSet* writes, uint32_t copy_count, const VkCopyDescriptorSet* copies) {
    (void)device;
    (void)copy_count;
    (void)copies;
    for (uint32_t i = 0; i < count; i++) {
        assert(writes[i].pBufferInfo->buffer);
        assert(writes[i].pBufferInfo->range);
    }
}

static VKAPI_ATTR void VKAPI_CALL nc__test_bind_sets(VkCommandBuffer command_buffer,
        VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first, uint32_t count,
        const VkDescriptorSet* sets, uint32_t offset_count, const uint32_t* offsets) {
    (void)command_buffer;
    (void)point;
    (void)layout;
    (void)first;
    (void)count;
    (void)sets;
    (void)offset_count;
    (void)offsets;
}

static VKAPI_ATTR void VKAPI_CALL nc__test_push_constants(VkCommandBuffer command_buffer,
        VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* values) {
    (void)command_buffer;
    (void)layout;
    (void)stages;
    (void)offset;
    assert(size == sizeof(nc__renderer_chunk_push_constants_t));
    assert(((const nc__renderer_chunk_push_constants_t*)values)->transparent <= 1);
}

static VKAPI_ATTR void VKAPI_CALL nc__test_draw_indirect(VkCommandBuffer command_buffer,
        VkBuffer buffer, VkDeviceSize offset, uint32_t count, uint32_t stride) {
    (void)command_buffer;
    assert(count > 0);
    assert(stride == sizeof(VkDrawIndirectCommand));
    const VkDrawIndirectCommand* draws = (const VkDrawIndirectCommand*)((const uint8_t*)buffer + offset);
    for (uint32_t i = 0; i < count; i++) {
        assert(draws[i].vertexCount == 4 && draws[i].firstInstance == 0);
        assert(draws[i].instanceCount > 0);
    }
    nc__test_draw_calls++;
    nc__test_draw_records += count;
}

void nc_mesher_compute_workspace(const nc_block_registry_t* registry, const uint16_t* blocks[3][3][3],
        nc_mesher_workspace_t* workspace) {
    (void)registry;
    (void)blocks;
    (void)workspace;
}

void nc_mesher_compute_chunk(const nc_block_registry_t* registry, const uint16_t* blocks[3][3][3],
        const uint8_t* lights[3][3][3], const nc_mesher_workspace_t* workspace,
        nc_mesh_quad_vec* quads, nc_mesh_face_data_vec* faces, bool transparent) {
    (void)registry;
    (void)blocks;
    (void)lights;
    (void)workspace;
    for (uint32_t i = 0; i < nc__test_quads[transparent]; i++) {
        nc_mesh_quad_vec_append(quads, (nc_mesh_quad_t){ .plane = (uint8_t)i });
    }
    for (uint32_t i = 0; i < nc__test_faces[transparent]; i++) {
        nc_mesh_face_data_vec_append(faces, (nc_mesh_face_data_t){ .texture_layer = (uint16_t)i });
    }
}

static bool nc__test_update(nc_renderer_t* renderer, uint32_t id, uint32_t opaque, uint32_t transparent) {
    const uint16_t* blocks[3][3][3] = { 0 };
    const uint8_t* lights[3][3][3] = { 0 };
    nc__test_quads[0] = opaque;
    nc__test_quads[1] = transparent;
    nc__test_faces[0] = opaque * 2;
    nc__test_faces[1] = transparent * 2;
    return nc_renderer_update_chunk(renderer, id, NULL, blocks, lights);
}

int main(void) {
    vkAllocateDescriptorSets = nc__test_allocate_sets;
    vkFreeDescriptorSets = nc__test_free_sets;
    vkUpdateDescriptorSets = nc__test_update_sets;
    vkCmdBindDescriptorSets = nc__test_bind_sets;
    vkCmdPushConstants = nc__test_push_constants;
    vkCmdDrawIndirect = nc__test_draw_indirect;
    vkCmdCopyBuffer = nc__test_copy_buffer;
    vkCmdPipelineBarrier = nc__test_barrier;
    nc_renderer_t renderer = { .frame_command_buffer = (VkCommandBuffer)(uintptr_t)1 };
    assert(nc__renderer_initialize_buffer_allocator(&renderer, &renderer.chunk_allocator, 4096,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 128));
    assert(nc__renderer_initialize_buffer_allocator(&renderer, &renderer.transfer_allocator, 65536,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, 1));
    const vkm_ivec3 origin = { 0 };
    const vkm_ivec3 distant = { { 100, 0, 0 } };
    uint32_t ids[] = {
        nc_renderer_create_chunk(&renderer, &origin), nc_renderer_create_chunk(&renderer, &origin),
        nc_renderer_create_chunk(&renderer, &distant), nc_renderer_create_chunk(&renderer, &origin),
        nc_renderer_create_chunk(&renderer, &origin),
    };
    assert(nc__test_update(&renderer, ids[0], 4, 0));
    assert(nc__test_update(&renderer, ids[1], 0, 5));
    assert(nc__test_update(&renderer, ids[2], 6, 7));
    assert(nc__test_update(&renderer, ids[3], 80, 80));
    assert(nc__test_update(&renderer, ids[4], 0, 0));
    assert(renderer.chunk_allocator.first_page->next); // Mixed geometry forced a second page.
    const vkm_mat4 projection = { .m00 = 0.01f, .m11 = 0.01f, .m22 = 0.01f, .m33 = 1 };
    assert(nc__renderer_prepare_chunks(&renderer, &projection));
    assert(renderer.chunk_stats.loaded_chunk_count == 5 && renderer.chunk_stats.empty_chunk_count == 1);
    assert(renderer.chunk_stats.culled_chunk_count == 1);
    assert(renderer.chunk_stats.opaque_drawn_chunk_count == 2);
    assert(renderer.chunk_stats.transparent_drawn_chunk_count == 2);

    const nc__renderer_chunk_t* mixed = &renderer.chunks.array[3];
    assert(((uint32_t*)mixed->slice.page->chunk_lookup.mapping)[mixed->slice.offset / 128 + 1] == 0xfefefefe);
    assert(nc__renderer_flush_uploads(&renderer));
    const uint8_t* mesh = (const uint8_t*)mixed->slice.page->buffer + mixed->slice.offset;
    const nc_mesh_quad_t* transparent = (const nc_mesh_quad_t*)(mesh + mixed->transparent_offset * 8);
    const nc_mesh_face_data_t* transparent_faces = (const nc_mesh_face_data_t*)(transparent + 80);
    assert(transparent[79].plane == 79 && transparent_faces[159].texture_layer == 159);
    for (nc__renderer_buffer_page_t* page = renderer.chunk_allocator.first_page; page; page = page->next) {
        for (uint32_t pass = 0; pass < 2; pass++) {
            const VkDrawIndirectCommand* commands = renderer.chunk_indirect.mapping;
            for (uint32_t j = 0; j < page->draw_count[pass]; j++) {
                const VkDrawIndirectCommand* command = &commands[page->draw_first[pass] + j];
                const uint32_t index = ((uint32_t*)page->chunk_lookup.mapping)[command->firstVertex >> 6];
                const nc__renderer_chunk_t* chunk = &renderer.chunks.array[index];
                assert(chunk->visible && chunk->slice.page == page);
                assert(command->instanceCount == (pass ? chunk->transparent_quad_count : chunk->opaque_quad_count));
            }
        }
    }

    for (uint32_t i = 0; i < 4; i++) {
        const nc__renderer_chunk_t* chunk = &renderer.chunks.array[i];
        const uint32_t* lookup = chunk->slice.page->chunk_lookup.mapping;
        const uint32_t first_vertex = chunk->slice.offset / 8 * 4;
        for (uint32_t corner = 0; corner < 4; corner++) {
            const uint32_t vertex = first_vertex + corner;
            assert((vertex & 3) == corner);
            assert(lookup[(vertex >> 2) >> 4] == i);
        }
    }
    const nc__renderer_buffer_page_t uniforms = { .descriptor_set = (VkDescriptorSet)(uintptr_t)999 };
    assert(nc__test_update(&renderer, ids[4], 1, 0));
    assert(nc__renderer_prepare_chunks(&renderer, &projection));
    for (uint32_t multi = 0; multi < 2; multi++) {
        renderer.multi_draw_indirect = multi != 0;
        nc__test_draw_calls = nc__test_draw_records = 0;
        nc__renderer_draw_chunks(&renderer, &uniforms, 0, false);
        nc__renderer_draw_chunks(&renderer, &uniforms, 0, true);
        assert(nc__test_draw_records == 5);
        assert(nc__test_draw_calls == (uint32_t)(multi ? 4 : 5));
    }
    assert(nc__test_update(&renderer, ids[4], 0, 0));
    // Removing the transparent-only chunk moves the empty tail, then removing that tail moves live geometry.
    nc_renderer_destroy_chunk(&renderer, ids[1]);
    nc_renderer_destroy_chunk(&renderer, ids[4]);
    assert(nc__renderer_chunk_pool_get(&renderer.chunk_ids, ids[3]) == 1);
    assert(nc__renderer_prepare_chunks(&renderer, &projection));
    nc__renderer_chunk_t* moved = &renderer.chunks.array[1];
    assert(((uint32_t*)moved->slice.page->chunk_lookup.mapping)[moved->slice.offset / 128] == 1);
    uint32_t reused = nc_renderer_create_chunk(&renderer, &origin);
    assert(reused == ids[4]);
    assert(nc__test_update(&renderer, reused, 2, 0));
    assert(nc__test_update(&renderer, reused, 3, 0));
    uint32_t queued = 0;
    for (uint32_t i = 0; i < renderer.upload_ops.count; i++) {
        queued += renderer.upload_ops.array[i].buffer.chunk_id == reused;
    }
    assert(queued == 1);
    assert(nc__renderer_flush_uploads(&renderer));
    assert(!renderer.upload_ops.count);
    nc__renderer_chunk_t* updated = &renderer.chunks.array[nc__renderer_chunk_pool_get(&renderer.chunk_ids, reused)];
    const nc__renderer_buffer_slice_t before = updated->slice;
    renderer.frame_in_progress = true;
    assert(nc__test_update(&renderer, reused, 1000, 0));
    assert(updated->slice.page != before.page || updated->slice.offset != before.offset);
    assert(renderer.retired_chunk_slices.count == 1);
    const uint32_t old_count = updated->opaque_quad_count;
    nc__test_fail_allocation = true;
    assert(!nc__test_update(&renderer, reused, 10000, 0));
    assert(updated->opaque_quad_count == old_count);
    nc__test_fail_allocation = false;
    assert(nc__test_update(&renderer, reused, 0, 0));
    assert(!updated->slice.page && renderer.retired_chunk_slices.count == 2);
    assert(nc__renderer_chunk_pool_get(&renderer.chunk_ids, reused) < renderer.chunks.count);
    assert(nc__renderer_prepare_chunks(&renderer, &projection));
    uint32_t extra_ids[200];
    const uint32_t old_capacity = renderer.chunk_metadata.capacity;
    for (uint32_t i = 0; i < NC_COUNTOF(extra_ids); i++) {
        extra_ids[i] = nc_renderer_create_chunk(&renderer, &origin);
    }
    assert(nc__renderer_prepare_chunks(&renderer, &projection));
    assert(renderer.chunk_metadata.capacity > old_capacity);
    const nc__renderer_chunk_gpu_t* gpu = renderer.chunk_metadata.mapping;
    assert(gpu[nc__renderer_chunk_pool_get(&renderer.chunk_ids, ids[3])].transparent_quad_count == 80);
    for (uint32_t i = 0; i < NC_COUNTOF(extra_ids); i++) {
        nc_renderer_destroy_chunk(&renderer, extra_ids[i]);
    }
    renderer.frame_in_progress = false;
    nc_renderer_destroy_chunk(&renderer, reused);
    nc_renderer_destroy_chunk(&renderer, ids[0]);
    nc_renderer_destroy_chunk(&renderer, ids[2]);
    nc_renderer_destroy_chunk(&renderer, ids[3]);
    assert(!renderer.chunks.count && !renderer.upload_ops.count);
    assert(nc__renderer_prepare_chunks(&renderer, &projection));
    nc__renderer_buffer_allocator_fini(&renderer, &renderer.chunk_allocator);
    nc__renderer_buffer_allocator_fini(&renderer, &renderer.transfer_allocator);
    nc__renderer_mapped_buffer_fini(&renderer, &renderer.chunk_metadata);
    nc__renderer_mapped_buffer_fini(&renderer, &renderer.chunk_indirect);
    nc__renderer_chunk_pool_fini(&renderer.chunk_ids);
    nc__renderer_chunk_vec_fini(&renderer.chunks);
    nc__renderer_retired_slice_vec_fini(&renderer.retired_chunk_slices);
    nc__renderer_upload_op_vec_fini(&renderer.upload_ops);
    for (uint32_t pass = 0; pass < 2; pass++) {
        nc_mesh_quad_vec_fini(&renderer.mesh_quads[pass]);
        nc_mesh_face_data_vec_fini(&renderer.mesh_faces[pass]);
    }
    puts("Chunk ownership, lookup, culling, indirect commands and update lifetime checks passed. (AI-assisted)");
    return 0;
}
