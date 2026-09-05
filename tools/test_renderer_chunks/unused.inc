// (AI-assisted) Link-only stand-ins for unrelated renderer entry points; calling one fails the test.
VkResult vmaCreateAllocator(const VmaAllocatorCreateInfo* info, VmaAllocator* allocator) {
    (void)info; (void)allocator;
    abort();
}
void vmaDestroyAllocator(VmaAllocator allocator) {
    (void)allocator;
    abort();
}
VkResult vmaCreateImage(VmaAllocator allocator, const VkImageCreateInfo* image_info,
        const VmaAllocationCreateInfo* allocation_info, VkImage* image, VmaAllocation* allocation,
        VmaAllocationInfo* result) {
    (void)allocator; (void)image_info; (void)allocation_info; (void)image; (void)allocation; (void)result;
    abort();
}
VkResult vmaCreateDedicatedImage(VmaAllocator allocator, const VkImageCreateInfo* image_info,
        const VmaAllocationCreateInfo* allocation_info, void* next, VkImage* image, VmaAllocation* allocation,
        VmaAllocationInfo* result) {
    (void)next;
    return vmaCreateImage(allocator, image_info, allocation_info, image, allocation, result);
}
void vmaDestroyImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation) {
    (void)allocator; (void)image; (void)allocation;
    abort();
}
bool nc_asset_manager_get_shader_baked_asset(nc_asset_manager_t* manager, const char* namespace,
        const char* name, nc_shader_stage_t stage, nc_shader_baked_asset_t* asset) {
    (void)manager; (void)namespace; (void)name; (void)stage; (void)asset;
    abort();
}
void nc_asset_manager_shader_baked_asset_fini(nc_shader_baked_asset_t* asset) {
    (void)asset;
    abort();
}
bool nc_asset_manager_get_texture_baked_asset(nc_asset_manager_t* manager, const char* namespace,
        const char* name, nc_texture_type_t type, nc_texture_baked_asset_t* asset) {
    (void)manager; (void)namespace; (void)name; (void)type; (void)asset;
    abort();
}
void nc_asset_manager_texture_baked_asset_fini(nc_texture_baked_asset_t* asset) {
    (void)asset;
    abort();
}
nc_gpu_memory_preference_t nc_cvar_get_gpu_memory_preference(void) { abort(); }
int nc_cvar_get_selected_gpu(void) { abort(); }
nc_block_highlight_effect_t nc_cvar_get_block_highlight_effect(void) { abort(); }
vkm_ubvec4 nc_cvar_get_block_highlight_color(void) { abort(); }
