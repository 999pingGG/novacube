#include <novacube/standard_functions.h>
#include <novacube/terrain_lighting.h>

#define NC__TERRAIN_LIGHT_MASK 0x0f
#define NC__TERRAIN_LIGHT_CHANNEL_SHIFT 4

uint8_t nc_terrain_light_get(const uint8_t packed_light, const nc_terrain_light_channel_t channel) {
    return packed_light >> channel * NC__TERRAIN_LIGHT_CHANNEL_SHIFT & NC__TERRAIN_LIGHT_MASK;
}

void nc_terrain_light_set(
    uint8_t* packed_light,
    const nc_terrain_light_channel_t channel,
    const uint8_t light
) {
    NC_ASSERT(light <= 15);
    const int shift = channel * NC__TERRAIN_LIGHT_CHANNEL_SHIFT;
    const uint8_t mask = (uint8_t)(NC__TERRAIN_LIGHT_MASK << shift);
    *packed_light = (uint8_t)((*packed_light & ~mask) | light << shift);
}

uint8_t nc_terrain_light_get_block(const uint8_t packed_light) {
    return nc_terrain_light_get(packed_light, NC_TERRAIN_LIGHT_CHANNEL_BLOCK);
}

uint8_t nc_terrain_light_get_sky(const uint8_t packed_light) {
    return nc_terrain_light_get(packed_light, NC_TERRAIN_LIGHT_CHANNEL_SKY);
}

void nc_terrain_light_set_block(uint8_t* packed_light, const uint8_t light) {
    nc_terrain_light_set(packed_light, NC_TERRAIN_LIGHT_CHANNEL_BLOCK, light);
}

void nc_terrain_light_set_sky(uint8_t* packed_light, const uint8_t light) {
    nc_terrain_light_set(packed_light, NC_TERRAIN_LIGHT_CHANNEL_SKY, light);
}

static bool nc__terrain_block_has_direct_sky(
    const nc__terrain_chunk_column_t* column,
    const int column_index,
    const int32_t world_y,
    const nc_block_t* block
) {
    return !(block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT) && world_y > column->top_light_blocking_blocks[column_index];
}

static bool nc__terrain_get_light_neighbor(
    const nc_terrain_t* terrain,
    nc__terrain_chunk_t* chunk,
    const uint16_t index,
    const vkm_ivec3* local_coords,
    const vkm_bvec3 offset,
    nc__terrain_block_location_t* result
) {
    (void)local_coords;
    vkm_ivec3 neighbor_chunk_coords;
    uint16_t neighbor_index;
    if (!nc_chunk_offset_block_index(
            &chunk->coords,
            index,
            offset,
            &neighbor_chunk_coords,
            &neighbor_index)) {
        return false;
    }

    nc__terrain_chunk_t* neighbor_chunk = chunk;
    if (neighbor_chunk_coords.x != chunk->coords.x
            || neighbor_chunk_coords.y != chunk->coords.y
            || neighbor_chunk_coords.z != chunk->coords.z) {
        neighbor_chunk = nc__terrain_get_chunk(terrain, &neighbor_chunk_coords);
        if (!neighbor_chunk) {
            return false;
        }
    }

    *result = (nc__terrain_block_location_t){
        .chunk = neighbor_chunk,
        .index = neighbor_index,
    };
    return true;
}

static void nc__terrain_queue_light_node(
    nc_terrain_t* terrain,
    const nc__terrain_light_channel_t channel,
    const nc__terrain_block_location_t* location
) {
    light_node_queue** queued_nodes = (light_node_queue**)&location->chunk->queued_light_nodes[channel];
    if (!*queued_nodes) {
        *queued_nodes = calloc(1, sizeof(**queued_nodes));
    }

    if (light_node_queue_get(*queued_nodes, location->index)) {
        return;
    }
    light_node_queue_set(*queued_nodes, location->index);

    nc__terrain_light_bfs_queue_push(&terrain->light_bfs_queues[channel], (nc__terrain_light_node_t){
        .chunk_coords = location->chunk->coords,
        .index = location->index,
    });
}

static void nc__terrain_queue_light_neighbors(
    nc_terrain_t* terrain,
    const nc__terrain_light_channel_t channel,
    nc__terrain_chunk_t* chunk,
    const uint16_t index,
    const vkm_ivec3* local_coords
) {
    for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
        nc__terrain_block_location_t neighbor;
        if (nc__terrain_get_light_neighbor(
                terrain,
                chunk,
                index,
                local_coords,
                nc__terrain_light_neighbor_offsets[i],
                &neighbor)
                && nc__terrain_get_light(neighbor.chunk->light_levels[neighbor.index], channel) > 1) {
            nc__terrain_queue_light_node(terrain, channel, &neighbor);
        }
    }
}

static void nc__terrain_queue_chunk_boundary_light_removal(nc_terrain_t* terrain, const nc__terrain_chunk_t* chunk) {
    for (int z = 0; z < NC_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_CHUNK_SIZE; x++) {
                if (x != 0 && x != NC_CHUNK_SIZE - 1
                        && y != 0 && y != NC_CHUNK_SIZE - 1
                        && z != 0 && z != NC_CHUNK_SIZE - 1) {
                    continue;
                }

                const uint16_t index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(x, y, z);
                const uint8_t block_light = nc__terrain_get_block_light(chunk->light_levels[index]);
                if (block_light) {
                    nc__terrain_light_removal_bfs_queue_push(
                            &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_BLOCK],
                            (nc__terrain_light_removal_node_t){
                                .chunk_coords = chunk->coords,
                                .index = index,
                                .light_level = block_light,
                            });
                }
                const uint8_t sky_light = nc__terrain_get_sky_light(chunk->light_levels[index]);
                if (sky_light) {
                    nc__terrain_light_removal_bfs_queue_push(
                            &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY],
                            (nc__terrain_light_removal_node_t){
                                .chunk_coords = chunk->coords,
                                .index = index,
                                .light_level = sky_light,
                            });
                }
            }
        }
    }
}

static void nc__terrain_seed_chunk_light(nc_terrain_t* terrain, nc__terrain_chunk_t* chunk) {
    for (uint16_t index = 0; index < NC_BLOCKS_PER_CHUNK; index++) {
        const nc_block_t* block = nc_block_registry_get(
                terrain->block_registry,
                (nc_block_type_t)chunk->blocks[index]);
        if (block->light_emission) {
            nc__terrain_set_block_light(&chunk->light_levels[index], block->light_emission);
            const nc__terrain_block_location_t location = { .chunk = chunk, .index = index };
            nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &location);
        }
    }

    // Existing light in adjacent chunks only needs to be requeued along the newly loaded boundary. Fetch each
    // neighboring chunk once rather than performing a hashmap lookup for every block on every face.
    for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
        const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
        vkm_ivec3 neighbor_coords;
        if (!nc_chunk_offset_coords(&chunk->coords, offset.x, offset.y, offset.z, &neighbor_coords)) {
            continue;
        }
        nc__terrain_chunk_t* neighbor = nc__terrain_get_chunk(terrain, &neighbor_coords);
        if (!neighbor) {
            continue;
        }

        for (int b = 0; b < NC_CHUNK_SIZE; b++) {
            for (int a = 0; a < NC_CHUNK_SIZE; a++) {
                const int x = offset.x < 0 ? NC_CHUNK_SIZE - 1 : offset.x > 0 ? 0 : a;
                const int y = offset.y < 0 ? NC_CHUNK_SIZE - 1 : offset.y > 0 ? 0 : offset.x ? a : b;
                const int z = offset.z < 0 ? NC_CHUNK_SIZE - 1 : offset.z > 0 ? 0 : b;
                const uint16_t index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(x, y, z);
                if (nc__terrain_get_block_light(neighbor->light_levels[index]) > 1) {
                    const nc__terrain_block_location_t location = { .chunk = neighbor, .index = index };
                    nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &location);
                }
            }
        }
    }
}

static void nc__terrain_queue_sky_light_edge(
    nc_terrain_t* terrain,
    const nc__terrain_block_location_t* source,
    const nc__terrain_block_location_t* target,
    const int target_offset_y
) {
    const uint8_t source_light = nc__terrain_get_sky_light(source->chunk->light_levels[source->index]);
    if (source_light <= 1) {
        return;
    }

    const uint8_t propagated = source_light == 15 && target_offset_y < 0 ? 15 : (uint8_t)(source_light - 1);
    if (nc__terrain_get_sky_light(target->chunk->light_levels[target->index]) >= propagated) {
        return;
    }

    const nc_block_t* target_block = nc_block_registry_get(
            terrain->block_registry,
            (nc_block_type_t)target->chunk->blocks[target->index]);
    if (!(target_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)) {
        nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, source);
    }
}

static void nc__terrain_queue_sky_light_edge_both_ways(
    nc_terrain_t* terrain,
    const nc__terrain_block_location_t* a,
    const nc__terrain_block_location_t* b,
    const int b_offset_y
) {
    nc__terrain_queue_sky_light_edge(terrain, a, b, b_offset_y);
    nc__terrain_queue_sky_light_edge(terrain, b, a, -b_offset_y);
}

static void nc__terrain_queue_chunk_sky_light_frontier(nc_terrain_t* terrain, nc__terrain_chunk_t* chunk) {
    const vkm_ivec2 column_coords = nc_chunk_to_chunk_column_coords(&chunk->coords);
    nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, column_coords);
    NC_ASSERT(column);
    const int64_t chunk_min_y = (int64_t)chunk->coords.y * NC_CHUNK_SIZE;
    const int64_t chunk_max_y = chunk_min_y + NC_CHUNK_SIZE - 1;

    // Within a chunk, direct sky can enter indirect space only across a horizontal heightmap discontinuity. Check only
    // the vertical interval between the two top blockers instead of inspecting all six neighbors of every voxel.
    for (int z = 0; z < NC_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_CHUNK_SIZE; x++) {
            const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(x, z);
            const int32_t top = (*column)->top_light_blocking_blocks[column_index];
            for (int direction = 0; direction < 2; direction++) {
                const int neighbor_x = x + (direction == 0);
                const int neighbor_z = z + (direction == 1);
                if (neighbor_x >= NC_CHUNK_SIZE || neighbor_z >= NC_CHUNK_SIZE) {
                    continue;
                }

                const int neighbor_column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(neighbor_x, neighbor_z);
                const int32_t neighbor_top = (*column)->top_light_blocking_blocks[neighbor_column_index];
                if (top == neighbor_top) {
                    continue;
                }

                const int32_t lower_top = top < neighbor_top ? top : neighbor_top;
                const int32_t upper_top = top > neighbor_top ? top : neighbor_top;
                const int64_t first_world_y = chunk_min_y > (int64_t)lower_top + 1
                        ? chunk_min_y
                        : (int64_t)lower_top + 1;
                const int64_t last_world_y = chunk_max_y < upper_top ? chunk_max_y : upper_top;

                for (int64_t world_y = first_world_y; world_y <= last_world_y; world_y++) {
                    const int y = (int)(world_y - chunk_min_y);
                    const nc__terrain_block_location_t location = {
                        .chunk = chunk,
                        .index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(x, y, z),
                    };
                    const nc__terrain_block_location_t neighbor = {
                        .chunk = chunk,
                        .index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(neighbor_x, y, neighbor_z),
                    };
                    nc__terrain_queue_sky_light_edge_both_ways(terrain, &location, &neighbor, 0);
                }
            }
        }
    }

    // Chunk loading creates new light paths only across the six touching faces. Fetch each neighbor once and compare
    // the two faces directly; both directions are checked so either chunk can refill the other.
    for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
        const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
        vkm_ivec3 neighbor_coords;
        if (!nc_chunk_offset_coords(&chunk->coords, offset.x, offset.y, offset.z, &neighbor_coords)) {
            continue;
        }

        nc__terrain_chunk_t* neighbor_chunk = nc__terrain_get_chunk(terrain, &neighbor_coords);
        if (!neighbor_chunk) {
            continue;
        }

        for (int b = 0; b < NC_CHUNK_SIZE; b++) {
            for (int a = 0; a < NC_CHUNK_SIZE; a++) {
                const int x = offset.x < 0 ? 0 : offset.x > 0 ? NC_CHUNK_SIZE - 1 : a;
                const int y = offset.y < 0 ? 0 : offset.y > 0 ? NC_CHUNK_SIZE - 1 : offset.x ? a : b;
                const int z = offset.z < 0 ? 0 : offset.z > 0 ? NC_CHUNK_SIZE - 1 : b;
                const int neighbor_x = offset.x < 0 ? NC_CHUNK_SIZE - 1 : offset.x > 0 ? 0 : x;
                const int neighbor_y = offset.y < 0 ? NC_CHUNK_SIZE - 1 : offset.y > 0 ? 0 : y;
                const int neighbor_z = offset.z < 0 ? NC_CHUNK_SIZE - 1 : offset.z > 0 ? 0 : z;
                const nc__terrain_block_location_t location = {
                    .chunk = chunk,
                    .index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(x, y, z),
                };
                const nc__terrain_block_location_t neighbor = {
                    .chunk = neighbor_chunk,
                    .index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(neighbor_x, neighbor_y, neighbor_z),
                };
                nc__terrain_queue_sky_light_edge_both_ways(terrain, &location, &neighbor, offset.y);
            }
        }
    }
}

static void nc__terrain_seed_chunk_sky_light(nc_terrain_t* terrain, nc__terrain_chunk_t* chunk) {
    const vkm_ivec2 column_coords = nc_chunk_to_chunk_column_coords(&chunk->coords);
    nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, column_coords);
    NC_ASSERT(column);

    // The heightmap already guarantees that every block above its top blocker is transparent. Assign only that
    // direct-sky interval without repeating a block-registry lookup for every voxel or queueing every voxel. A later
    // frontier scan queues only sources that can actually improve a darker neighbor.
    const int64_t chunk_min_y = (int64_t)chunk->coords.y * NC_CHUNK_SIZE;
    for (int z = 0; z < NC_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_CHUNK_SIZE; x++) {
            const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(x, z);
            int64_t first_y = (int64_t)(*column)->top_light_blocking_blocks[column_index] + 1 - chunk_min_y;
            if (first_y < 0) {
                first_y = 0;
            }
            if (first_y >= NC_CHUNK_SIZE) {
                continue;
            }

            for (int y = (int)first_y; y < NC_CHUNK_SIZE; y++) {
                const uint16_t index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(x, y, z);
                nc__terrain_set_sky_light(&chunk->light_levels[index], 15);
            }
        }
    }
}

static void nc__terrain_reconcile_sky_light_cell(
    nc_terrain_t* terrain,
    const nc__terrain_chunk_column_t* column,
    nc__terrain_chunk_t* chunk,
    const vkm_ivec3 local_coords,
    const nc__terrain_sky_light_reconcile_mode_t mode
) {
    const uint16_t index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z);
    const nc_block_t* block = nc_block_registry_get(terrain->block_registry, (nc_block_type_t)chunk->blocks[index]);
    const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(local_coords.x, local_coords.z);
    const int32_t world_y = nc_chunk_local_coord_to_block_coord(chunk->coords.y, local_coords.y);
    const uint8_t old_light = nc__terrain_get_sky_light(chunk->light_levels[index]);
    const bool direct_sky = nc__terrain_block_has_direct_sky(column, column_index, world_y, block);

    if (direct_sky) {
        if (old_light == 15) {
            return;
        }

        nc__terrain_set_sky_light(&chunk->light_levels[index], 15);
        if (mode == NC__TERRAIN_SKY_LIGHT_RECONCILE_INCREMENTAL) {
            const nc__terrain_block_location_t location = { .chunk = chunk, .index = index };
            nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, &location);
        }
    } else {
        if (!old_light || (mode == NC__TERRAIN_SKY_LIGHT_RECONCILE_SEEDED && old_light != 15)) {
            return;
        }

        nc__terrain_set_sky_light(&chunk->light_levels[index], 0);
        if (mode == NC__TERRAIN_SKY_LIGHT_RECONCILE_INCREMENTAL || terrain->sky_light_has_propagated) {
            nc__terrain_light_removal_bfs_queue_push(
                    &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY],
                    (nc__terrain_light_removal_node_t){
                        .chunk_coords = chunk->coords,
                        .index = index,
                        .light_level = old_light,
                    });
        }
    }
    nc__terrain_mark_block_chunks_dirty(terrain, &chunk->coords, &local_coords);
}

static void nc__terrain_reconcile_chunk_column_sky_light(
    nc_terrain_t* terrain,
    const vkm_ivec2 column_coords,
    const nc__terrain_chunk_column_t* column,
    const int32_t old_top_light_blocking_blocks[NC_BLOCK_COLUMNS_PER_CHUNK]
) {
    nc__terrain_sky_light_chunk_range_t ranges[NC_BLOCK_COLUMNS_PER_CHUNK];
    int32_t first_affected_chunk_y = INT32_MAX;
    int32_t last_affected_chunk_y = INT32_MIN;

    for (int z = 0; z < NC_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_CHUNK_SIZE; x++) {
            const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(x, z);
            nc__terrain_sky_light_chunk_range_t* range = &ranges[column_index];
            const int32_t old_top = old_top_light_blocking_blocks[column_index];
            const int32_t new_top = column->top_light_blocking_blocks[column_index];
            if (old_top == new_top) {
                range->first_chunk_y = 1;
                range->last_chunk_y = 0;
                continue;
            }

            // Only blocks above the lower height and at or below the higher height changed direct-sky status.
            range->lower_top = old_top < new_top ? old_top : new_top;
            range->upper_top = old_top > new_top ? old_top : new_top;
            range->first_chunk_y = nc_block_coord_to_chunk_coord(range->lower_top + 1);
            range->last_chunk_y = nc_block_coord_to_chunk_coord(range->upper_top);
            if (range->first_chunk_y < column->min_loaded_chunk_y) {
                range->first_chunk_y = column->min_loaded_chunk_y;
            }
            if (range->last_chunk_y > column->max_loaded_chunk_y) {
                range->last_chunk_y = column->max_loaded_chunk_y;
            }
            if (range->first_chunk_y > range->last_chunk_y) {
                continue;
            }
            if (range->first_chunk_y < first_affected_chunk_y) {
                first_affected_chunk_y = range->first_chunk_y;
            }
            if (range->last_chunk_y > last_affected_chunk_y) {
                last_affected_chunk_y = range->last_chunk_y;
            }
        }
    }

    for (int32_t chunk_y = first_affected_chunk_y; chunk_y <= last_affected_chunk_y; chunk_y++) {
        const vkm_ivec3 chunk_coords = { { column_coords.x, chunk_y, column_coords.y } };
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
        if (!chunk) {
            continue;
        }

        const int64_t chunk_min_y = (int64_t)chunk_y * NC_CHUNK_SIZE;
        const int64_t chunk_max_y = chunk_min_y + NC_CHUNK_SIZE - 1;
        for (int z = 0; z < NC_CHUNK_SIZE; z++) {
            for (int x = 0; x < NC_CHUNK_SIZE; x++) {
                const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(x, z);
                const nc__terrain_sky_light_chunk_range_t* range = &ranges[column_index];
                if (chunk_y < range->first_chunk_y || chunk_y > range->last_chunk_y) {
                    continue;
                }

                const int64_t first_y = chunk_min_y > (int64_t)range->lower_top + 1
                        ? chunk_min_y
                        : (int64_t)range->lower_top + 1;
                const int64_t last_y = chunk_max_y < range->upper_top ? chunk_max_y : range->upper_top;
                NC_ASSERT(first_y >= chunk_min_y && first_y <= last_y && last_y <= chunk_max_y);
                for (int64_t world_y = first_y; world_y <= last_y; world_y++) {
                    const int y = (int)(world_y - chunk_min_y);
                    nc__terrain_reconcile_sky_light_cell(
                            terrain,
                            column,
                            chunk,
                            (vkm_ivec3){ { x, y, z } },
                            NC__TERRAIN_SKY_LIGHT_RECONCILE_SEEDED);
                }
            }
        }
    }
}

static void nc__terrain_reconcile_block_column_sky_light(
    nc_terrain_t* terrain,
    const vkm_ivec2 column_coords,
    const nc__terrain_chunk_column_t* column,
    const int x,
    const int z
) {
    for (int32_t chunk_y = column->min_loaded_chunk_y; chunk_y <= column->max_loaded_chunk_y; chunk_y++) {
        const vkm_ivec3 chunk_coords = { { column_coords.x, chunk_y, column_coords.y } };
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
        if (!chunk) {
            continue;
        }

        for (int y = 0; y < NC_CHUNK_SIZE; y++) {
            nc__terrain_reconcile_sky_light_cell(
                    terrain,
                    column,
                    chunk,
                    (vkm_ivec3){ { x, y, z } },
                    NC__TERRAIN_SKY_LIGHT_RECONCILE_INCREMENTAL);
        }
    }
}

static bool nc__terrain_remove_light(nc_terrain_t* terrain, const uint64_t deadline) {
    nc__terrain_light_removal_bfs_queue* queue = &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_BLOCK];
    while (nc__terrain_light_removal_bfs_queue_count(queue)) {
        if (SDL_GetTicksNS() >= deadline) {
            return false;
        }

        const nc__terrain_light_removal_node_t node = nc__terrain_light_removal_bfs_queue_pop(queue);
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &node.chunk_coords);
        if (!chunk) {
            // Dangling pointer successfully avoided!
            continue;
        }

        vkm_ivec3 local_coords;
        nc_chunk_index_to_local_coords(node.index, &local_coords);

        for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
            nc__terrain_block_location_t neighbor;
            if (!nc__terrain_get_light_neighbor(
                    terrain,
                    chunk,
                    node.index,
                    &local_coords,
                    nc__terrain_light_neighbor_offsets[i],
                    &neighbor)) {
                continue;
            }

            const uint8_t neighbor_light_level = nc__terrain_get_block_light(neighbor.chunk->light_levels[neighbor.index]);
            if (!neighbor_light_level) {
                continue;
            }

            if (neighbor_light_level < node.light_level) {
                const nc_block_t* neighbor_block = nc_block_registry_get(
                        terrain->block_registry,
                        (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
                nc__terrain_set_block_light(
                        &neighbor.chunk->light_levels[neighbor.index],
                        neighbor_block->light_emission);

                vkm_ivec3 neighbor_local_coords;
                nc_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
                nc__terrain_mark_block_chunks_dirty(terrain, &neighbor.chunk->coords, &neighbor_local_coords);
                nc__terrain_light_removal_bfs_queue_push(
                        queue,
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = neighbor.chunk->coords,
                            .index = neighbor.index,
                            .light_level = neighbor_light_level,
                        });

                if (neighbor_block->light_emission) {
                    nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &neighbor);
                }
            } else {
                // This light came from another source and will refill the invalidated region after removal finishes.
                nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &neighbor);
            }
        }
    }
    return true;
}

static bool nc__terrain_propagate_light(
    nc_terrain_t* terrain,
    const nc__terrain_light_channel_t channel,
    const uint64_t deadline
) {
    nc__terrain_light_bfs_queue* queue = &terrain->light_bfs_queues[channel];
    while (nc__terrain_light_bfs_queue_count(queue)) {
        if (SDL_GetTicksNS() >= deadline) {
            return false;
        }

        const nc__terrain_light_node_t node = nc__terrain_light_bfs_queue_pop(queue);
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &node.chunk_coords);
        if (!chunk) {
            // Dangling pointer successfully avoided!
            continue;
        }

        NC_ASSERT(chunk->queued_light_nodes[channel]);
        light_node_queue_clear((light_node_queue*)chunk->queued_light_nodes[channel], node.index);

        vkm_ivec3 local_coords;
        nc_chunk_index_to_local_coords(node.index, &local_coords);

        const uint8_t light_level = nc__terrain_get_light(chunk->light_levels[node.index], channel);
        if (light_level <= 1) {
            continue;
        }

        for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
            const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
            nc__terrain_block_location_t neighbor;
            if (!nc__terrain_get_light_neighbor(
                    terrain,
                    chunk,
                    node.index,
                    &local_coords,
                    offset,
                    &neighbor)) {
                continue;
            }

            const nc_block_t* neighbor_block = nc_block_registry_get(
                    terrain->block_registry,
                    (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
            const uint8_t propagated = channel == NC__TERRAIN_LIGHT_CHANNEL_SKY
                    && light_level == 15
                    && offset.y < 0
                    ? 15
                    : (uint8_t)(light_level - 1);
            if ((neighbor_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)
                    || nc__terrain_get_light(neighbor.chunk->light_levels[neighbor.index], channel) >= propagated) {
                continue;
            }

            nc__terrain_set_light(&neighbor.chunk->light_levels[neighbor.index], channel, propagated);
            vkm_ivec3 neighbor_local_coords;
            nc_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
            nc__terrain_mark_block_chunks_dirty(terrain, &neighbor.chunk->coords, &neighbor_local_coords);
            nc__terrain_queue_light_node(terrain, channel, &neighbor);
        }
    }
    return true;
}

static bool nc__terrain_remove_sky_light(nc_terrain_t* terrain, const uint64_t deadline) {
    // Do not use the block-light level comparison here. Sky light can contain equal-level plateaus fed by several
    // entrances; after the last entrance closes, treating an equal neighbor as an independent source leaves stale
    // light behind. Flood all non-direct sky light, stop at authoritative direct-sky cells, then propagate them again.
    nc__terrain_light_removal_bfs_queue* queue = &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY];
    while (nc__terrain_light_removal_bfs_queue_count(queue)) {
        if (SDL_GetTicksNS() >= deadline) {
            return false;
        }

        const nc__terrain_light_removal_node_t node = nc__terrain_light_removal_bfs_queue_pop(queue);
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &node.chunk_coords);
        if (!chunk) {
            continue;
        }

        vkm_ivec3 local_coords;
        nc_chunk_index_to_local_coords(node.index, &local_coords);
        for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
            const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
            nc__terrain_block_location_t neighbor;
            if (!nc__terrain_get_light_neighbor(
                    terrain,
                    chunk,
                    node.index,
                    &local_coords,
                    offset,
                    &neighbor)) {
                continue;
            }
            const uint8_t neighbor_light = nc__terrain_get_sky_light(neighbor.chunk->light_levels[neighbor.index]);
            if (!neighbor_light) {
                continue;
            }
            vkm_ivec3 neighbor_local_coords;
            nc_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
            const vkm_ivec2 column_coords = nc_chunk_to_chunk_column_coords(&neighbor.chunk->coords);
            nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(
                    &terrain->chunk_columns,
                    column_coords);
            NC_ASSERT(column);
            const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(neighbor_local_coords.x, neighbor_local_coords.z);
            const int32_t world_y = nc_chunk_local_coord_to_block_coord(
                    neighbor.chunk->coords.y,
                    neighbor_local_coords.y);
            const nc_block_t* neighbor_block = nc_block_registry_get(
                    terrain->block_registry,
                    (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
            const bool direct_sky = nc__terrain_block_has_direct_sky(*column, column_index, world_y, neighbor_block);
            if (!direct_sky) {
                nc__terrain_set_sky_light(&neighbor.chunk->light_levels[neighbor.index], 0);
                nc__terrain_mark_block_chunks_dirty(terrain, &neighbor.chunk->coords, &neighbor_local_coords);
                nc__terrain_light_removal_bfs_queue_push(
                        queue,
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = neighbor.chunk->coords,
                            .index = neighbor.index,
                            .light_level = neighbor_light,
                        });
            } else {
                nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, &neighbor);
            }
        }
    }

    return true;
}
