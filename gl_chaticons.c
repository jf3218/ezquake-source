/*
Copyright (C) 1996-2003 Id Software, Inc., A Nourai

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"

extern cvar_t r_chaticons_alpha;

/**************************************** chat icon *****************************/

// qqshka: code is a mixture of autoid and particle engine

static ci_player_t ci_clients[MAX_CLIENTS];
static int ci_count;

static ci_texture_t ci_textures[num_citextures];

qbool ci_initialized = false;

#define FONT_SIZE (256.0)

#define ADD_CICON_TEXTURE(_ptex, _texnum, _texindex, _components, _s1, _t1, _s2, _t2)	\
	do {																					\
		ci_textures[_ptex].texnum = _texnum;												\
		ci_textures[_ptex].components = _components;										\
		ci_textures[_ptex].coords[_texindex][0] = (_s1 + 1) / FONT_SIZE;					\
		ci_textures[_ptex].coords[_texindex][1] = (_t1 + 1) / FONT_SIZE;					\
		ci_textures[_ptex].coords[_texindex][2] = (_s2 - 1) / FONT_SIZE;					\
		ci_textures[_ptex].coords[_texindex][3] = (_t2 - 1) / FONT_SIZE;					\
	} while(0);

void CI_Init(void)
{
	int ci_font;
	int texmode = TEX_ALPHA | TEX_COMPLAIN | TEX_MIPMAP;

	ci_initialized = false;

	if (!gl_scaleModelTextures.value)
		texmode |= TEX_NOSCALE;

	if (!(ci_font = GL_LoadTextureImage("textures/chaticons", "ci:chaticons", FONT_SIZE, FONT_SIZE, texmode)))
		return;

	ADD_CICON_TEXTURE(citex_chat, ci_font, 0, 1, 0, 0, 64, 64); // get chat part from font
	ADD_CICON_TEXTURE(citex_afk, ci_font, 0, 1, 64, 0, 128, 64); // get afk part
	ADD_CICON_TEXTURE(citex_chat_afk, ci_font, 0, 1, 0, 0, 128, 64); // get chat+afk part

	ci_initialized = true;
}

int CmpCI_Order(const void *p1, const void *p2)
{
	const ci_player_t	*a1 = (ci_player_t *)p1;
	const ci_player_t	*a2 = (ci_player_t *)p2;
	int l1 = a1->distance;
	int l2 = a2->distance;

	if (l1 > l2)
		return -1;
	if (l1 < l2)
		return  1;

	return 0;
}

void SCR_SetupCI(void)
{
	int j, tracknum = -1;
	player_state_t *state;
	player_info_t *info;
	ci_player_t *id;
	centity_t *cent;
	char *s;

	ci_count = 0;

	if (!bound(0, r_chaticons_alpha.value, 1))
		return;

	if (cls.state != ca_active || !cl.validsequence || cl.intermission)
		return;

	if (cl.spectator)
		tracknum = Cam_TrackNum();

	state = cl.frames[cl.parsecount & UPDATE_MASK].playerstate;
	info = cl.players;
	cent = &cl_entities[1];

	for (j = 0; j < MAX_CLIENTS; j++, info++, state++, cent++) {
		if (state->messagenum != cl.parsecount || j == cl.playernum || j == tracknum || info->spectator)
			continue;

		if (!*(s = Info_ValueForKey(info->userinfo, "chat")))
			continue; // user not chatting, so ignore

		id = &ci_clients[ci_count];
		id->texindex = 0;
		id->player = info;

		id->org[0] = cent->lerp_origin[0];
		id->org[1] = cent->lerp_origin[1];
		id->org[2] = cent->lerp_origin[2] + 33; // move baloon up a bit

		id->size = 8; // scale baloon
		id->rotangle = 5 * sin(2 * r_refdef2.time); // may be set to 0, if u dislike rolling
		id->color[0] = 255; // r
		id->color[1] = 255; // g
		id->color[2] = 255; // b
		id->color[3] = 255 * bound(0, r_chaticons_alpha.value, 1); // alpha
		{
			vec3_t diff;

			VectorSubtract(id->org, r_refdef.vieworg, diff);
			id->distance = VectorLength(diff);
		}
		if ((!cls.mvdplayback || Cam_TrackNum() >= 0) && cl.racing) {
			if (id->distance < KTX_RACING_PLAYER_MIN_DISTANCE) {
				continue; // too close, hide indicators
			}
			id->color[3] *= min(id->distance, KTX_RACING_PLAYER_MAX_DISTANCE) / KTX_RACING_PLAYER_ALPHA_SCALE;
		}
		id->flags = Q_atoi(s) & (CIF_CHAT | CIF_AFK); // get known flags
		id->flags = (id->flags ? id->flags : CIF_CHAT); // use chat as default if we got some unknown "chat" value

		ci_count++;
	}

	if (ci_count) // sort icons so we draw most far to you first
		qsort((void *)ci_clients, ci_count, sizeof(ci_clients[0]), CmpCI_Order);
}

void CI_DrawBillboard(ci_texture_t* _ptex, ci_player_t* _p, vec3_t _coord[4])
{
	if (GL_ShadersSupported()) {
		GLM_DrawBillboard(_ptex, _p, _coord);
	}
	else {
		GLC_DrawBillboard(_ptex, _p, _coord);
	}
}

// probably may be made as macros, but i hate macros cos macroses is unsafe
static void CI_Bind(ci_texture_t *citex, int *texture)
{
	//VULT PARTICLES - I gather this speeds it up, but I haven't really checked
	if (*texture != citex->texnum) {
		GL_Bind(citex->texnum);
		*texture = citex->texnum;
	}
}

void DrawCI(void)
{
	int	i, texture = 0, flags;
	vec3_t billboard[4], billboard2[4], vright_tmp;
	ci_player_t *p;
	ci_texture_t *citex;

	if (!ci_initialized)
		return;

	if (!bound(0, r_chaticons_alpha.value, 1) || ci_count < 1)
		return;

	GL_DisableFog();

	VectorAdd(vup, vright, billboard[2]);
	VectorSubtract(vright, vup, billboard[3]);
	VectorNegate(billboard[2], billboard[0]);
	VectorNegate(billboard[3], billboard[1]);

	VectorScale(vright, 2, vright_tmp);
	VectorAdd(vup, vright_tmp, billboard2[2]);
	VectorSubtract(vright_tmp, vup, billboard2[3]);
	VectorNegate(billboard2[2], billboard2[0]);
	VectorNegate(billboard2[3], billboard2[1]);

	glDepthMask(GL_FALSE);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	GL_TextureEnvMode(GL_MODULATE);
	glShadeModel(GL_SMOOTH);

	// FIXME: i'm not sure which blend mode here better
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_TEXTURE_2D);

	for (i = 0; i < ci_count; i++) {
		p = &ci_clients[i];
		flags = p->flags;

		if (flags & CIF_CHAT && flags & CIF_AFK) {
			flags = flags & ~(CIF_CHAT | CIF_AFK); // so they will be not showed below again
			CI_Bind(citex = &ci_textures[citex_chat_afk], &texture);
			CI_DrawBillboard(citex, p, billboard2);
		}

		if (flags & CIF_CHAT) {
			CI_Bind(citex = &ci_textures[citex_chat], &texture);
			CI_DrawBillboard(citex, p, billboard);
		}

		if (flags & CIF_AFK) {
			CI_Bind(citex = &ci_textures[citex_afk], &texture);
			CI_DrawBillboard(citex, p, billboard);
		}
	}

	glEnable(GL_TEXTURE_2D);
	glDepthMask(GL_TRUE);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TextureEnvMode(GL_REPLACE);
	glShadeModel(GL_FLAT);

	GL_EnableFog();
}
