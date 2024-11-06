
RenderObject LoadMesh(Renderer* renderer, STR_View filepath); // for simplicity, asserts that the mesh is valid
void UnloadMesh(RenderObject* mesh);

GPU_Texture* MakeTextureFromHDRIFile(STR_View filepath); // for simplicity, asserts that the texture is valid

