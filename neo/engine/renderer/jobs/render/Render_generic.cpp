// Render_generic.cpp
//

#include "../../RenderSystem_local.h"


/*
==================
RB_STD_T_RenderShaderPasses

This is also called for the generated 2D rendering
==================
*/
void RB_STD_T_RenderShaderPasses(const drawSurf_t* surf) {
	int			stage;
	const idMaterial* shader;
	const shaderStage_t* pStage;
	const float* regs;
	float		color[4];
	const srfTriangles_t* tri;

	tri = surf->geo;
	shader = surf->material;

	if (!shader->HasAmbient()) {
		return;
	}

	if (shader->IsPortalSky()) {
		return;
	}

	// change the matrix if needed
	if (surf->space != backEnd.currentSpace) {
		glLoadMatrixf(surf->space->modelViewMatrix);
		backEnd.currentSpace = surf->space;
	}

	// change the scissor if needed
	if (r_useScissor.GetBool() && !backEnd.currentScissor.Equals(surf->scissorRect)) {
		backEnd.currentScissor = surf->scissorRect;
		glScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if (!tri->numIndexes) {
		return;
	}

	if (!tri->ambientCache) {
		common->Printf("RB_T_RenderShaderPasses: !tri->ambientCache\n");
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// set face culling appropriately
	GL_Cull(shader->GetCullType());

	// set polygon offset if necessary
	if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
	}

	if (surf->space->weaponDepthHack) {
		RB_EnterWeaponDepthHack();
	}

	if (surf->space->modelDepthHack != 0.0f) {
		RB_EnterModelDepthHack(surf->space->modelDepthHack);
	}

	idDrawVert* ac = (idDrawVert*)vertexCache.Position(tri->ambientCache);
	glVertexPointer(3, GL_FLOAT, sizeof(idDrawVert), ac->xyz.ToFloatPtr());
	glTexCoordPointer(2, GL_FLOAT, sizeof(idDrawVert), reinterpret_cast<void*>(&ac->st));

	for (stage = 0; stage < shader->GetNumStages(); stage++) {
		pStage = shader->GetStage(stage);

		// check the enable condition
		if (regs[pStage->conditionRegister] == 0) {
			continue;
		}

		// skip the stages involved in lighting
		if (pStage->lighting != SL_AMBIENT) {
			continue;
		}

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE)) {
			continue;
		}

		// see if we are a new-style stage
		newShaderStage_t* newStage = pStage->newStage;
		if (newStage) {
			//--------------------------
			//
			// new style stages
			//
			//--------------------------

			// completely skip the stage if we don't have the capability
			if (tr.backEndRenderer != BE_ARB2) {
				continue;
			}
			if (r_skipNewAmbient.GetBool()) {
				continue;
			}
			glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(idDrawVert), (void*)&ac->color);
			glVertexAttribPointerARB(9, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[0].ToFloatPtr());
			glVertexAttribPointerARB(10, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[1].ToFloatPtr());
			glNormalPointer(GL_FLOAT, sizeof(idDrawVert), ac->normal.ToFloatPtr());

			glEnableClientState(GL_COLOR_ARRAY);
			glEnableVertexAttribArrayARB(9);
			glEnableVertexAttribArrayARB(10);
			glEnableClientState(GL_NORMAL_ARRAY);

			GL_State(pStage->drawStateBits);

			glBindProgramARB(GL_VERTEX_PROGRAM_ARB, newStage->vertexProgram);
			glEnable(GL_VERTEX_PROGRAM_ARB);

			for (int i = 0; i < newStage->numVertexParms; i++) {
				float	parm[4];
				parm[0] = regs[newStage->vertexParms[i][0]];
				parm[1] = regs[newStage->vertexParms[i][1]];
				parm[2] = regs[newStage->vertexParms[i][2]];
				parm[3] = regs[newStage->vertexParms[i][3]];
				glProgramLocalParameter4fvARB(GL_VERTEX_PROGRAM_ARB, i, parm);
			}

			for (int i = 0; i < newStage->numFragmentProgramImages; i++) {
				if (newStage->fragmentProgramImages[i]) {
					GL_SelectTexture(i);
					newStage->fragmentProgramImages[i]->Bind();
				}
			}
			glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram);
			glEnable(GL_FRAGMENT_PROGRAM_ARB);

			// draw it
			RB_DrawElementsWithCounters(tri);

			for (int i = 1; i < newStage->numFragmentProgramImages; i++) {
				if (newStage->fragmentProgramImages[i]) {
					GL_SelectTexture(i);
					globalImages->BindNull();
				}
			}

			GL_SelectTexture(0);

			glDisable(GL_VERTEX_PROGRAM_ARB);
			glDisable(GL_FRAGMENT_PROGRAM_ARB);
			// Fixme: Hack to get around an apparent bug in ATI drivers.  Should remove as soon as it gets fixed.
			glBindProgramARB(GL_VERTEX_PROGRAM_ARB, 0);

			glDisableClientState(GL_COLOR_ARRAY);
			glDisableVertexAttribArrayARB(9);
			glDisableVertexAttribArrayARB(10);
			glDisableClientState(GL_NORMAL_ARRAY);
			continue;
		}


		//--------------------------
		//
		// old style stages
		//
		//--------------------------

		// set the color
		color[0] = regs[pStage->color.registers[0]];
		color[1] = regs[pStage->color.registers[1]];
		color[2] = regs[pStage->color.registers[2]];
		color[3] = regs[pStage->color.registers[3]];

		// skip the entire stage if an add would be black
		if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)
			&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
			continue;
		}

		// skip the entire stage if a blend would be completely transparent
		if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
			&& color[3] <= 0) {
			continue;
		}

		// select the vertex color source
		if (pStage->vertexColor == SVC_IGNORE) {
			glColor4fv(color);
		}
		else {
			glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(idDrawVert), (void*)&ac->color);
			glEnableClientState(GL_COLOR_ARRAY);

			if (pStage->vertexColor == SVC_INVERSE_MODULATE) {
				GL_TexEnv(GL_COMBINE_ARB);
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_ONE_MINUS_SRC_COLOR);
				glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1);
			}

			// for vertex color and modulated color, we need to enable a second
			// texture stage
			if (color[0] != 1 || color[1] != 1 || color[2] != 1 || color[3] != 1) {
				GL_SelectTexture(1);

				globalImages->whiteImage->Bind();
				GL_TexEnv(GL_COMBINE_ARB);

				glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);

				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
				glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1);

				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_CONSTANT_ARB);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA);
				glTexEnvi(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1);

				GL_SelectTexture(0);
			}
		}

		// bind the texture
		RB_BindVariableStageImage(&pStage->texture, regs);

		// set the state
		GL_State(pStage->drawStateBits);

		//RB_PrepareStageTexturing(pStage, surf, ac);

		// draw it
		tr.guiTextureProgram->Bind();
		RB_DrawElementsWithCounters(tri);
		tr.guiTextureProgram->BindNull();

		//RB_FinishStageTexturing(pStage, surf, ac);

		if (pStage->vertexColor != SVC_IGNORE) {
			glDisableClientState(GL_COLOR_ARRAY);

			GL_SelectTexture(1);
			GL_TexEnv(GL_MODULATE);
			globalImages->BindNull();
			GL_SelectTexture(0);
			GL_TexEnv(GL_MODULATE);
		}
	}

	// reset polygon offset
	if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	if (surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f) {
		RB_LeaveDepthHack();
	}
}

/*
=====================
idRender::DrawShaderPasses

Draw non-light dependent passes
=====================
*/
int idRender::DrawShaderPasses(drawSurf_t** drawSurfs, int numDrawSurfs) {
	int				i;

	// only obey skipAmbient if we are rendering a view
	if (backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool()) {
		return numDrawSurfs;
	}

	RB_LogComment("---------- RB_STD_DrawShaderPasses ----------\n");

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if (drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS) {
		if (r_skipPostProcess.GetBool()) {
			return 0;
		}

		// only dump if in a 3d view
		if (backEnd.viewDef->viewEntitys && tr.backEndRenderer == BE_ARB2) {
			globalImages->currentRenderImage->CopyFramebuffer(backEnd.viewDef->viewport.x1,
				backEnd.viewDef->viewport.y1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1);
		}
		backEnd.currentRenderCopied = true;
	}

	GL_SelectTexture(1);
	globalImages->BindNull();

	GL_SelectTexture(0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

//	RB_SetProgramEnvironment();

	// we don't use RB_RenderDrawSurfListWithFunction()
	// because we want to defer the matrix load because many
	// surfaces won't draw any ambient passes
	backEnd.currentSpace = NULL;
	for (i = 0; i < numDrawSurfs; i++) {
		if (drawSurfs[i]->material->SuppressInSubview()) {
			continue;
		}

		if (backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef) {
			if (drawSurfs[i]->space->entityDef->parms.xrayIndex != 2) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if (drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
			&& !backEnd.currentRenderCopied) {
			break;
		}

		RB_STD_T_RenderShaderPasses(drawSurfs[i]);
	}

	GL_Cull(CT_FRONT_SIDED);
	glColor3f(1, 1, 1);

	return i;
}