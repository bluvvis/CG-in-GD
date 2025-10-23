#pragma once

#include "resource.h"

#include <functional>
#include <iostream>
#include <limits>
#include <linalg.h>
#include <memory>


using namespace linalg::aliases;

static constexpr float DEFAULT_DEPTH = std::numeric_limits<float>::max();

namespace cg::renderer
{
	template<typename VB, typename RT>
	class rasterizer
	{
	public:
		rasterizer() {};
		~rasterizer() {};
		void set_render_target(
				std::shared_ptr<resource<RT>> in_render_target,
				std::shared_ptr<resource<float>> in_depth_buffer = nullptr);
		void clear_render_target(
				const RT& in_clear_value, const float in_depth = DEFAULT_DEPTH);

		void set_vertex_buffer(std::shared_ptr<resource<VB>> in_vertex_buffer);
		void set_index_buffer(std::shared_ptr<resource<unsigned int>> in_index_buffer);

		void set_viewport(size_t in_width, size_t in_height);

		void draw(size_t num_vertexes, size_t vertex_offset);

		std::function<std::pair<float4, VB>(float4 vertex, VB vertex_data)> vertex_shader;
		std::function<cg::color(const VB& vertex_data, const float z)> pixel_shader;

	protected:
		std::shared_ptr<cg::resource<VB>> vertex_buffer;
		std::shared_ptr<cg::resource<unsigned int>> index_buffer;
		std::shared_ptr<cg::resource<RT>> render_target;
		std::shared_ptr<cg::resource<float>> depth_buffer;

		size_t width = 1920;
		size_t height = 1080;

		int edge_function(int2 a, int2 b, int2 c);
		bool depth_test(float z, size_t x, size_t y);
	};

	template<typename VB, typename RT>
	inline void rasterizer<VB, RT>::set_render_target(
			std::shared_ptr<resource<RT>> in_render_target,
			std::shared_ptr<resource<float>> in_depth_buffer)
	{
		if (in_render_target) {
			render_target = in_render_target;
		}

		if (in_depth_buffer) {
			depth_buffer = in_depth_buffer;
		}
	}

	template<typename VB, typename RT>
	inline void rasterizer<VB, RT>::set_viewport(size_t in_width, size_t in_height)
	{
		width = in_width;
		height = in_height;
	}

	template<typename VB, typename RT>
	inline void rasterizer<VB, RT>::clear_render_target(
			const RT& in_clear_value, const float in_depth)
	{
		for (size_t i = 0; i < render_target->count(); i++) {
			render_target->item(i) = in_clear_value;
		}

		if (depth_buffer) {
			for (size_t i = 0; i < depth_buffer->count(); i++) {
				depth_buffer->item(i) = in_depth;
			}
		}
	}

	template<typename VB, typename RT>
	inline void rasterizer<VB, RT>::set_vertex_buffer(
			std::shared_ptr<resource<VB>> in_vertex_buffer)
	{
		vertex_buffer = in_vertex_buffer;
	}

	template<typename VB, typename RT>
	inline void rasterizer<VB, RT>::set_index_buffer(
			std::shared_ptr<resource<unsigned int>> in_index_buffer)
	{
		index_buffer = in_index_buffer;
	}

	template<typename VB, typename RT>
	inline void rasterizer<VB, RT>::draw(size_t num_vertexes, size_t vertex_offset)
	{
		if (!vertex_buffer || !index_buffer || !vertex_shader || !pixel_shader) {
			return;
		}

		// Draw triangles
		for (size_t i = vertex_offset; i < vertex_offset + num_vertexes; i += 3) {
			if (i + 2 >= vertex_buffer->count()) break;

			// Get vertices for this triangle
			VB v0 = vertex_buffer->item(index_buffer->item(i));
			VB v1 = vertex_buffer->item(index_buffer->item(i + 1));
			VB v2 = vertex_buffer->item(index_buffer->item(i + 2));

			// Apply vertex shader
			auto [pos0, vs_v0] = vertex_shader(float4{v0.position, 1.0f}, v0);
			auto [pos1, vs_v1] = vertex_shader(float4{v1.position, 1.0f}, v1);
			auto [pos2, vs_v2] = vertex_shader(float4{v2.position, 1.0f}, v2);

			// Convert to screen coordinates
			int2 p0 = int2{
					static_cast<int>((pos0.x + 1.0f) * 0.5f * width),
					static_cast<int>((1.0f - pos0.y) * 0.5f * height)};
			int2 p1 = int2{
					static_cast<int>((pos1.x + 1.0f) * 0.5f * width),
					static_cast<int>((1.0f - pos1.y) * 0.5f * height)};
			int2 p2 = int2{
					static_cast<int>((pos2.x + 1.0f) * 0.5f * width),
					static_cast<int>((1.0f - pos2.y) * 0.5f * height)};

			// Get triangle bounding box
			int min_x = std::max(0, std::min({p0.x, p1.x, p2.x}));
			int max_x = std::min(static_cast<int>(width - 1), std::max({p0.x, p1.x, p2.x}));
			int min_y = std::max(0, std::min({p0.y, p1.y, p2.y}));
			int max_y = std::min(static_cast<int>(height - 1), std::max({p0.y, p1.y, p2.y}));

			// Triangle area for barycentric coordinates
			float area = static_cast<float>(edge_function(p0, p1, p2));
			if (area <= 0) continue;// Skip degenerate or back-facing triangles

			// Rasterize triangle
			for (int y = min_y; y <= max_y; y++) {
				for (int x = min_x; x <= max_x; x++) {
					int2 p = int2{x, y};

					// Calculate barycentric coordinates
					float w0 = static_cast<float>(edge_function(p1, p2, p)) / area;
					float w1 = static_cast<float>(edge_function(p2, p0, p)) / area;
					float w2 = static_cast<float>(edge_function(p0, p1, p)) / area;

					// Check if point is inside triangle
					if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
						// Interpolate depth
						float z = w0 * pos0.z + w1 * pos1.z + w2 * pos2.z;

						// Depth test
						if (depth_test(z, x, y)) {
							// Interpolate vertex data
							VB interpolated_vertex;
							interpolated_vertex.position = w0 * vs_v0.position + w1 * vs_v1.position + w2 * vs_v2.position;
							interpolated_vertex.normal = normalize(w0 * vs_v0.normal + w1 * vs_v1.normal + w2 * vs_v2.normal);
							interpolated_vertex.texcoord = w0 * vs_v0.texcoord + w1 * vs_v1.texcoord + w2 * vs_v2.texcoord;
							interpolated_vertex.diffuse = color{
									w0 * vs_v0.diffuse.r + w1 * vs_v1.diffuse.r + w2 * vs_v2.diffuse.r,
									w0 * vs_v0.diffuse.g + w1 * vs_v1.diffuse.g + w2 * vs_v2.diffuse.g,
									w0 * vs_v0.diffuse.b + w1 * vs_v1.diffuse.b + w2 * vs_v2.diffuse.b};

							// Apply pixel shader
							cg::color pixel_color = pixel_shader(interpolated_vertex, z);

							// Convert to RT format and write to render target
							if constexpr (std::is_same_v<RT, cg::unsigned_color>) {
								RT final_color;
								final_color.r = static_cast<uint8_t>(std::clamp(pixel_color.r * 255.0f, 0.0f, 255.0f));
								final_color.g = static_cast<uint8_t>(std::clamp(pixel_color.g * 255.0f, 0.0f, 255.0f));
								final_color.b = static_cast<uint8_t>(std::clamp(pixel_color.b * 255.0f, 0.0f, 255.0f));
								render_target->item(x, y) = final_color;
							}

							// Update depth buffer
							if (depth_buffer) {
								depth_buffer->item(x, y) = z;
							}
						}
					}
				}
			}
		}
	}

	template<typename VB, typename RT>
	inline int
	rasterizer<VB, RT>::edge_function(int2 a, int2 b, int2 c)
	{
		return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
	}

	template<typename VB, typename RT>
	inline bool rasterizer<VB, RT>::depth_test(float z, size_t x, size_t y)
	{
		if (!depth_buffer) {
			return true;
		}
		return depth_buffer->item(x, y) > z;
	}

}// namespace cg::renderer