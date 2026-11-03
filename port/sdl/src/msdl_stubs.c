/* Native SDL2 subset: rectangles, and the parts that are not there yet
   (joysticks, game controllers, SDL audio devices). Sound will be a native
   mixer on Paula, not SDL audio. */
#include <string.h>

#include "msdl_internal.h"

SDL_bool SDL_IntersectRect(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *result)
{
    int x0, y0, x1, y1;
    if (a == NULL || b == NULL || result == NULL) return SDL_FALSE;
    x0 = a->x > b->x ? a->x : b->x;
    y0 = a->y > b->y ? a->y : b->y;
    x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    result->x = x0;
    result->y = y0;
    result->w = x1 > x0 ? x1 - x0 : 0;
    result->h = y1 > y0 ? y1 - y0 : 0;
    return (result->w > 0 && result->h > 0) ? SDL_TRUE : SDL_FALSE;
}

/* ---- joysticks and game controllers: none yet ---- */
int SDL_NumJoysticks(void) { return 0; }

SDL_Joystick *SDL_JoystickOpen(int device_index)
{
    (void)device_index;
    SDL_SetError("no joysticks");
    return NULL;
}

SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick) { (void)joystick; return -1; }

SDL_JoystickGUID SDL_JoystickGetGUID(SDL_Joystick *joystick)
{
    SDL_JoystickGUID g;
    (void)joystick;
    memset(&g, 0, sizeof(g));
    return g;
}

const char *SDL_JoystickNameForIndex(int device_index) { (void)device_index; return NULL; }

int SDL_GameControllerAddMapping(const char *mappingString) { (void)mappingString; return -1; }

SDL_GameController *SDL_GameControllerOpen(int joystick_index)
{
    (void)joystick_index;
    SDL_SetError("no game controllers");
    return NULL;
}

Uint8 SDL_GameControllerGetButton(SDL_GameController *gc, SDL_GameControllerButton button)
{
    (void)gc;
    (void)button;
    return 0;
}

SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gc) { (void)gc; return NULL; }

SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *gc)
{
    (void)gc;
    return SDL_CONTROLLER_TYPE_UNKNOWN;
}

SDL_bool SDL_GameControllerHasButton(SDL_GameController *gc, SDL_GameControllerButton button)
{
    (void)gc;
    (void)button;
    return SDL_FALSE;
}

char *SDL_GameControllerMappingForGUID(SDL_JoystickGUID guid)
{
    (void)guid;
    return NULL;
}

/* ---- SDL audio devices: none ---- */
int SDL_GetNumAudioDevices(int iscapture) { (void)iscapture; return 0; }
const char *SDL_GetAudioDeviceName(int index, int iscapture) { (void)index; (void)iscapture; return NULL; }

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained, int allowed_changes)
{
    (void)device; (void)iscapture; (void)desired; (void)obtained; (void)allowed_changes;
    SDL_SetError("no SDL audio");
    return 0;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) { (void)dev; (void)pause_on; }
void SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }

SDL_AudioStream *SDL_NewAudioStream(const SDL_AudioFormat src_format, const Uint8 src_channels, const int src_rate,
                                    const SDL_AudioFormat dst_format, const Uint8 dst_channels, const int dst_rate)
{
    (void)src_format; (void)src_channels; (void)src_rate; (void)dst_format; (void)dst_channels; (void)dst_rate;
    SDL_SetError("no audio streams");
    return NULL;
}

int SDL_AudioStreamPut(SDL_AudioStream *stream, const void *buf, int len)
{
    (void)stream; (void)buf; (void)len;
    return -1;
}

int SDL_AudioStreamGet(SDL_AudioStream *stream, void *buf, int len)
{
    (void)stream; (void)buf; (void)len;
    return -1;
}

void SDL_AudioStreamClear(SDL_AudioStream *stream) { (void)stream; }
void SDL_FreeAudioStream(SDL_AudioStream *stream) { (void)stream; }
