#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 720
#define HEIGHT 600
#define CARD_HEIGHT 85
#define MAX_APPS 64

typedef struct {
    uint32_t index;
    char name[256];
    int volume;
    int muted;
    SDL_Texture *icon;
} App;

typedef struct {
    SDL_Color bg;
    SDL_Color card;
    SDL_Color text;
    SDL_Color bar_bg;
    SDL_Color accent;
    SDL_Color btn;
} Theme;

// テーマ定義: 0=Dark, 1=Light
Theme themes[2] = {
    {{28, 28, 32, 255}, {45, 45, 55, 255}, {230, 230, 230, 255}, {70, 70, 80, 255}, {0, 170, 255, 255}, {70, 70, 95, 255}}, // Dark
    {{240, 240, 245, 255}, {255, 255, 255, 255}, {30, 30, 40, 255}, {210, 210, 220, 255}, {0, 120, 215, 255}, {200, 200, 210, 255}} // Light
};

App apps[MAX_APPS];
int app_count = 0;
int current_theme = 0; // 0: Dark, 1: Light
pa_mainloop *pa_ml = NULL;
pa_context *pa_ctx = NULL;
TTF_Font *font = NULL;
SDL_Renderer *ren = NULL;

/* --- PulseAudio & Icon 処理 (前回と同じ) --- */
void set_volume(uint32_t index, int percent) {
    if (!pa_ctx) return;
    pa_cvolume v; pa_cvolume_set(&v, 2, pa_sw_volume_from_linear((double)percent / 100.0));
    pa_context_set_sink_input_volume(pa_ctx, index, &v, NULL, NULL);
}
void set_mute(uint32_t index, int mute) { if (pa_ctx) pa_context_set_sink_input_mute(pa_ctx, index, mute, NULL, NULL); }

SDL_Texture* load_app_icon(const char *icon_name) {
    if (!icon_name || !ren) return NULL;
    char path[512];
    const char *dirs[] = {"/usr/share/icons/hicolor/48x48/apps", "/usr/share/pixmaps", "/usr/share/icons/hicolor/scalable/apps"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s/%s.png", dirs[i], icon_name);
        SDL_Surface *s = IMG_Load(path);
        if (s) { SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s); SDL_FreeSurface(s); return t; }
    }
    return NULL;
}

void sink_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
    if (eol > 0 || !i || app_count >= MAX_APPS) return;
    App *a = &apps[app_count]; a->index = i->index;
    const char *n = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    const char *ic = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_ICON_NAME);
    strncpy(a->name, n ? n : "Unknown", 255);
    a->volume = (int)(pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume)) * 100.0);
    a->muted = i->mute; a->icon = load_app_icon(ic); app_count++;
}

void refresh_apps() {
    for(int i = 0; i < app_count; i++) if(apps[i].icon) SDL_DestroyTexture(apps[i].icon);
    app_count = 0;
    pa_operation *op = pa_context_get_sink_input_info_list(pa_ctx, sink_cb, NULL);
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_mainloop_iterate(pa_ml, 1, NULL);
    pa_operation_unref(op);
}

/* --- UI 描画 --- */
void draw_text(const char *text, int x, int y, SDL_Color color) {
    if (!font || !text || strlen(text) == 0) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text, color);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_Rect dst = {x, y, s->w, s->h}; SDL_RenderCopy(ren, t, NULL, &dst);
    SDL_FreeSurface(s); SDL_DestroyTexture(t);
}

void draw_card(int y, int idx) {
    App *a = &apps[idx]; Theme *t = &themes[current_theme];
    
    SDL_Rect card = {40, y, 640, 70};
    SDL_SetRenderDrawColor(ren, t->card.r, t->card.g, t->card.b, 255);
    SDL_RenderFillRect(ren, &card);

    if (a->icon) { SDL_Rect icr = {55, y+15, 40, 40}; SDL_RenderCopy(ren, a->icon, NULL, &icr); }
    draw_text(a->name, 110, y + 10, t->text);

    // Buttons: Mute / 100%
    SDL_SetRenderDrawColor(ren, a->muted ? 220 : t->btn.r, a->muted ? 80 : t->btn.g, a->muted ? 80 : t->btn.b, 255);
    SDL_Rect bm = {475, y+10, 45, 25}; SDL_RenderFillRect(ren, &bm);
    draw_text("M", 490, y+13, a->muted ? (SDL_Color){255,255,255,255} : t->text);

    SDL_SetRenderDrawColor(ren, t->btn.r, t->btn.g, t->btn.b, 255);
    SDL_Rect b100 = {530, y+10, 50, 25}; SDL_RenderFillRect(ren, &b100);
    draw_text("100", 540, y+13, t->text);

    char buf[16]; snprintf(buf, 16, "%d%%", a->volume);
    draw_text(buf, 605, y+13, t->text);

    // Bar
    SDL_SetRenderDrawColor(ren, t->bar_bg.r, t->bar_bg.g, t->bar_bg.b, 255);
    SDL_Rect bb = {110, y+45, 520, 12}; SDL_RenderFillRect(ren, &bb);
    
    int fw = (520 * a->volume) / 150; if (fw > 520) fw = 520;
    SDL_Rect bar = {110, y+45, fw, 12};
    if (a->muted) SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
    else SDL_SetRenderDrawColor(ren, t->accent.r, t->accent.g, t->accent.b, 255);
    SDL_RenderFillRect(ren, &bar);
}

int main() {
    pa_ml = pa_mainloop_new(); pa_ctx = pa_context_new(pa_mainloop_get_api(pa_ml), "VolPro");
    pa_context_connect(pa_ctx, NULL, 0, NULL);
    while (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) pa_mainloop_iterate(pa_ml, 1, NULL);

    SDL_Init(SDL_INIT_VIDEO); TTF_Init(); IMG_Init(IMG_INIT_PNG);
    font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14);
    SDL_Window *win = SDL_CreateWindow("Volume Control", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    refresh_apps();

    int running = 1, dragging = -1; Uint32 last_ref = SDL_GetTicks();
    while (running) {
        pa_mainloop_iterate(pa_ml, 0, NULL);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                // Theme Toggle Button (Top Right)
                if (e.button.x >= 600 && e.button.y >= 5 && e.button.y <= 30) current_theme = !current_theme;
                for (int i = 0; i < app_count; i++) {
                    int cy = 40 + i * CARD_HEIGHT;
                    if (e.button.x >= 475 && e.button.x <= 520 && e.button.y >= cy+10 && e.button.y <= cy+35) { apps[i].muted = !apps[i].muted; set_mute(apps[i].index, apps[i].muted); }
                    else if (e.button.x >= 530 && e.button.x <= 580 && e.button.y >= cy+10 && e.button.y <= cy+35) { apps[i].volume = 100; set_volume(apps[i].index, 100); }
                    else if (e.button.x >= 110 && e.button.x <= 630 && e.button.y >= cy+40 && e.button.y <= cy+65) dragging = i;
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP) dragging = -1;
            if (e.type == SDL_MOUSEMOTION && dragging >= 0) {
                int p = (e.motion.x - 110) * 150 / 520;
                if (p < 0) p = 0; if (p > 150) p = 150;
                apps[dragging].volume = p; set_volume(apps[dragging].index, p);
            }
        }
        if (dragging == -1 && SDL_GetTicks() - last_ref > 3000) { refresh_apps(); last_ref = SDL_GetTicks(); }
        Theme *t = &themes[current_theme];
        SDL_SetRenderDrawColor(ren, t->bg.r, t->bg.g, t->bg.b, 255); SDL_RenderClear(ren);
        
        // Draw Theme Toggle Button
        SDL_SetRenderDrawColor(ren, t->btn.r, t->btn.g, t->btn.b, 255);
        SDL_Rect tr = {600, 5, 100, 25}; SDL_RenderFillRect(ren, &tr);
        draw_text(current_theme ? "Dark Mode" : "Light Mode", 610, 8, t->text);

        for (int i = 0; i < app_count; i++) draw_card(40 + i * CARD_HEIGHT, i);
        SDL_RenderPresent(ren); SDL_Delay(16);
    }
    SDL_Quit(); return 0;
}
