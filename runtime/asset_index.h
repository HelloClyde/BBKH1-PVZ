/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef PVZ_ASSET_INDEX_H
#define PVZ_ASSET_INDEX_H
#include <stddef.h>
#include <stdint.h>
typedef struct {const unsigned char *data;uint32_t count,strings,bytes;} PvzAssetIndex;
int pvz_asset_index_parse(PvzAssetIndex *,const void *,size_t);
int pvz_asset_index_find(const PvzAssetIndex *,const char *,uint32_t *,int *);
int pvz_asset_index_managed(const char *);
#endif
