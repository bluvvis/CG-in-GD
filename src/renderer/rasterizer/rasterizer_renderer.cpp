#include "rasterizer_renderer.h"

#include "utils/resource_utils.h"
#include "utils/timer.h"


void cg::renderer::rasterization_renderer::init()
{
	rasterizer = std::make_shared<
			cg::renderer::rasterizer<cg::vertex, cg::unsigned_color>>();
	rasterizer->set_viewport(settings->width, settings->height);

	render_target = std::make_shared<cg::resource<cg::unsigned_color>>(
			settings->width, settings->height);
	rasterizer->set_render_target(render_target);

	depth_buffer = std::make_shared<cg::resource<float>>(
			settings->width, settings->height);
	rasterizer->set_render_target(render_target, depth_buffer);

	load_model();
	load_camera();
}
void cg::renderer::rasterization_renderer::render()
{
	rasterizer->clear_render_target(
			cg::unsigned_color{
					.r = 56,
					.g = 178,
					.b = 37},
			1.f);

	// Set up vertex shader
	auto mvp_matrix = mul(camera->get_projection_matrix(), mul(camera->get_view_matrix(), model->get_world_matrix()));
	rasterizer->vertex_shader = [mvp_matrix](float4 vertex, cg::vertex vertex_data) {
		float4 transformed_vertex = mul(mvp_matrix, vertex);
		// Perspective divide
		if (transformed_vertex.w != 0.0f) {
			transformed_vertex.x /= transformed_vertex.w;
			transformed_vertex.y /= transformed_vertex.w;
			transformed_vertex.z /= transformed_vertex.w;
		}
		return std::make_pair(transformed_vertex, vertex_data);
	};

	// Set up pixel shader - Normal visualization (creative modification)
	rasterizer->pixel_shader = [](const cg::vertex& vertex_data, const float z) {
		// Convert normal from [-1,1] to [0,1] range for color display
		float3 normal_color = (vertex_data.normal + float3{1.0f, 1.0f, 1.0f}) * 0.5f;

		// Mix normal color with material color for better visual effect
		float3 material_color = vertex_data.diffuse.to_float3();
		float3 final_color = lerp(material_color, normal_color, 0.7f);

		return cg::color{final_color.x, final_color.y, final_color.z};
	};

	// Draw the model
	auto vertex_buffers = model->get_vertex_buffers();
	auto index_buffers = model->get_index_buffers();

	for (size_t shape_id = 0; shape_id < vertex_buffers.size(); shape_id++) {
		rasterizer->set_vertex_buffer(vertex_buffers[shape_id]);
		rasterizer->set_index_buffer(index_buffers[shape_id]);
		rasterizer->draw(index_buffers[shape_id]->count(), 0);
	}

	cg::utils::save_resource(*render_target, settings->result_path);
}

void cg::renderer::rasterization_renderer::destroy() {}

void cg::renderer::rasterization_renderer::update() {}