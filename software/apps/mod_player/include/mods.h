#ifndef MODS_H
#define MODS_H

#include "becks_mod.h"
#include "commando_mod.h"
#include "lotus2_mod.h"
#include "metal_slug_mod.h"
#include "parallax_mod.h"

typedef struct {
    const unsigned char *data;
    const int size;
} mod_entry_t;

static const mod_entry_t mod_list[] = {
    {becks_mod, becks_mod_len}, 
    {lotus2_mod, lotus2_mod_len},
    //{commando_mod, commando_mod_len}, 
    //{metal_slug_mod, metal_slug_mod_len}, 
    //{parallax_mod, parallax_mod_len}
};

static const int mod_count = sizeof(mod_list) / sizeof(mod_list[0]);

#endif