#ifndef VITA_MOONLIGHT_INTERNAL_H
#define VITA_MOONLIGHT_INTERNAL_H

#include "moonlight/api.h"

/* Internal bridge from subsystems to the public Moonlight event callback. */
void moonlight_api_emit_event(const MoonlightEvent *event);

#endif /* VITA_MOONLIGHT_INTERNAL_H */
