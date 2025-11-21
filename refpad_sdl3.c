
#include "refpad_editor.c"

#define SDL_STATIC_LIB
#include <SDL3/SDL.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#define PIXELS_PER_SCROLL_TICK 50 // @Hardcoded
#define PIXELS_PER_SCROLL_BUTTON_PRESS 16

#define GLYPH_TEXTURE_SIZE 64
#define GLYPH_CACHE_CAPACITY 2048

// Bound to CTRL+[0-9]
static const unsigned char *QuickPastePalette[10] = {
    /* 0 */ (const unsigned char *)u8"Ζήνων ὁ Ἐλεᾱ́της",
    /* 1 */ (const unsigned char *)u8"There is a saying in Farsi that goes like \"مرغه همسایه همیشه غازه\" which literally translates to \"The neighbor’s chicken is always a goose\". This saying refers to the fact that people often think that what others have is better than what they themselves have. Farsi is full of sayings, proverbs, poems, and cool stories! \n\nانگلیسی و المانی خیلی ضرب المثل های جالب دارن. خلیلی هاشون مستقیم به فارسی نمیتونن تبدیل شن ولی خیلی ضرب المثل ها شبیه به نسخه های فارسیشون هستن مثلا \"Der Apfel fällt nicht weit vom Stamm\" که یک ضرب المثل المانی هست که نسخه فارسیش میشه سیب خلی دور از درخت نمی افته. By the way, there are no capital letters in Farsi. من بازی کویک رو خیلی دوست دارم! کاشکی این بازی نت کود بهتری داشت!",
    /* 2 */ (const unsigned char *)u8"Андре́й Никола́евич Колмого́ров",
    /* 3 */ (const unsigned char *)u8"窓際族",
    /* 4 */ (const unsigned char *)u8"詠春",
    /* 5 */ (const unsigned char *)u8"H̵̛͕̞̦̰̜͍̰̥̟͆̏͂̌͑ͅ",
    /* 6 */ (const unsigned char *)u8"",
    /* 7 */ (const unsigned char *)u8"",
    /* 8 */ (const unsigned char *)u8"",
    /* 9 */ (const unsigned char *)u8"",
};

typedef struct cached_glyph
{
    font *Font;
    int GlyphIndex;
    float Scale;

    uint8_t *Data;
    int MinX;
    int MinY;
    int MaxX;
    int MaxY;
} cached_glyph;

typedef struct ui_style {
    uint32_t BackgroundColor;
    uint32_t ForegroundColor;
    uint32_t CursorColor;
    uint32_t SelectionBackgroundColor;
    uint32_t SelectionForegroundColor;
    int ScrollbarThickness;
} ui_style;

typedef uint32_t ui_element_id;
enum ui_element_id_enum {
    UI_ELEMENT_ID_NONE,
    UI_ELEMENT_ID_TEXT_AREA,
    UI_ELEMENT_ID_SCROLL_UP,
    UI_ELEMENT_ID_SCROLL_DOWN,
    UI_ELEMENT_ID_VERTICAL_SCROLLBAR,
    UI_ELEMENT_ID_SCROLL_LEFT,
    UI_ELEMENT_ID_SCROLL_RIGHT,
    UI_ELEMENT_ID_HORIZONTAL_SCROLLBAR,

    UI_ELEMENT_ID_COUNT,
};

typedef struct app_state {
    SDL_Window* Window;
    SDL_Renderer* Renderer;
    SDL_Texture* Texture;
    SDL_Cursor* ArrowCursor;
    SDL_Cursor* TextCursor;

    cached_glyph *CachedGlyphs;
    int CachedGlyphCount;

    ui_style Style;

    float TextureWidth;
    float TextureHeight;

    int FontPixelHeight;

    int ScrollMinX;
    int ScrollMinY;
    int ScrollMaxX;
    int ScrollMaxY;

    float ScrollOffset;
    int ScrollingX;
    int ScrollingY;

    editor Editor;
    int Frame;
} app_state;

static size_t StringLength(const char *S)
{
    size_t Result = 0;
    if(S)
    {
        while(*S++)
        {
            Result += 1;
        }
    }
    return Result;
}

static void ClearPixels(uint32_t* Pixels, int Width, int Height, uint32_t Color) {
    for (int I = 0; I < Width * Height; I++) {
        Pixels[I] = Color;
    }
}

static void DrawBox(uint32_t* Pixels, int PixelsWidth, int PixelsHeight, int RectX, int RectY, int RectW, int RectH, uint32_t Color) {
    int RectXStart = MAXIMUM(0, MINIMUM(PixelsWidth-1, RectX));
    int RectXEnd = MAXIMUM(0, MINIMUM(PixelsWidth, RectX + RectW));
    int RectYStart = MAXIMUM(0, MINIMUM(PixelsHeight-1, RectY));
    int RectYEnd = MAXIMUM(0, MINIMUM(PixelsHeight, RectY + RectH));
    for (int Y = RectYStart; Y < RectYEnd; ++Y) {
        int Offset = Y * PixelsWidth;
        for (int X = RectXStart; X < RectXEnd; ++X) {
            Pixels[Offset + X] = Color;
        }
    }
}
static void DrawRect(uint32_t* Pixels, int PixelsWidth, int PixelsHeight, int MinX, int MinY, int MaxX, int MaxY, uint32_t Color) {
    int RectXStart = MAXIMUM(0, MINIMUM(PixelsWidth-1, MinX));
    int RectXEnd = MAXIMUM(0, MINIMUM(PixelsWidth, MaxX));
    int RectYStart = MAXIMUM(0, MINIMUM(PixelsHeight-1, MinY));
    int RectYEnd = MAXIMUM(0, MINIMUM(PixelsHeight, MaxY));
    for (int Y = RectYStart; Y < RectYEnd; ++Y) {
        int Offset = Y * PixelsWidth;
        for (int X = RectXStart; X < RectXEnd; ++X) {
            Pixels[Offset + X] = Color;
        }
    }
}

static cached_glyph *FindOrCreateGlyph(app_state* App, font *Font, int GlyphIndex, float Scale) {
    // TODO(keeba): Replace this with a real cache with eviction!
    cached_glyph *Result = &App->CachedGlyphs[0];

    for (int CachedGlyphIndex = 1; CachedGlyphIndex <= App->CachedGlyphCount; ++CachedGlyphIndex) {
        cached_glyph *Entry = &App->CachedGlyphs[CachedGlyphIndex];
        if ((Entry->Font == Font) && (Entry->GlyphIndex == GlyphIndex) && (Entry->Scale == Scale)) {
            Result = Entry;
            break;
        }
    }

    if ((Result == &App->CachedGlyphs[0]) && ((App->CachedGlyphCount + 1) < GLYPH_CACHE_CAPACITY)) {
        Result = &App->CachedGlyphs[++App->CachedGlyphCount];
        Result->Font = Font;
        Result->GlyphIndex = GlyphIndex;
        Result->Scale = Scale;
        stbtt_GetGlyphBitmapBoxSubpixel(&Font->Stbtt, GlyphIndex, Scale, Scale, 0, 0,
                                        &Result->MinX, &Result->MinY, &Result->MaxX, &Result->MaxY);
        if (((Result->MaxX - Result->MinX) <= GLYPH_TEXTURE_SIZE) && ((Result->MaxY - Result->MinY) <= GLYPH_TEXTURE_SIZE)) {
            stbtt_MakeGlyphBitmapSubpixel(&Font->Stbtt, Result->Data, GLYPH_TEXTURE_SIZE, GLYPH_TEXTURE_SIZE, GLYPH_TEXTURE_SIZE,
                Scale, Scale, 0, 0, GlyphIndex);
        }
    }

    return Result;
}

static void AppResize(app_state* App, int Width, int Height) {
    if (App->Texture)
        SDL_DestroyTexture(App->Texture);

    App->Texture = SDL_CreateTexture(App->Renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        Width,
        Height);
    SDL_GetTextureSize(App->Texture, &App->TextureWidth, &App->TextureHeight);

    SDL_StopTextInput(App->Window);

    SDL_Rect TextInputRect = {0};
    TextInputRect.w = (int)App->TextureWidth - App->Style.ScrollbarThickness;
    TextInputRect.h = (int)App->Editor.LineHeight;
    SDL_SetTextInputArea(App->Window, &TextInputRect, 0);

    SDL_StartTextInput(App->Window);
}

static float Lerp(float From, float To, float T) {
    float Result = From + (To - From) * T;
    return Result;
}

static void AppDraw(app_state* App, uint32_t* Pixels, int Width, int Height, editor* Editor) {
    // NOTE(keeba): The code below could be considered a backend and moved off into a render_software.c file.
    // Draw commands contain glyph indices instead of textures, because we might imagine some renderer wants
    // to integrate nicely with e.g. Windows and use ClearType, or get the curve data and use SDFs.

    ui_style *Style = &App->Style;

    int TextAreaWidth = Width - Style->ScrollbarThickness;
    int TextAreaHeight = Height - Style->ScrollbarThickness;

    if (TextAreaWidth > 0 && TextAreaHeight > 0) {
        ClearPixels(Pixels, Width, Height, Style->BackgroundColor);

        draw_command_list DrawList = Draw(Editor, App->FontPixelHeight, TextAreaWidth, TextAreaHeight);

        for (int I = 0; I < (int)DrawList.SelectionsCount; ++I) {
            draw_box* Sel = &DrawList.Selections[I];
            DrawRect(Pixels, Width, Height, (int)Sel->MinX, (int)Sel->MinY, (int)Sel->MaxX, (int)Sel->MaxY, Style->SelectionBackgroundColor);
        }

        float CursorHeight = (float)Editor->LineHeight;
        float CursorWidth = 2.0f;

        SDL_Rect TextInputRect = {0};
        TextInputRect.w = TextAreaWidth;
        TextInputRect.h = App->Editor.LineHeight;
        SDL_SetTextInputArea(App->Window, &TextInputRect, (int)DrawList.Cursor.X);

        DrawBox(Pixels, Width, Height, (int)DrawList.Cursor.X, (int)DrawList.Cursor.Y, (int)CursorWidth, (int)CursorHeight, Style->CursorColor);

        { // Draw scrollbars.
            int Thickness = Style->ScrollbarThickness;

            int VerticalScrollbarX = Width - Thickness;
            int DownButtonY = Height - Thickness * 2;
            DrawRect(Pixels, Width, Height, VerticalScrollbarX, 0,           Width, Thickness, 0xFF555555); // Up button.
            DrawRect(Pixels, Width, Height, VerticalScrollbarX, Thickness,   Width, DownButtonY, 0xFF000000); // Vertical scroll area.
            DrawRect(Pixels, Width, Height, VerticalScrollbarX, DownButtonY, Width, Height - Thickness, 0xFF555555); // Down button.

            int HorizontalScrollbarY = Height - Thickness;
            int RightButtonX = Width - Thickness * 2;
            DrawRect(Pixels, Width, Height, 0,            HorizontalScrollbarY, Thickness,         Height, 0xFF555555); // Left button.
            DrawRect(Pixels, Width, Height, Thickness,    HorizontalScrollbarY, RightButtonX,      Height, 0xFF000000); // Horizontal scroll area.
            DrawRect(Pixels, Width, Height, RightButtonX, HorizontalScrollbarY, Width - Thickness, Height, 0xFF555555); // Right button.

            int ScrollMinY = (int)Lerp((float)Thickness, (float)DownButtonY, DrawList.ScrollMinY);
            int ScrollMaxY = (int)Lerp((float)Thickness, (float)DownButtonY, DrawList.ScrollMaxY);
            DrawRect(Pixels, Width, Height, VerticalScrollbarX, ScrollMinY, Width, ScrollMaxY, 0xFFFFFFFF);

            int ScrollMinX = (int)Lerp((float)Thickness, (float)RightButtonX, DrawList.ScrollMinX);
            int ScrollMaxX = (int)Lerp((float)Thickness, (float)RightButtonX, DrawList.ScrollMaxX);
            DrawRect(Pixels, Width, Height, ScrollMinX, HorizontalScrollbarY, ScrollMaxX, Height, 0xFFFFFFFF);

            App->ScrollMinX = ScrollMinX;
            App->ScrollMaxX = ScrollMaxX;
            App->ScrollMinY = ScrollMinY;
            App->ScrollMaxY = ScrollMaxY;
        }

        for(int CommandIndex = 0; CommandIndex < (int)DrawList.Count; ++CommandIndex) {
            draw_command* Command = &DrawList.Commands[CommandIndex];

            if(Command->Font && (Command->Flags & DRAW_COMMAND_FLAG_VISIBLE))
            {
                cached_glyph *CachedGlyph = FindOrCreateGlyph(App, Command->Font, Command->GlyphIndex, Command->Scale);

                // Blit the glyph to the screen.
                int GlyphWidth = CachedGlyph->MaxX - CachedGlyph->MinX;
                int GlyphHeight = CachedGlyph->MaxY - CachedGlyph->MinY;

                int SourceOffsetX = 0;
                int SourceOffsetY = 0;

                int OutX = (int)SDL_roundf(Command->X) + CachedGlyph->MinX;
                int OutY = (int)SDL_roundf(Command->Y) + CachedGlyph->MinY;

                if(OutX < 0) {
                    SourceOffsetX = -OutX;
                    OutX = 0;
                }
                if(OutY < 0) {
                    SourceOffsetY = -OutY;
                    OutY = 0;
                }

                int ScissoredWidth = MINIMUM(TextAreaWidth - OutX, GlyphWidth - SourceOffsetX);
                int ScissoredHeight = MINIMUM(TextAreaHeight - OutY, GlyphHeight - SourceOffsetY);

                uint32_t ForegroundArgb = Style->ForegroundColor;
                if(Command->Flags & DRAW_COMMAND_FLAG_SELECTED) {
                    ForegroundArgb = Style->SelectionForegroundColor;
                }

                float ForegroundR = (float)(ForegroundArgb & 0xFF) * (1.0f / 255.0f);
                float ForegroundG = (float)((ForegroundArgb >> 8) & 0xFF) * (1.0f / 255.0f);
                float ForegroundB = (float)((ForegroundArgb >> 16) & 0xFF) * (1.0f / 255.0f);

                for(int Y = 0; Y < ScissoredHeight; ++Y) {
                    for(int X = 0; X < ScissoredWidth; ++X) {
                        uint32_t *Dest = &Pixels[(OutY + Y) * Width + OutX + X];
                        uint32_t Source = CachedGlyph->Data[(SourceOffsetY + Y) * GLYPH_TEXTURE_SIZE + (SourceOffsetX + X)];

                        if(Source) {
                            assert((OutY + Y) < TextAreaHeight);
                            assert((OutX + X) < TextAreaWidth);
                            assert((SourceOffsetY + Y) < GLYPH_TEXTURE_SIZE);
                            assert((SourceOffsetX + X) < GLYPH_TEXTURE_SIZE);

                            uint32_t DestArgb = *Dest;
                            float DestR = (float)(DestArgb & 0xFF) * (1.0f / 255.0f);
                            float DestG = (float)((DestArgb >> 8) & 0xFF) * (1.0f / 255.0f);
                            float DestB = (float)((DestArgb >> 16) & 0xFF) * (1.0f / 255.0f);

                            float Alpha = (float)Source * (1.0f / 255.0f);
                            float R = Alpha * ForegroundR;
                            float G = Alpha * ForegroundG;
                            float B = Alpha * ForegroundB;
                            float OneMinusAlpha = 1.0f - Alpha;

                            float NewR = MINIMUM(DestR * OneMinusAlpha + R, 1.0f);
                            float NewG = MINIMUM(DestG * OneMinusAlpha + G, 1.0f);
                            float NewB = MINIMUM(DestB * OneMinusAlpha + B, 1.0f);

                            uint32_t New = (uint32_t)(NewR * 255.0f) |
                                           ((uint32_t)(NewG * 255.0f) << 8) |
                                           ((uint32_t)(NewB * 255.0f) << 16) |
                                           0xFF000000;
                            *Dest = New;
                        }
                    }
                }
            }
        }
    }
}

static void AppDrawAndPresent(app_state* App) {
    void* Texels;

    int Pitch;
    SDL_LockTexture(App->Texture, NULL, &Texels, &Pitch);
    AppDraw(App, Texels, (int)App->TextureWidth, (int)App->TextureHeight, &App->Editor);
    SDL_UnlockTexture(App->Texture);

    // For resizing to be smooth and not-too-glitchy, we appear to have to clear the renderer (even though we overwrite the entire screen).
    SDL_SetRenderDrawColor(App->Renderer, 0, 0, 0, 255);
    SDL_RenderClear(App->Renderer);
    SDL_RenderTexture(App->Renderer, App->Texture, NULL, NULL);
    SDL_RenderPresent(App->Renderer);
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char *argv[]) {
    (void)argc; (void)argv;

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL could not initialize! SDL_Error: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");

    // Allocate app state
    app_state *App = SDL_calloc(1, sizeof(app_state));
    if (!App) {
        SDL_Log("Could not allocate app state!");
        return SDL_APP_FAILURE;
    }

    App->FontPixelHeight = 24;

    App->Style.BackgroundColor = 0xFFEAFFFF;
    App->Style.ForegroundColor = 0xFF000000;
    App->Style.CursorColor = 0xFF000000;
    App->Style.SelectionBackgroundColor = 0xFF000000;
    App->Style.SelectionForegroundColor = 0xFFEAFFFF;
    App->Style.ScrollbarThickness = 24;

    {
        App->CachedGlyphs = SDL_calloc(1, sizeof(cached_glyph) * GLYPH_CACHE_CAPACITY);
        uint8_t *TextureData = SDL_calloc(1, sizeof(uint8_t) * GLYPH_TEXTURE_SIZE * GLYPH_TEXTURE_SIZE * GLYPH_CACHE_CAPACITY);
        for (int CachedGlyphIndex = 0; CachedGlyphIndex < GLYPH_CACHE_CAPACITY; ++CachedGlyphIndex) {
            cached_glyph *Entry = &App->CachedGlyphs[CachedGlyphIndex];

            Entry->Data = TextureData;
            TextureData += GLYPH_TEXTURE_SIZE * GLYPH_TEXTURE_SIZE;
        }
    }

    int Width = 800;
    int Height = 800;
    App->Frame = 0;

    // Create window
    App->Window = SDL_CreateWindow(
        "refpad",
        Width,
        Height,
        SDL_WINDOW_RESIZABLE
    );

    if (!App->Window) {
        SDL_Log("Window could not be created. SDL_Error: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    App->Renderer = SDL_CreateRenderer(App->Window, "software");
    if (!App->Renderer) {
        SDL_Log("Renderer could not be created. SDL_Error: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_SetRenderVSync(App->Renderer, 1)) {
        SDL_Log("Could not enable VSync. SDL error: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    App->ArrowCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    App->TextCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    if (App->TextCursor) {
        SDL_SetCursor(App->TextCursor);
    }

    AppResize(App, Width, Height);

    if (!App->Texture) {
        SDL_Log("Texture could not be created. SDL_Error: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    *appstate = App;

    return SDL_APP_CONTINUE;
}

static int QuickPaste(app_state *App, SDL_Event* Event) {
    if (Event->type == SDL_EVENT_KEY_DOWN) {
        if ((Event->key.mod & SDL_KMOD_CTRL) && Event->key.key >= SDLK_0 && Event->key.key <= SDLK_9) {
            const char *Text = (const char *)QuickPastePalette[Event->key.key - SDLK_0];
            InsertText(&App->Editor, Text, (int)StringLength(Text), 0);
            return 1;
        }
    }
    return 0;
}

static int ShiftIsDown = 0;
static int ControlIsDown = 0;

static ui_element_id UiElementIdForMousePosition(app_state *App, float X, float Y) {
    ui_element_id Result = 0;

    float TextAreaWidth = App->TextureWidth - (float)App->Style.ScrollbarThickness;
    float TextAreaHeight = App->TextureHeight - (float)App->Style.ScrollbarThickness;
    float ScrollbarThickness = (float)App->Style.ScrollbarThickness;

    if (X > TextAreaWidth) {
        if (Y < ScrollbarThickness) {
            Result = UI_ELEMENT_ID_SCROLL_UP;
        } else if (Y < (App->TextureHeight - ScrollbarThickness * 2)) {
            Result = UI_ELEMENT_ID_VERTICAL_SCROLLBAR;
        } else if (Y < (App->TextureHeight - ScrollbarThickness)) {
            Result = UI_ELEMENT_ID_SCROLL_DOWN;
        }
    } else if (Y > TextAreaHeight) {
        if (X < ScrollbarThickness) {
            Result = UI_ELEMENT_ID_SCROLL_LEFT;
        } else if (X < (App->TextureWidth - ScrollbarThickness * 2)) {
            Result = UI_ELEMENT_ID_HORIZONTAL_SCROLLBAR;
        } else if (X < (App->TextureWidth - ScrollbarThickness)) {
            Result = UI_ELEMENT_ID_SCROLL_RIGHT;
        }
    } else {
        Result = UI_ELEMENT_ID_TEXT_AREA;
    }

    return Result;
}

static void ChangeFontSize(app_state *App, int Delta) {
    App->CachedGlyphCount = 0;
    App->FontPixelHeight += Delta * 8;
    App->FontPixelHeight = MAXIMUM(8, MINIMUM(GLYPH_TEXTURE_SIZE, App->FontPixelHeight));
}

SDL_AppResult SDL_AppEvent(void *AppState, SDL_Event *Event) {
    app_state *App = (app_state *)AppState;

    // Quick paste for testing
    if (QuickPaste(App, Event))
        return SDL_APP_CONTINUE;

    switch (Event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_MOUSE_MOTION: {
        if (App->ScrollingX) {
            float Thickness = (float)App->Style.ScrollbarThickness;
            float ScrollbarSize = (float)(App->ScrollMaxX - App->ScrollMinX);
            float ScrollAreaMinX = Thickness;
            float ScrollAreaMaxX = App->TextureWidth - Thickness * 2 - ScrollbarSize;
            float ScrollX = MAXIMUM(0, MINIMUM(1, (Event->motion.x - App->ScrollOffset - ScrollAreaMinX) / (ScrollAreaMaxX - ScrollAreaMinX)));
            editor_command Command = { 0 };
            Command.Type = EDITOR_COMMAND_SCROLL_ABSOLUTE_01;
            Command.X = ScrollX;
            Command.Axis = EDITOR_AXIS_X;
            DoCommand(&App->Editor, Command);
        } else if (App->ScrollingY) {
            float Thickness = (float)App->Style.ScrollbarThickness;
            float ScrollbarSize = (float)(App->ScrollMaxY - App->ScrollMinY);
            float ScrollAreaMinY = Thickness;
            float ScrollAreaMaxY = App->TextureHeight - Thickness * 2 - ScrollbarSize;
            float ScrollY = MAXIMUM(0, MINIMUM(1, (Event->motion.y - App->ScrollOffset - ScrollAreaMinY) / (ScrollAreaMaxY - ScrollAreaMinY)));
            editor_command Command = { 0 };
            Command.Type = EDITOR_COMMAND_SCROLL_ABSOLUTE_01;
            Command.Y = ScrollY;
            Command.Axis = EDITOR_AXIS_Y;
            DoCommand(&App->Editor, Command);
        } else if ((Event->motion.state & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) &&
                   (UiElementIdForMousePosition(App, Event->motion.x, Event->motion.y) == UI_ELEMENT_ID_TEXT_AREA)) {
            editor_command Command = { 0 };
            Command.Type = EDITOR_COMMAND_MOUSE_MOVE;
            Command.X = Event->motion.x;
            Command.Y = Event->motion.y;
            Command.SelectionActive = 1;
            DoCommand(&App->Editor, Command);
        }
    } break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (Event->button.button == SDL_BUTTON_LEFT) {
            ui_element_id ElementId = UiElementIdForMousePosition(App, Event->button.x, Event->button.y);
            editor_command Command = { 0 };

            switch (ElementId) {
            case UI_ELEMENT_ID_TEXT_AREA:
                Command.Type = EDITOR_COMMAND_MOUSE_PRESS;
                Command.X = Event->button.x;
                Command.Y = Event->button.y;
            break;

            case UI_ELEMENT_ID_SCROLL_UP:
            case UI_ELEMENT_ID_SCROLL_DOWN:
                Command.Type = EDITOR_COMMAND_SCROLL;
                Command.Y = (float)((ElementId == UI_ELEMENT_ID_SCROLL_UP) ? PIXELS_PER_SCROLL_BUTTON_PRESS : -PIXELS_PER_SCROLL_BUTTON_PRESS);
            break;

            case UI_ELEMENT_ID_SCROLL_LEFT:
            case UI_ELEMENT_ID_SCROLL_RIGHT:
                Command.Type = EDITOR_COMMAND_SCROLL;
                Command.X = (float)((ElementId == UI_ELEMENT_ID_SCROLL_LEFT) ? -PIXELS_PER_SCROLL_BUTTON_PRESS : PIXELS_PER_SCROLL_BUTTON_PRESS);
            break;

            case UI_ELEMENT_ID_HORIZONTAL_SCROLLBAR: {
                float ScrollOffset = Event->button.x - (float)App->ScrollMinX;
                if ((ScrollOffset >= 0) && (Event->button.x < (float)App->ScrollMaxX)) {
                    App->ScrollingX = 1;
                    App->ScrollOffset = ScrollOffset;
                }
            } break;

            case UI_ELEMENT_ID_VERTICAL_SCROLLBAR: {
                float ScrollOffset = Event->button.y - (float)App->ScrollMinY;
                if ((ScrollOffset >= 0) && (Event->button.y < (float)App->ScrollMaxY)) {
                    App->ScrollingY = 1;
                    App->ScrollOffset = ScrollOffset;
                }
            } break;
            }

            if (Command.Type != 0) {
                DoCommand(&App->Editor, Command);
            }
        }
    } break;

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (Event->button.button == SDL_BUTTON_LEFT) {
            if (App->ScrollingX) {
                App->ScrollingX = 0;
            } else if (App->ScrollingY) {
                App->ScrollingY = 0;
            } else if (UiElementIdForMousePosition(App, Event->button.x, Event->button.y) == UI_ELEMENT_ID_TEXT_AREA) {
                editor_command Command = { 0 };
                Command.Type = EDITOR_COMMAND_MOUSE_RELEASE;
                Command.X = Event->button.x;
                Command.Y = Event->button.y;
                DoCommand(&App->Editor, Command);
            }
        }
    } break;

    case SDL_EVENT_MOUSE_WHEEL: {
        editor_command Command = { 0 };
        if (ControlIsDown) {
            ChangeFontSize(App, (int)Event->wheel.y);
        } else {
            Command.Type = EDITOR_COMMAND_SCROLL;
            Command.X = Event->wheel.x * PIXELS_PER_SCROLL_TICK;
            Command.Y = Event->wheel.y * PIXELS_PER_SCROLL_TICK;
            if (ShiftIsDown) {
                Command.X = -Command.Y;
                Command.Y = 0;
            }
        }
        DoCommand(&App->Editor, Command);
    } break;

    case SDL_EVENT_WINDOW_RESIZED:
        AppResize(App, Event->window.data1, Event->window.data2);

        if (!App->Texture) {
            SDL_Log("Could not recreate texture.");
            return SDL_APP_FAILURE;
        }

        break;

    case SDL_EVENT_TEXT_INPUT:
        InsertText(&App->Editor, Event->text.text, (int)StringLength(Event->text.text), 0);
        break;

    case SDL_EVENT_TEXT_EDITING: {
        // There are a bunch of issues with IME. At least on Gnome.
        // - The on-screen keyboard does not pop up. It stays on screen if we switch from another window that made it pop up, though.
        // - Gnome (or, at the very least, SDL-on-Gnome) sends every TEXT_EDITING event twice, once on press, another on "release".
        // - When spacebar or return are hit, we do receive TEXT_INPUT, but we are not getting backspace nor arrow presses.
        // - If we click an autocomplete suggestion, the chain of events that happens is:
        // -   TEXT_EDITING: pro
        // -   (Click "programming")
        // -   TEXT_INPUT: programming
        // -   TEXT_EDITING: pro
        // -   SDL_AppIterate (!)
        // -   TEXT_EDITING:
        // - So an additional "pro" hangs around for one more frame before disappearing.
        size_t TextLength = StringLength(Event->edit.text);
        int Cursor = Event->edit.start;
        int SelectionLength = Event->edit.length;
        if (Cursor < 0) {
            Cursor = (int)TextLength - 1;
        }
        if (SelectionLength < 0) {
            SelectionLength = 0;
        }

        ImeCompose(&App->Editor, Event->edit.text, (int)TextLength, Cursor, SelectionLength);
    } break;

    case SDL_EVENT_KEY_DOWN: {
        editor_command Command = { 0 };
        Command.SelectionActive = Event->key.mod & SDL_KMOD_SHIFT;
        switch (Event->key.key) {
            case SDLK_ESCAPE:
                return SDL_APP_SUCCESS;

            case SDLK_LSHIFT:
            case SDLK_RSHIFT: {
                ShiftIsDown = 1;
            } break;

            case SDLK_LCTRL:
            case SDLK_RCTRL: {
                ControlIsDown = 1;
            } break;

            case SDLK_HOME: Command.Type = EDITOR_COMMAND_HOME; break;
            case SDLK_END: Command.Type = EDITOR_COMMAND_END; break;
            case SDLK_PAGEUP: Command.Type = EDITOR_COMMAND_PAGEUP; break;
            case SDLK_PAGEDOWN: Command.Type = EDITOR_COMMAND_PAGEDOWN; break;

            case SDLK_UP:
                Command.Type = (ControlIsDown) ? EDITOR_COMMAND_PREV_PARAGRAPH : EDITOR_COMMAND_UP;
            break;

            case SDLK_DOWN:
                Command.Type = (ControlIsDown) ? EDITOR_COMMAND_NEXT_PARAGRAPH : EDITOR_COMMAND_DOWN;
            break;

            case SDLK_LEFT:
                Command.Type = (ControlIsDown) ? EDITOR_COMMAND_PREV_WORD : EDITOR_COMMAND_LEFT;
            break;

            case SDLK_RIGHT:
                Command.Type = (ControlIsDown) ? EDITOR_COMMAND_NEXT_WORD : EDITOR_COMMAND_RIGHT;
            break;

            case SDLK_DELETE: {
                Command.Type = (ControlIsDown) ? EDITOR_COMMAND_DELETE_WORD : EDITOR_COMMAND_DELETE;
            } break;

            case SDLK_BACKSPACE: {
                Command.Type = (ControlIsDown) ? EDITOR_COMMAND_BACKSPACE_WORD : EDITOR_COMMAND_BACKSPACE;
            } break;

            case SDLK_EQUALS:
                if (ControlIsDown) {
                    ChangeFontSize(App, 1);
                }
            break;

            case SDLK_MINUS:
                if (ControlIsDown) {
                    ChangeFontSize(App, -1);
                }
            break;

            case SDLK_A:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    SelectAllText(&App->Editor);
                }
            break;

            case SDLK_B:
            case SDLK_I:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    ToggleSelectionStyle(&App->Editor, (Event->key.key == SDLK_B) ? TEXT_STYLE_BOLD : TEXT_STYLE_ITALIC);
                }
            break;

            case SDLK_C:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    uint8_t* Text = GetSelectedText(&App->Editor);
                    if (Text) {
                        SDL_SetClipboardText((char *)Text);
                        free(Text);
                    }
                }
            break;

            case SDLK_R:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    Command.Type = EDITOR_COMMAND_TOGGLE_NEWLINE_DISPLAY;
                }
            break;

            case SDLK_X:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    // Identical to Copy above...
                    uint8_t* Text = GetSelectedText(&App->Editor);
                    if (Text) {
                        SDL_SetClipboardText((char *)Text);
                        free(Text);
                    }
                    // Maybe this should be COMMAND_DELETE?
                    DeleteSelectedText(&App->Editor);
                }
            break;

            case SDLK_V:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    char* ClipboardText = SDL_GetClipboardText();
                    if (ClipboardText) {
                        InsertText(&App->Editor, ClipboardText, (int)StringLength(ClipboardText), 0);
                    }
                    SDL_free(ClipboardText);
                }
            break;

            case SDLK_W:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    Command.Type = EDITOR_COMMAND_TOGGLE_LINE_WRAP;
                }
            break;

            case SDLK_Z:
            case SDLK_Y:
                if (Event->key.mod & SDL_KMOD_CTRL) {
                    Command.Type = (Event->key.key == SDLK_Z) ? EDITOR_COMMAND_UNDO : EDITOR_COMMAND_REDO;
                }
            break;

            case SDLK_RETURN: {
                character Char = ZERO;
                Char.Codepoint = '\n';
                InsertCharacter(&App->Editor, Char);
            } break;
        }

        if (Command.Type != EDITOR_COMMAND_NONE) {
            DoCommand(&App->Editor, Command);
        }
    } break;

    case SDL_EVENT_KEY_UP: {
        switch (Event->key.key) {
            case SDLK_LSHIFT:
            case SDLK_RSHIFT: {
                ShiftIsDown = 0;
            } break;

            case SDLK_LCTRL:
            case SDLK_RCTRL: {
                ControlIsDown = 0;
            } break;
        }
    } break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *AppState) {
    app_state *App = (app_state *)AppState;

    AppDrawAndPresent(App);

    App->Frame++;

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
}
