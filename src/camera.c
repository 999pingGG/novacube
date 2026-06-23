#include <novacube/camera.h>

void nc_camera_rotate(nc_camera_t* camera, const float yaw_delta, const float pitch_delta) {
    camera->yaw = vkm_mod(camera->yaw + yaw_delta, 2.0f * CVKM_PI_F);
    camera->pitch = vkm_clamp(
            camera->pitch + pitch_delta,
            -CVKM_PI_2_F + 0.001f,
            CVKM_PI_2_F - 0.001f);
}

void nc_camera_get_basis(const nc_camera_t* camera, vkm_vec3* forward, vkm_vec3* right, vkm_vec3* up) {
    const float pitch_sine = vkm_sin(camera->pitch);
    const float pitch_cosine = vkm_cos(camera->pitch);
    const float yaw_sine = vkm_sin(camera->yaw);
    const float yaw_cosine = vkm_cos(camera->yaw);

    *forward = (vkm_vec3){ {
        pitch_cosine * yaw_sine,
        pitch_sine,
        pitch_cosine * yaw_cosine,
    } };

    vkm_vec3_cross(&CVKM_VEC3_UP, forward, right);
    vkm_vec3_normalize(right, right);
    vkm_vec3_cross(forward, right, up);
}

void nc_camera_get_view_projection(
    const nc_camera_t* camera,
    const float vertical_fov,
    const float aspect_ratio,
    const float near_plane,
    const float far_plane,
    vkm_mat4* view_projection
) {
    vkm_vec3 forward;
    vkm_vec3 right;
    vkm_vec3 up;
    nc_camera_get_basis(camera, &forward, &right, &up);

    vkm_vec3 camera_target;
    vkm_vec3_add(&camera->position, &forward, &camera_target);

    vkm_mat4 view_matrix;
    vkm_look_at(&camera->position, &camera_target, &CVKM_VEC3_UP, &view_matrix);

    vkm_mat4 projection_matrix;
    vkm_perspective(vertical_fov, aspect_ratio, near_plane, far_plane, &projection_matrix);

    vkm_mul(&projection_matrix, &view_matrix, view_projection);
}
