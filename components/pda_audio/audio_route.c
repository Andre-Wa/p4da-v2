/**
 * audio_route.c — roteamento AUTO/interno/UAC (SOM.2). Ver audio_route.h.
 */
#include "audio_route.h"
#include "audio_uac.h"

static audio_route_t s_route = AUDIO_ROUTE_AUTO;

void audio_route_set(audio_route_t r) { s_route = r; }
audio_route_t audio_route_get(void) { return s_route; }

bool audio_route_use_uac(void)
{
    if (s_route == AUDIO_ROUTE_UAC) return true;
    if (s_route == AUDIO_ROUTE_INTERNAL) return false;
    return audio_uac_present();   /* AUTO */
}

const char *audio_route_label(void)
{
    switch (s_route) {
    case AUDIO_ROUTE_INTERNAL: return "falante";
    case AUDIO_ROUTE_UAC:      return "USB";
    default:                   return "auto";
    }
}
