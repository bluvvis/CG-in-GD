#pragma once

#include "resource.h"

#include <cmath>
#include <iostream>
#include <linalg.h>
#include <memory>
#include <omp.h>
#include <random>

using namespace linalg::aliases;

namespace cg::renderer
{
	struct ray
	{
		ray(float3 position, float3 direction) : position(position)
		{
			this->direction = normalize(direction);
		}
		float3 position;
		float3 direction;
	};

	struct payload
	{
		float t;
		float3 bary;
		cg::color color;
	};

	template<typename VB>
	struct triangle
	{
		triangle(const VB& vertex_a, const VB& vertex_b, const VB& vertex_c);

		float3 a;
		float3 b;
		float3 c;

		float3 ba;
		float3 ca;

		float3 na;
		float3 nb;
		float3 nc;

		float3 ambient;
		float3 diffuse;
		float3 emissive;
	};

	template<typename VB>
	inline triangle<VB>::triangle(
			const VB& vertex_a, const VB& vertex_b, const VB& vertex_c)
	{
		a = vertex_a.position;
		b = vertex_b.position;
		c = vertex_c.position;
		ba = b - a;
		ca = c - a;
		na = vertex_a.normal;
		nb = vertex_b.normal;
		nc = vertex_c.normal;
		ambient = vertex_a.ambient.to_float3();
		diffuse = vertex_a.diffuse.to_float3();
		emissive = vertex_a.emissive.to_float3();
	}

	template<typename VB>
	class aabb
	{
	public:
		void add_triangle(const triangle<VB> triangle);
		const std::vector<triangle<VB>>& get_triangles() const;
		bool aabb_test(const ray& ray) const;

	protected:
		std::vector<triangle<VB>> triangles;

		float3 aabb_min;
		float3 aabb_max;
	};

	struct light
	{
		float3 position;
		float3 color;
	};

	template<typename VB, typename RT>
	class raytracer
	{
	public:
		raytracer(){};
		~raytracer(){};

		void set_render_target(std::shared_ptr<resource<RT>> in_render_target);
		void clear_render_target(const RT& in_clear_value);
		void set_viewport(size_t in_width, size_t in_height);

		void set_vertex_buffers(std::vector<std::shared_ptr<cg::resource<VB>>> in_vertex_buffers);
		void set_index_buffers(std::vector<std::shared_ptr<cg::resource<unsigned int>>> in_index_buffers);
		void build_acceleration_structure();
		std::vector<aabb<VB>> acceleration_structures;

		void ray_generation(float3 position, float3 direction, float3 right, float3 up, size_t depth, size_t accumulation_num);

		payload trace_ray(const ray& ray, size_t depth, float max_t = 1000.f, float min_t = 0.001f) const;
		payload intersection_shader(const triangle<VB>& triangle, const ray& ray) const;

		std::function<payload(const ray& ray)> miss_shader = nullptr;
		std::function<payload(const ray& ray, payload& payload, const triangle<VB>& triangle, size_t depth)>
				closest_hit_shader = nullptr;
		std::function<payload(const ray& ray, payload& payload, const triangle<VB>& triangle)> any_hit_shader =
				nullptr;

		float2 get_jitter(int frame_id);

	protected:
		std::shared_ptr<cg::resource<RT>> render_target;
		std::shared_ptr<cg::resource<float3>> history;
		std::vector<std::shared_ptr<cg::resource<unsigned int>>> index_buffers;
		std::vector<std::shared_ptr<cg::resource<VB>>> vertex_buffers;
		std::vector<triangle<VB>> triangles;

		size_t width = 1920;
		size_t height = 1080;
	};

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::set_render_target(
			std::shared_ptr<resource<RT>> in_render_target)
	{
		render_target = in_render_target;
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::set_viewport(size_t in_width,
												size_t in_height)
	{
		width = in_width;
		height = in_height;
		history = std::make_shared<cg::resource<float3>>(width, height);
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::clear_render_target(
			const RT& in_clear_value)
	{
		if (!render_target) return;
		for (size_t y = 0; y < height; y++) {
			for (size_t x = 0; x < width; x++) {
				render_target->item(x, y) = in_clear_value;
			}
		}
		if (history) {
			for (size_t y = 0; y < height; y++) {
				for (size_t x = 0; x < width; x++) {
					history->item(x, y) = float3{0.f, 0.f, 0.f};
				}
			}
		}
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::set_vertex_buffers(std::vector<std::shared_ptr<cg::resource<VB>>> in_vertex_buffers)
	{
		vertex_buffers = in_vertex_buffers;
	}

	template<typename VB, typename RT>
	void raytracer<VB, RT>::set_index_buffers(std::vector<std::shared_ptr<cg::resource<unsigned int>>> in_index_buffers)
	{
		index_buffers = in_index_buffers;
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::build_acceleration_structure()
	{
		triangles.clear();
		acceleration_structures.clear();
		
		for (size_t buffer_id = 0; buffer_id < vertex_buffers.size(); buffer_id++) {
			if (buffer_id >= index_buffers.size()) continue;
			
			auto& vertex_buffer = vertex_buffers[buffer_id];
			auto& index_buffer = index_buffers[buffer_id];
			
			for (size_t i = 0; i < index_buffer->count(); i += 3) {
				if (i + 2 >= index_buffer->count()) break;
				
				unsigned int idx0 = index_buffer->item(i);
				unsigned int idx1 = index_buffer->item(i + 1);
				unsigned int idx2 = index_buffer->item(i + 2);
				
				VB v0 = vertex_buffer->item(idx0);
				VB v1 = vertex_buffer->item(idx1);
				VB v2 = vertex_buffer->item(idx2);
				
				triangle<VB> tri(v0, v1, v2);
				triangles.push_back(tri);
			}
		}
		
		if (triangles.empty()) return;
		
		aabb<VB> root_aabb;
		for (const auto& tri : triangles) {
			root_aabb.add_triangle(tri);
		}
		acceleration_structures.push_back(root_aabb);
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::ray_generation(
			float3 position, float3 direction,
			float3 right, float3 up, size_t depth, size_t accumulation_num)
	{
		if (!render_target) return;
		
		float aspect = static_cast<float>(width) / static_cast<float>(height);
		float fov = 1.0f;
		
		#pragma omp parallel for
		for (size_t y = 0; y < height; y++) {
			for (size_t x = 0; x < width; x++) {
				float2 jitter = get_jitter(static_cast<int>(accumulation_num));
				float u = (static_cast<float>(x) + 0.5f + jitter.x) / static_cast<float>(width);
				float v = (static_cast<float>(y) + 0.5f + jitter.y) / static_cast<float>(height);
				
				u = u * 2.0f - 1.0f;
				v = 1.0f - v * 2.0f;
				
				u *= aspect * fov;
				v *= fov;
				
				float3 ray_dir = normalize(direction + right * u + up * v);
				ray r(position, ray_dir);
				
				payload p = trace_ray(r, depth);
				
				float3 color = p.color.to_float3();
				
				if (history && accumulation_num > 0) {
					float3 prev_color = history->item(x, y);
					float alpha = 1.0f / (accumulation_num + 1.0f);
					color = lerp(prev_color, color, alpha);
				}
				
				if (history) {
					history->item(x, y) = color;
				}
				
				if constexpr (std::is_same_v<RT, cg::unsigned_color>) {
					render_target->item(x, y) = RT::from_float3(color);
				}
			}
		}
	}

	template<typename VB, typename RT>
	inline payload raytracer<VB, RT>::trace_ray(
			const ray& ray, size_t depth, float max_t, float min_t) const
	{
		if (depth == 0) {
			if (miss_shader) {
				return miss_shader(ray);
			}
			payload p;
			p.t = -1.f;
			p.color = cg::color{0.f, 0.f, 0.f};
			return p;
		}
		
		payload closest;
		closest.t = max_t;
		const triangle<VB>* closest_triangle = nullptr;
		
		if (!acceleration_structures.empty()) {
			for (const auto& aabb : acceleration_structures) {
				if (!aabb.aabb_test(ray)) continue;
				
				for (const auto& tri : aabb.get_triangles()) {
					payload p = intersection_shader(tri, ray);
					
					if (p.t > min_t && p.t < closest.t) {
						if (any_hit_shader) {
							payload any_p = any_hit_shader(ray, p, tri);
							if (any_p.t < 0.f) continue;
						}
						closest = p;
						closest_triangle = &tri;
					}
				}
			}
		} else {
			for (const auto& tri : triangles) {
				payload p = intersection_shader(tri, ray);
				
				if (p.t > min_t && p.t < closest.t) {
					if (any_hit_shader) {
						payload any_p = any_hit_shader(ray, p, tri);
						if (any_p.t < 0.f) continue;
					}
					closest = p;
					closest_triangle = &tri;
				}
			}
		}
		
		if (closest_triangle && closest_hit_shader) {
			return closest_hit_shader(ray, closest, *closest_triangle, depth);
		}
		
		if (closest.t < max_t) {
			return closest;
		}
		
		if (miss_shader) {
			return miss_shader(ray);
		}
		
		payload p;
		p.t = -1.f;
		p.color = cg::color{0.f, 0.f, 0.f};
		return p;
	}

	template<typename VB, typename RT>
	inline payload raytracer<VB, RT>::intersection_shader(
			const triangle<VB>& triangle, const ray& ray) const
	{
		payload result;
		float3 pvec = cross(ray.direction, triangle.ca);
		float det = dot(triangle.ba, pvec);
		
		if (std::abs(det) < 1e-6f) {
			result.t = -1.f;
			return result;
		}
		
		float inv_det = 1.f / det;
		float3 tvec = ray.position - triangle.a;
		float u = dot(tvec, pvec) * inv_det;
		
		if (u < 0.f || u > 1.f) {
			result.t = -1.f;
			return result;
		}
		
		float3 qvec = cross(tvec, triangle.ba);
		float v = dot(ray.direction, qvec) * inv_det;
		
		if (v < 0.f || u + v > 1.f) {
			result.t = -1.f;
			return result;
		}
		
		float t = dot(triangle.ca, qvec) * inv_det;
		
		if (t < 0.f) {
			result.t = -1.f;
			return result;
		}
		
		result.t = t;
		result.bary = float3{1.f - u - v, u, v};
		return result;
	}

	template<typename VB, typename RT>
	float2 raytracer<VB, RT>::get_jitter(int frame_id)
	{
		static std::mt19937 gen(42);
		static std::uniform_real_distribution<float> dis(-0.5f, 0.5f);
		
		float2 jitter;
		jitter.x = dis(gen);
		jitter.y = dis(gen);
		
		return jitter;
	}


	template<typename VB>
	inline void aabb<VB>::add_triangle(const triangle<VB> triangle)
	{
		triangles.push_back(triangle);
		
		if (triangles.size() == 1) {
			aabb_min = float3{
				std::min({triangle.a.x, triangle.b.x, triangle.c.x}),
				std::min({triangle.a.y, triangle.b.y, triangle.c.y}),
				std::min({triangle.a.z, triangle.b.z, triangle.c.z})
			};
			aabb_max = float3{
				std::max({triangle.a.x, triangle.b.x, triangle.c.x}),
				std::max({triangle.a.y, triangle.b.y, triangle.c.y}),
				std::max({triangle.a.z, triangle.b.z, triangle.c.z})
			};
		} else {
			aabb_min = float3{
				std::min(aabb_min.x, std::min({triangle.a.x, triangle.b.x, triangle.c.x})),
				std::min(aabb_min.y, std::min({triangle.a.y, triangle.b.y, triangle.c.y})),
				std::min(aabb_min.z, std::min({triangle.a.z, triangle.b.z, triangle.c.z}))
			};
			aabb_max = float3{
				std::max(aabb_max.x, std::max({triangle.a.x, triangle.b.x, triangle.c.x})),
				std::max(aabb_max.y, std::max({triangle.a.y, triangle.b.y, triangle.c.y})),
				std::max(aabb_max.z, std::max({triangle.a.z, triangle.b.z, triangle.c.z}))
			};
		}
	}

	template<typename VB>
	inline const std::vector<triangle<VB>>& aabb<VB>::get_triangles() const
	{
		return triangles;
	}

	template<typename VB>
	inline bool aabb<VB>::aabb_test(const ray& ray) const
	{
		float3 inv_dir = float3{
			ray.direction.x != 0.f ? 1.f / ray.direction.x : 1e30f,
			ray.direction.y != 0.f ? 1.f / ray.direction.y : 1e30f,
			ray.direction.z != 0.f ? 1.f / ray.direction.z : 1e30f
		};
		float3 t0 = (aabb_min - ray.position) * inv_dir;
		float3 t1 = (aabb_max - ray.position) * inv_dir;
		
		float3 tmin = float3{
			std::min(t0.x, t1.x),
			std::min(t0.y, t1.y),
			std::min(t0.z, t1.z)
		};
		float3 tmax = float3{
			std::max(t0.x, t1.x),
			std::max(t0.y, t1.y),
			std::max(t0.z, t1.z)
		};
		
		float t_min = std::max({tmin.x, tmin.y, tmin.z});
		float t_max = std::min({tmax.x, tmax.y, tmax.z});
		
		return t_max >= t_min && t_max >= 0.f;
	}

}// namespace cg::renderer