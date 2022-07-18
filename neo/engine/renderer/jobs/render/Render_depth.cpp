// Render_depth.cpp
//

#include "../../RenderSystem_local.h"

/*
==================
idRender::DepthBufferPass
==================
*/
void idRender::DepthBufferPass(const drawSurf_t* surf) {
	int			stage;
	const idMaterial* shader;
	const shaderStage_t* pStage;
	const float* regs;
	float		color[4];
	const srfTriangles_t* tri;

	tri = surf->geo;
	shader = surf->material;

	if (!shader->ShouldRenderZPrepass()) {
		return;
	}

	if (surf->forceTwoSided)
	{
		GL_Cull(CT_TWO_SIDED);
	}
	else
	{
		GL_Cull(CT_FRONT_SIDED);
	}

	// update the clip plane if needed
	if (backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace) {
		GL_SelectTexture(1);

		idPlane	plane;

		R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane);
		plane[3] += 0.5;	// the notch is in the middle
		glTexGenfv(GL_S, GL_OBJECT_PLANE, plane.ToFloatPtr());
		GL_SelectTexture(0);
	}

	if (!shader->IsDrawn()) {
		return;
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if (!tri->numIndexes) {
		return;
	}

	// translucent surfaces don't put anything in the depth buffer and don't
	// test against it, which makes them fail the mirror clip plane operation
	if (shader->Coverage() == MC_TRANSLUCENT) {
		return;
	}

	if (!tri->ambientCache) {
		common->Printf("RB_T_FillDepthBuffer: !tri->ambientCache\n");
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// if all stages of a material have been conditioned off, don't do anything
	for (stage = 0; stage < shader->GetNumStages(); stage++) {
		pStage = shader->GetStage(stage);
		// check the stage enable condition
		if (regs[pStage->conditionRegister] != 0) {
			break;
		}
	}
	if (stage == shader->GetNumStages()) {
		return;
	}

	// set polygon offset if necessary
	if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
	}

	// subviews will just down-modulate the color buffer by overbright
	if (shader->GetSort() == SS_SUBVIEW) {
		GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS);
		color[0] =
			color[1] =
			color[2] = (1.0 / backEnd.overBright);
		color[3] = 1;
	}
	else {
		// others just draw black
		color[0] = 0;
		color[1] = 0;
		color[2] = 0;
		color[3] = 1;
	}

	idDrawVert* ac = (idDrawVert*)vertexCache.Position(tri->ambientCache);
	glVertexPointer(3, GL_FLOAT, sizeof(idDrawVert), ac->xyz.ToFloatPtr());
	glTexCoordPointer(2, GL_FLOAT, sizeof(idDrawVert), reinterpret_cast<void*>(&ac->st));

	pStage = shader->GetAlbedoStage();

	if (pStage)
	{
		tr.albedoTextureParam->SetImage(shader->GetEditorImage());
		PrepareStageTexturing(pStage, surf, ac);
		tr.occluderProgram[PROG_VARIANT_NONSKINNED]->Bind();
	}

	// draw it
	if (!surf->hideInMainView)
	{
		RB_DrawElementsWithCounters(tri);
	}

	if (pStage)
	{
		tr.occluderProgram[PROG_VARIANT_NONSKINNED]->BindNull();
		FinishStageTexturing(pStage, surf, ac);
	}

	// reset polygon offset
	if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	// reset blending
	if (shader->GetSort() == SS_SUBVIEW) {
		GL_State(GLS_DEPTHFUNC_LESS);
	}

}


/*
=====================
idRender::FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void idRender::FillDepthBuffer(drawSurf_t** drawSurfs, int numDrawSurfs) {
	// if we are just doing 2D rendering, no need to fill the depth buffer
	if (!backEnd.viewDef->viewEntitys) {
		return;
	}

	RB_LogComment("---------- RB_STD_FillDepthBuffer ----------\n");

	// enable the second texture for mirror plane clipping if needed
	if (backEnd.viewDef->numClipPlanes) {
		GL_SelectTexture(1);
		globalImages->alphaNotchImage->Bind();
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnable(GL_TEXTURE_GEN_S);
		glTexCoord2f(1, 0.5);
	}

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture(0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	// decal surfaces may enable polygon offset
	glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat());

	GL_State(GLS_DEPTHFUNC_LESS);

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 1, 255);

	RB_RenderDrawSurfListWithFunction(drawSurfs, numDrawSurfs, idRender::DepthBufferPass);

	if (backEnd.viewDef->numClipPlanes) {
		GL_SelectTexture(1);
		globalImages->BindNull();
		glDisable(GL_TEXTURE_GEN_S);
		GL_SelectTexture(0);
	}

}