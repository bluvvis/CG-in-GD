#include "raytracer_renderer.h"

#include "utils/resource_utils.h"
#include "utils/timer.h"

#include <iostream>
#include <random>

static size_t accumulation_num = 0;

void cg::renderer::ray_tracing_renderer::init()
{
	raytracer = std::make_shared<cg::renderer::raytracer<cg::vertex, cg::unsigned_color>>();
	shadow_raytracer = std::make_shared<cg::renderer::raytracer<cg::vertex, cg::unsigned_color>>();
	
	raytracer->set_viewport(settings->width, settings->height);
	shadow_raytracer->set_viewport(settings->width, settings->height);
	
	render_target = std::make_shared<cg::resource<cg::unsigned_color>>(
			settings->width, settings->height);
	raytracer->set_render_target(render_target);
	
	load_model();
	load_camera();
	
	auto vertex_buffers = model->get_vertex_buffers();
	auto index_buffers = model->get_index_buffers();
	
	raytracer->set_vertex_buffers(vertex_buffers);
	raytracer->set_index_buffers(index_buffers);
	shadow_raytracer->set_vertex_buffers(vertex_buffers);
	shadow_raytracer->set_index_buffers(index_buffers);
	
	raytracer->build_acceleration_structure();
	shadow_raytracer->build_acceleration_structure();
	
	lights.push_back(light{
		float3{0.f, 1.5f, 0.f},
		float3{1.f, 1.f, 1.f}
	});
	
	shadow_raytracer->miss_shader = [](const ray& r) {
		payload p;
		p.t = -1.f;
		p.color = cg::color{1.f, 1.f, 1.f};
		return p;
	};
	
	shadow_raytracer->any_hit_shader = [](const ray& r, payload& p, const triangle<cg::vertex>& tri) {
		payload result;
		result.t = -1.f;
		return result;
	};
}

void cg::renderer::ray_tracing_renderer::destroy() {}

void cg::renderer::ray_tracing_renderer::update() {}

void cg::renderer::ray_tracing_renderer::render()
{
	raytracer->clear_render_target(cg::unsigned_color{0, 0, 0});
	
	raytracer->miss_shader = [](const ray& r) {
		payload p;
		p.t = -1.f;
		float3 dir = normalize(r.direction);
		float t = (dir.y + 1.f) * 0.5f;
		float3 color = lerp(float3{0.5f, 0.5f, 0.5f}, float3{0.5f, 0.7f, 1.0f}, t);
		p.color = cg::color{color.x, color.y, color.z};
		return p;
	};
	
	raytracer->closest_hit_shader = [this](const ray& r, payload& p, const triangle<cg::vertex>& tri, size_t depth) {
		float3 normal = normalize(
			tri.na * p.bary.x +
			tri.nb * p.bary.y +
			tri.nc * p.bary.z
		);
		
		float3 hit_point = r.position + r.direction * p.t;
		float3 color = tri.ambient;
		
		static std::mt19937 gen(12345);
		static std::uniform_real_distribution<float> dis(0.f, 1.f);
		
		const int num_samples = 4;
		for (const auto& light : lights) {
			float3 total_light = float3{0.f, 0.f, 0.f};
			
			for (int i = 0; i < num_samples; i++) {
				float3 light_offset = float3{
					dis(gen) - 0.5f,
					dis(gen) - 0.5f,
					dis(gen) - 0.5f
				} * 0.2f;
				
				float3 light_pos = light.position + light_offset;
				float3 light_dir = normalize(light_pos - hit_point);
				float ndotl = std::max(0.f, dot(normal, light_dir));
				
				ray shadow_ray(hit_point + normal * 0.001f, light_dir);
				payload shadow_payload = shadow_raytracer->trace_ray(shadow_ray, 1, length(light_pos - hit_point), 0.001f);
				
				if (shadow_payload.t < 0.f) {
					float3 lambertian = tri.diffuse * light.color * ndotl;
					total_light = total_light + lambertian;
				}
			}
			
			color = color + total_light / static_cast<float>(num_samples);
		}
		
		color = color + tri.emissive;
		
		p.color = cg::color{color.x, color.y, color.z};
		return p;
	};
	
	float3 position = camera->get_position();
	float3 direction = camera->get_direction();
	float3 right = camera->get_right();
	float3 up = camera->get_up();
	
	raytracer->ray_generation(position, direction, right, up, 1, accumulation_num);
	accumulation_num++;
	
	cg::utils::save_resource(*render_target, settings->result_path);
}