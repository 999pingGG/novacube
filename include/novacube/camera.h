#pragma once
#ifndef NOVACUBE_CAMERA_H_
#define NOVACUBE_CAMERA_H_

#include <novacube/cvkm.h>

typedef struct nc_camera_t {
    vkm_vec3 position;
    float yaw;
    float pitch;
} nc_camera_t;

void nc_camera_rotate(nc_camera_t* camera, float yaw_delta, float pitch_delta);
void nc_camera_get_basis(const nc_camera_t* camera, vkm_vec3* forward, vkm_vec3* right, vkm_vec3* up);
void nc_camera_get_view_projection(
        const nc_camera_t* camera,
        float vertical_fov,
        float aspect_ratio,
        float near_plane,
        float far_plane,
        vkm_mat4* view_projection);
#endif
