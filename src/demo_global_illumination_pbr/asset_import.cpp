#if 0

#define ASSIMP_DLL
#define ASSIMP_API 
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

// may return NULL
static GPU_Texture* LoadMeshTexture(STR_View base_directory, struct aiMaterial* mat, enum aiTextureType type) {
	if (aiGetMaterialTextureCount(mat, type) != 0) {
		struct aiString path;
		uint32_t flags;
		if (aiGetMaterialTexture(mat, type, 0, &path, NULL, NULL, NULL, NULL, NULL, &flags) == AI_SUCCESS) {
			DS_ArenaMark dds_file_mark = DS_ArenaGetMark(TEMP);
			STR_View texture_path = STR_Form(TEMP, "%v/%s", base_directory, path.data);

			STR_View tex_file_data;
			if (!OS_ReadEntireFile(TEMP, texture_path, &tex_file_data)) TODO();

			DDSPP_Descriptor desc = {};
			DDSPP_Result result = ddspp_decode_header((uint8_t*)tex_file_data.data, &desc);
			const void* tex_data = tex_file_data.data + desc.headerSize + ddspp_get_offset(&desc, 0, 0);
			
			GPU_Format format;
			if (desc.format == BC1_UNORM)           format = GPU_Format_BC1_RGBA_UN;
			else if (desc.format == BC3_UNORM)      format = GPU_Format_BC3_RGBA_UN;
			else if (desc.format == R8G8B8A8_UNORM) format = GPU_Format_RGBA8UN;
			else if (desc.format == BC5_UNORM)      format = GPU_Format_BC5_UN;
			else TODO();

			GPU_Texture* texture = GPU_MakeTexture(format, desc.width, desc.height, 1, 0, tex_data);
			DS_ArenaSetMark(TEMP, dds_file_mark);

			//RL_FREE(tex_data);
			return texture;
		}
	}
	return NULL;
}

static bool ReloadMesh(DS_Arena* arena, Scene* s, RenderObject* render_object, STR_View filepath) {
	// free existing data
	{
		GPU_DestroyBuffer(render_object->vertex_buffer);
		GPU_DestroyBuffer(render_object->index_buffer);
		
		for (int i = 0; i < render_object->parts.count; i++) {
			RenderObjectPart* part = &render_object->parts[i];
			GPU_DestroyDescriptorSet(part->descriptor_set);
			GPU_DestroyTexture(part->tex_base_color);
			GPU_DestroyTexture(part->tex_normal);
			GPU_DestroyTexture(part->tex_orm);
			GPU_DestroyTexture(part->tex_emissive);
		}

		*render_object = {};
	}

	// init render object
	DS_ArrInit(&render_object->parts, arena);
	
	DS_ArenaMark T = DS_ArenaGetMark(TEMP);
	
	assert(!STR_ContainsU(filepath, '\\')); // we should use / for path separators
	STR_View base_directory = STR_BeforeLast(filepath, '/');

	char* filepath_cstr = STR_ToC(TEMP, filepath);
	
	// aiProcess_GlobalScale uses the scale settings from the file. It looks like the blender exporter uses it too.
	const struct aiScene* scene = aiImportFile(filepath_cstr, aiProcess_Triangulate|aiProcess_PreTransformVertices|aiProcess_GlobalScale|aiProcess_CalcTangentSpace);
	assert(scene != NULL);

	typedef struct {
		DS_DynArray<Vertex> vertices;
		DS_DynArray<uint32_t> indices;
	} MatMesh;
	
	DS_DynArray<MatMesh> mat_meshes = {TEMP};
	
	MatMesh empty_mat_mesh = {};
	DS_ArrResize(&mat_meshes, empty_mat_mesh, scene->mNumMaterials);
	
	uint32_t total_vertex_count = 0;
	uint32_t total_index_count = 0;

	for (uint32_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; mesh_idx++) {
		aiMesh* mesh = scene->mMeshes[mesh_idx];
		
		MatMesh* mat_mesh = &mat_meshes[mesh->mMaterialIndex];
		DS_ArrInit(&mat_mesh->vertices, TEMP);
		DS_ArrInit(&mat_mesh->indices, TEMP);
		uint32_t first_new_vertex = (uint32_t)mat_mesh->vertices.count;

		for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
			if (mesh->mNormals == NULL) TODO();
			if (mesh->mTextureCoords[0] == NULL) TODO();
			aiVector3D pos = mesh->mVertices[i];
			aiVector3D normal = mesh->mNormals[i];
			aiVector3D tangent = mesh->mTangents[i];
			aiVector3D tex_coord = mesh->mTextureCoords[0][i];
			
			// NOTE: flip Y and Z and texcoord y
			Vertex v;
			v.position  = {pos.x + world_import_offset.X, pos.z * -1.f + world_import_offset.Y, pos.y + world_import_offset.Z};
			v.normal    = {normal.x,  normal.z  * -1.f, normal.y};
			v.tangent   = {tangent.x, tangent.z * -1.f, tangent.y};
			v.tex_coord = {tex_coord.x, 1.f - tex_coord.y};
			DS_ArrPush(&mat_mesh->vertices, v);
		}

		for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
			aiFace face = mesh->mFaces[i];
			if (face.mNumIndices != 3) TODO();

			DS_ArrPush(&mat_mesh->indices, first_new_vertex + face.mIndices[0]);
			DS_ArrPush(&mat_mesh->indices, first_new_vertex + face.mIndices[1]);
			DS_ArrPush(&mat_mesh->indices, first_new_vertex + face.mIndices[2]);
		}
		
		total_vertex_count += mesh->mNumVertices;
		total_index_count += mesh->mNumFaces*3;
	}

	void* merged_vertices = DS_ArenaPush(TEMP, total_vertex_count * sizeof(Vertex));
	void* merged_indices = DS_ArenaPush(TEMP, total_index_count * sizeof(uint32_t));
	
	{
		uint32_t first_vertex = 0;
		uint32_t first_index = 0;
		
		for (int mat_mesh_i = 0; mat_mesh_i < mat_meshes.count; mat_mesh_i++) {
			MatMesh* mat_mesh = &mat_meshes[mat_mesh_i];

			RenderObjectPart part = {};
			part.first_index = first_index;
			part.index_count = (uint32_t)mat_mesh->indices.count;

			memcpy((Vertex*)merged_vertices + first_vertex, mat_mesh->vertices.data, mat_mesh->vertices.count * sizeof(Vertex));

			for (int i = 0; i < mat_mesh->indices.count; i++) {
				((uint32_t*)merged_indices)[first_index] = first_vertex + mat_mesh->indices[i];
				first_index++;
			}
			first_vertex += (uint32_t)mat_mesh->vertices.count;
			
			DS_ArrPush(&render_object->parts, part);
		}

		assert(first_vertex == total_vertex_count);
		assert(first_index == total_index_count);
	}

	render_object->vertex_buffer = GPU_MakeBuffer(total_vertex_count * sizeof(Vertex), GPU_BufferFlag_GPU | GPU_BufferFlag_StorageBuffer, merged_vertices);
	render_object->index_buffer = GPU_MakeBuffer(total_index_count * sizeof(uint32_t), GPU_BufferFlag_GPU | GPU_BufferFlag_StorageBuffer, merged_indices);
	
	for (int i = 0; i < mat_meshes.count; i++) {
		MatMesh* mat_mesh = &mat_meshes[i];
		RenderObjectPart* part = &render_object->parts[i];

		aiMaterial* mat = scene->mMaterials[i];
		part->tex_base_color = LoadMeshTexture(base_directory, mat, aiTextureType_DIFFUSE);
		part->tex_normal     = LoadMeshTexture(base_directory, mat, aiTextureType_NORMALS);
		part->tex_orm        = LoadMeshTexture(base_directory, mat, aiTextureType_SPECULAR);
		part->tex_emissive   = LoadMeshTexture(base_directory, mat, aiTextureType_EMISSIVE);

		MainPassLayout* pass = &s->main_pass_layout;

		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);

		GPU_SetBufferBinding(desc_set, pass->globals_binding, s->globals_buffer);
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
		GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, s->sampler_percentage_closer);

		GPU_SetTextureBinding(desc_set, pass->tex0_binding, part->tex_base_color ? part->tex_base_color : s->dummy_white);
		GPU_SetTextureBinding(desc_set, pass->tex1_binding, part->tex_normal ? part->tex_normal : s->dummy_normal_map);
		GPU_SetTextureBinding(desc_set, pass->tex2_binding, part->tex_orm ? part->tex_orm : s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex3_binding, part->tex_emissive ? part->tex_emissive : s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, s->sun_depth_rt);

		// for lightgrid voxelization
		GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, render_object->vertex_buffer);
		GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, render_object->index_buffer);
		GPU_SetStorageImageBinding(desc_set, pass->img0_binding, s->lightgrid, 0);
		//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, s->lightgrid, 0);

		// ... unused descriptors. This is stupid.
		GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, s->dummy_black);

		GPU_FinalizeDescriptorSet(desc_set);
		part->descriptor_set = desc_set;
	}

	aiReleaseImport(scene);

	assert(arena != TEMP);
	DS_ArenaSetMark(TEMP, T);
	return true;
}

#endif