#define TINYOBJLOADER_IMPLEMENTATION

#include "model.h"

#include "utils/error_handler.h"

#include <linalg.h>


using namespace linalg::aliases;
using namespace cg::world;

cg::world::model::model() {}

cg::world::model::~model() {}

void cg::world::model::load_obj(const std::filesystem::path& model_path)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	std::filesystem::path base_folder = model_path.parent_path();
	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
								model_path.string().c_str(), base_folder.string().c_str());

	if (!warn.empty()) {
		THROW_ERROR(warn);
	}

	if (!err.empty()) {
		THROW_ERROR(err);
	}

	if (!ret) {
		THROW_ERROR("Failed to load model");
	}

	allocate_buffers(shapes);
	fill_buffers(shapes, attrib, materials, base_folder);
}

void model::allocate_buffers(const std::vector<tinyobj::shape_t>& shapes)
{
	vertex_buffers.resize(shapes.size());
	index_buffers.resize(shapes.size());

	for (size_t s = 0; s < shapes.size(); s++) {
		size_t index_count = shapes[s].mesh.indices.size();
		size_t vertex_count = index_count;// Each index maps to a unique vertex in our case

		vertex_buffers[s] = std::make_shared<cg::resource<cg::vertex>>(vertex_count);
		index_buffers[s] = std::make_shared<cg::resource<unsigned int>>(index_count);
	}
}

float3 cg::world::model::compute_normal(const tinyobj::attrib_t& attrib, const tinyobj::mesh_t& mesh, size_t index_offset)
{
	auto a_id = mesh.indices[index_offset];
	auto b_id = mesh.indices[index_offset + 1];
	auto c_id = mesh.indices[index_offset + 2];

	float3 a{
			attrib.vertices[3 * a_id.vertex_index],
			attrib.vertices[3 * a_id.vertex_index + 1],
			attrib.vertices[3 * a_id.vertex_index + 2]};

	float3 b{
			attrib.vertices[3 * b_id.vertex_index],
			attrib.vertices[3 * b_id.vertex_index + 1],
			attrib.vertices[3 * b_id.vertex_index + 2]};

	float3 c{
			attrib.vertices[3 * c_id.vertex_index],
			attrib.vertices[3 * c_id.vertex_index + 1],
			attrib.vertices[3 * c_id.vertex_index + 2]};

	return normalize(cross(b - a, c - a));
}

void model::fill_vertex_data(cg::vertex& vertex, const tinyobj::attrib_t& attrib, const tinyobj::index_t idx, const float3 computed_normal, const tinyobj::material_t material)
{
	// Fill position
	vertex.position = float3{
			attrib.vertices[3 * idx.vertex_index],
			attrib.vertices[3 * idx.vertex_index + 1],
			attrib.vertices[3 * idx.vertex_index + 2]};

	// Fill normal
	if (idx.normal_index >= 0) {
		vertex.normal = float3{
				attrib.normals[3 * idx.normal_index],
				attrib.normals[3 * idx.normal_index + 1],
				attrib.normals[3 * idx.normal_index + 2]};
	}
	else {
		vertex.normal = computed_normal;
	}

	// Fill texture coordinates
	if (idx.texcoord_index >= 0) {
		vertex.texcoord = float2{
				attrib.texcoords[2 * idx.texcoord_index],
				attrib.texcoords[2 * idx.texcoord_index + 1]};
	}
	else {
		vertex.texcoord = float2{0.0f, 0.0f};
	}

	// Fill material colors
	vertex.diffuse = color{material.diffuse[0], material.diffuse[1], material.diffuse[2]};
	vertex.ambient = color{material.ambient[0], material.ambient[1], material.ambient[2]};
	vertex.emissive = color{material.emission[0], material.emission[1], material.emission[2]};
}

void model::fill_buffers(const std::vector<tinyobj::shape_t>& shapes, const tinyobj::attrib_t& attrib, const std::vector<tinyobj::material_t>& materials, const std::filesystem::path& base_folder)
{
	for (size_t s = 0; s < shapes.size(); s++) {
		size_t index_offset = 0;
		auto& mesh = shapes[s].mesh;

		for (size_t f = 0; f < mesh.num_face_vertices.size(); f++) {
			int fv = mesh.num_face_vertices[f];

			for (size_t v = 0; v < fv; v++) {
				auto idx = mesh.indices[index_offset + v];
				auto material_id = mesh.material_ids[f];

				tinyobj::material_t material;
				if (material_id >= 0 && material_id < materials.size()) {
					material = materials[material_id];
				}

				float3 computed_normal;
				if (fv == 3) {// Triangle
					computed_normal = compute_normal(attrib, mesh, index_offset);
				}

				cg::vertex vertex;
				fill_vertex_data(vertex, attrib, idx, computed_normal, material);

				vertex_buffers[s]->item(index_offset + v) = vertex;
				index_buffers[s]->item(index_offset + v) = static_cast<unsigned int>(index_offset + v);
			}

			index_offset += fv;
		}

		// Store texture files if available
		for (auto& material: materials) {
			if (!material.diffuse_texname.empty()) {
				auto texture_path = base_folder / material.diffuse_texname;
				if (std::find(textures.begin(), textures.end(), texture_path) == textures.end()) {
					textures.push_back(texture_path);
				}
			}
		}
	}
}


const std::vector<std::shared_ptr<cg::resource<cg::vertex>>>&
cg::world::model::get_vertex_buffers() const
{
	return vertex_buffers;
}

const std::vector<std::shared_ptr<cg::resource<unsigned int>>>&
cg::world::model::get_index_buffers() const
{
	return index_buffers;
}

const std::vector<std::filesystem::path>& cg::world::model::get_per_shape_texture_files() const
{
	return textures;
}


const float4x4 cg::world::model::get_world_matrix() const
{
	return float4x4{
			{1, 0, 0, 0},
			{0, 1, 0, 0},
			{0, 0, 1, 0},
			{0, 0, 0, 1}};
}
