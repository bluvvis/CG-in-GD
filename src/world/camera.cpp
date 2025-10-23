#define _USE_MATH_DEFINES

#include "camera.h"

#include "utils/error_handler.h"

#include <math.h>


using namespace cg::world;

cg::world::camera::camera() : theta(0.f), phi(0.f), height(1080.f), width(1920.f),
							  aspect_ratio(1920.f / 1080.f), angle_of_view(1.04719f),
							  z_near(0.001f), z_far(100.f), position(float3{0.f, 0.f, 0.f})
{
}

cg::world::camera::~camera() {}

void cg::world::camera::set_position(float3 in_position)
{
	position = in_position;
}

void cg::world::camera::set_theta(float in_theta)
{
	theta = in_theta;
}

void cg::world::camera::set_phi(float in_phi)
{
	phi = in_phi;
}

void cg::world::camera::set_angle_of_view(float in_aov)
{
	angle_of_view = in_aov;
}

void cg::world::camera::set_height(float in_height)
{
	height = in_height;
	aspect_ratio = width / height;
}

void cg::world::camera::set_width(float in_width)
{
	width = in_width;
	aspect_ratio = width / height;
}

void cg::world::camera::set_z_near(float in_z_near)
{
	z_near = in_z_near;
}

void cg::world::camera::set_z_far(float in_z_far)
{
	z_far = in_z_far;
}

const float4x4 cg::world::camera::get_view_matrix() const
{
	float3 direction = get_direction();
	float3 up = get_up();
	float3 right = get_right();

	return float4x4{
			{right.x, up.x, direction.x, 0},
			{right.y, up.y, direction.y, 0},
			{right.z, up.z, direction.z, 0},
			{-dot(right, position), -dot(up, position), -dot(direction, position), 1}};
}

#ifdef DX12
const DirectX::XMMATRIX cg::world::camera::get_dxm_view_matrix() const
{
	// TODO Lab: 3.08 Implement `get_dxm_view_matrix`, `get_dxm_projection_matrix`, and `get_dxm_mvp_matrix` methods of `camera`
	return DirectX::XMMatrixIdentity();
}

const DirectX::XMMATRIX cg::world::camera::get_dxm_projection_matrix() const
{
	// TODO Lab: 3.08 Implement `get_dxm_view_matrix`, `get_dxm_projection_matrix`, and `get_dxm_mvp_matrix` methods of `camera`
	return DirectX::XMMatrixIdentity();
}

const DirectX::XMMATRIX camera::get_dxm_mvp_matrix() const
{
	// TODO Lab: 3.08 Implement `get_dxm_view_matrix`, `get_dxm_projection_matrix`, and `get_dxm_mvp_matrix` methods of `camera`
	return DirectX::XMMatrixIdentity();
}
#endif

const float4x4 cg::world::camera::get_projection_matrix() const
{
	float f = 1.0f / tan(angle_of_view * 0.5f);
	float range_inv = 1.0f / (z_near - z_far);

	return float4x4{
			{f / aspect_ratio, 0, 0, 0},
			{0, f, 0, 0},
			{0, 0, (z_far + z_near) * range_inv, -1},
			{0, 0, 2.0f * z_far * z_near * range_inv, 0}};
}

const float3 cg::world::camera::get_position() const
{
	return position;
}

const float3 cg::world::camera::get_direction() const
{
	float sin_theta = sin(theta);
	float cos_theta = cos(theta);
	float sin_phi = sin(phi);
	float cos_phi = cos(phi);

	return float3{
			sin_phi * cos_theta,
			sin_theta,
			cos_phi * cos_theta};
}

const float3 cg::world::camera::get_right() const
{
	float3 world_up = float3{0, 1, 0};
	return normalize(cross(get_direction(), world_up));
}

const float3 cg::world::camera::get_up() const
{
	return normalize(cross(get_right(), get_direction()));
}
const float camera::get_theta() const
{
	return theta;
}
const float camera::get_phi() const
{
	return phi;
}
