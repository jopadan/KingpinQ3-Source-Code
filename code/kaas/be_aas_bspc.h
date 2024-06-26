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
#ifndef BE_AAS_BSPC_H_
#define BE_AAS_BSPC_H_
void AAS_CalcReachAndClusters(struct quakefile_s *qf);
void AAS_InitBotImport(void);
//void Com_Memcpy2 (void* dest, const void* src, const size_t count);
//void Com_Memset2 (void* dest, const int val, const size_t count);
void Com_DPrintf(const char *fmt, ...);  //hypov8
#endif // BE_AAS_BSPC_H_