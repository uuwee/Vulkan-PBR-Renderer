
RenderObject LoadMesh(Renderer* renderer, STR_View filepath, HMM_Vec3 offset); // asserts that the mesh is valid
void UnloadMesh(RenderObject* mesh);

GPU_Texture* MakeTextureFromHDRIFile(STR_View filepath); // asserts that the texture is valid
