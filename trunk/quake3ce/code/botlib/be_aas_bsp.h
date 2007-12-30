/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*****************************************************************************
 * name:		be_aas_bsp.h
 *
 * desc:		AAS
 *
 * $Archive: /source/code/botlib/be_aas_bsp.h $
 *
 *****************************************************************************/

#ifdef AASINTERN
//loads the given BSP file
int AAS_LoadBSPFile(void);
//dump the loaded BSP data
void AAS_DumpBSPData(void);
//unlink the given entity from the bsp tree leaves
void AAS_UnlinkFromBSPLeaves(bsp_link_t *leaves);
//link the given entity to the bsp tree leaves of the given model
bsp_link_t *AAS_BSPLinkEntity(bvec3_t absmins,
										bvec3_t absmaxs,
										int entnum,
										int modelnum);

//calculates collision with given entity
qboolean AAS_EntityCollision(int entnum,
										bvec3_t start,
										bvec3_t boxmins,
										bvec3_t boxmaxs,
										bvec3_t end,
										int contentmask,
										bsp_trace_t *trace);
//for debugging
void AAS_PrintFreeBSPLinks(char *str);
//
#endif //AASINTERN

#define MAX_EPAIRKEY		128

//trace through the world
bsp_trace_t AAS_Trace(	bvec3_t start,
								bvec3_t mins,
								bvec3_t maxs,
								bvec3_t end,
								int passent,
								int contentmask);
//returns the contents at the given point
int AAS_PointContents(bvec3_t point);
//returns true when p2 is in the PVS of p1
qboolean AAS_inPVS(bvec3_t p1, bvec3_t p2);
//returns true when p2 is in the PHS of p1
qboolean AAS_inPHS(bvec3_t p1, bvec3_t p2);
//returns true if the given areas are connected
qboolean AAS_AreasConnected(int area1, int area2);
//creates a list with entities totally or partly within the given box
int AAS_BoxEntities(bvec3_t absmins, bvec3_t absmaxs, int *list, int maxcount);
//gets the mins, maxs and origin of a BSP model
void AAS_BSPModelMinsMaxsOrigin(int modelnum, avec3_t angles, bvec3_t mins, bvec3_t maxs, bvec3_t origin);
//handle to the next bsp entity
int AAS_NextBSPEntity(int ent);
//return the value of the BSP epair key
int AAS_ValueForBSPEpairKey(int ent, char *key, char *value, int size);
//get a vector for the BSP epair key
int AAS_VectorForBSPEpairKey(int ent, char *key, bvec3_t v);
//get a gfixed for the BSP epair key
int AAS_FloatForBSPEpairKey(int ent, char *key, afixed *value);
int AAS_FloatForBSPEpairKey(int ent, char *key, bfixed *value);
int AAS_FloatForBSPEpairKey(int ent, char *key, gfixed *value);
//get an integer for the BSP epair key
int AAS_IntForBSPEpairKey(int ent, char *key, int *value);

