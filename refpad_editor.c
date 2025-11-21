
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <float.h> // for FLT_MAX

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wimplicit-float-conversion"
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wno-unused-function"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define KB_TEXT_SHAPE_IMPLEMENTATION
#define KB_TEXT_SHAPE_STATIC
#include "kb_text_shape.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __cplusplus
#define ZERO {}
#else
#define ZERO {0}
#endif

#define POINTER_OFFSET(Type, Base, Offset) (Type *)((char *)(Base) + (Offset))

#define MINIMUM(A, B) (((A) < (B)) ? (A) : (B))
#define MAXIMUM(A, B) (((A) < (B)) ? (B) : (A))

#define TEXT_CAPACITY (1024*1024)
#define LINE_CAPACITY 65536

//
// Arena
//

typedef struct arena
{
    char *Base;
    char *At;
    char *End;
} arena;

typedef struct arena_lifetime
{
    arena *Arena;
    char *At;
} arena_lifetime;

// 1GB ought to be enough for anybody..?
#define ARENA_CAPACITY (1024ull * 1024ull * 1024ull)

static void EnsureArenaInitialized(arena *Arena)
{    
    if(!Arena->Base)
    {
        Arena->Base = malloc(ARENA_CAPACITY);
        Arena->At = Arena->Base;
        Arena->End = Arena->Base + ARENA_CAPACITY;
    }
}

static void *PushSize(arena *Arena, size_t Size, int DoNotZero)
{
    EnsureArenaInitialized(Arena);
    void *Result = 0;

    char *At = Arena->At;
    char *NewAt = At + Size;
    if(NewAt < Arena->End)
    {
        Result = At;
        Arena->At = NewAt;
    }

    if(!DoNotZero)
    {
        memset(Result, 0, Size);
    }

    return Result;
}
#define PushType(Arena, Type, DoNotZero) (Type *)PushSize((Arena), sizeof(Type), (DoNotZero))
#define PushArray(Arena, Type, Count, DoNotZero) (Type *)PushSize((Arena), sizeof(Type) * (Count), (DoNotZero))

static arena_lifetime ArenaBeginLifetime(arena *Arena)
{
    arena_lifetime Result = ZERO;
    Result.Arena = Arena;
    if(Arena)
    {
        Result.At = Arena->At;
    }
    return Result;
}

static void ArenaEndLifetime(arena_lifetime *Lifetime)
{
    if(Lifetime->Arena)
    {
        Lifetime->Arena->At = Lifetime->At;
    }
}

//
// Ring allocator
//

typedef struct ring_allocator
{
    char *Base;
    char *At;
    char *End;

    int WraparoundCount;
} ring_allocator;

typedef struct ring_allocation
{
    void *Memory;
    int Wraparound;
} ring_allocation;

static ring_allocator RingAllocatorInit(void *Memory, size_t Size)
{
    ring_allocator Result = {0};
    Result.Base = (char *)Memory;
    Result.At = Result.Base;
    Result.End = Result.Base + Size;
    return Result;
}

static ring_allocation RingAllocatorAlloc(ring_allocator *Alloc, size_t Size)
{
    ring_allocation Result = {0};

    if(Size <= (size_t)(Alloc->End - Alloc->Base))
    {
        if((Alloc->At + Size) <= Alloc->End)
        {
            Result.Memory = Alloc->At;
        }
        else
        {
            Result.Memory = Alloc->Base;
            Alloc->WraparoundCount += 1;
        }

        Alloc->At = (char *)Result.Memory + Size;
        Result.Wraparound = Alloc->WraparoundCount;
    }

    return Result;
}

static void RingAllocatorRewind(ring_allocator *Alloc, ring_allocation *Allocation)
{
    Alloc->At = (char *)Allocation->Memory;
    Alloc->WraparoundCount = Allocation->Wraparound;
}

static int RingAllocationIsValid(ring_allocator *Alloc, ring_allocation *Allocation)
{
    int Result = Allocation->Memory &&
                 ((((char *)Allocation->Memory < Alloc->At) &&
                   (Allocation->Wraparound == Alloc->WraparoundCount)) ||
                  (((char *)Allocation->Memory >= Alloc->At) &&
                   ((Allocation->Wraparound + 1) == Alloc->WraparoundCount)));
    return Result;
}

//
//
//

typedef uint32_t text_alignment;
enum text_alignment_enum
{
    TEXT_ALIGNMENT_DONT_KNOW,
    TEXT_ALIGNMENT_LEFT,
    TEXT_ALIGNMENT_RIGHT,
    TEXT_ALIGNMENT_CENTER,

    TEXT_ALIGNMENT_COUNT,
};

#define MAX_CODEPOINTS_PER_CELL 8
typedef struct cell_cache_entry
{
    uint32_t Codepoints[MAX_CODEPOINTS_PER_CELL];
    uint32_t CodepointCount;

    uint8_t *Pixels; // [CellWidth * CellHeight]
} cell_cache_entry;

typedef struct font
{
    kbts_font Kbts;
    stbtt_fontinfo Stbtt;
    kbts_font_style_flags StyleFlags;
} font;

typedef uint32_t text_style;
enum
{
    TEXT_STYLE_REGULAR,
    TEXT_STYLE_ITALIC,
    TEXT_STYLE_BOLD,
    TEXT_STYLE_BOLD_ITALIC,

    TEXT_STYLE_COUNT,
};

// Finest grain atom of editing. Something like a grapheme cluster.
typedef struct character {
    int Codepoint;
    text_style Style;

    // Filled in by the editor.
    kbts_break_flags BreakFlags;

    // #TODO: Going to get much fatter.
    // Marks And Stuff
    // Color
} character;

typedef struct draw_box
{
    // Bounding box, expressed as an open interval [Min,Max)
    float MinX;
    float MinY;
    float MaxX;
    float MaxY;
} draw_box;

// keeba->AM: I have replaced draw_selection with draw_box for now.
// Feel free to re-use draw_selection if each selection needs to hold more data.
#if 0
typedef struct draw_selection
{
    // Bounding box of the selection, expressed as an open interval [Min,Max)
    draw_box Box;
} draw_selection;
#endif

typedef struct edit_line
{
    draw_box GlyphBox;

    int MinCodepointIndex;
    int MaxCodepointIndex;

    int FirstCommandIndex;
    int OnePastLastCommandIndex;

    int FirstSelectionIndex;
    int OnePastLastSelectionIndex;

    kbts_direction Direction;

    text_alignment PreferredAlignment;
    text_alignment ActualAlignment; // If PreferredAlignment is DONT_KNOW, we infer alignment from the text.
} edit_line;

static draw_box DrawBoxUnion(float Ax0, float Ay0, float Ax1, float Ay1, float Bx0, float By0, float Bx1, float By1)
{
    draw_box Result;
    Result.MinX = MINIMUM(Ax0, Bx0);
    Result.MinY = MINIMUM(Ay0, By0);
    Result.MaxX = MAXIMUM(Ax1, Bx1);
    Result.MaxY = MAXIMUM(Ay1, By1);
    return Result;
}

static int BoxOverlap(float Ax0, float Ay0, float Ax1, float Ay1, float Bx0, float By0, float Bx1, float By1)
{
    int Result = (Ax1 >= Bx0) &&
                 (Ax0 < Bx1) &&
                 (By1 >= By0) &&
                 (By0 < By1);
    return Result;
}

static draw_box InvalidDrawBox(void)
{
    draw_box Result;
    Result.MinX = +FLT_MAX;
    Result.MaxX = -FLT_MAX;
    Result.MinY = +FLT_MAX;
    Result.MaxY = -FLT_MAX;
    return Result;
}

enum editor_command_type {
    EDITOR_COMMAND_NONE,
    EDITOR_COMMAND_LEFT,
    EDITOR_COMMAND_RIGHT,
    EDITOR_COMMAND_UP,
    EDITOR_COMMAND_DOWN,
    EDITOR_COMMAND_PREV_PARAGRAPH,
    EDITOR_COMMAND_NEXT_PARAGRAPH,
    EDITOR_COMMAND_PREV_WORD,
    EDITOR_COMMAND_NEXT_WORD,
    EDITOR_COMMAND_HOME,
    EDITOR_COMMAND_END,
    EDITOR_COMMAND_PAGEUP,
    EDITOR_COMMAND_PAGEDOWN,
    EDITOR_COMMAND_DELETE,
    EDITOR_COMMAND_DELETE_WORD,
    EDITOR_COMMAND_BACKSPACE,
    EDITOR_COMMAND_BACKSPACE_WORD,
    EDITOR_COMMAND_MOUSE_PRESS,
    EDITOR_COMMAND_MOUSE_MOVE,
    EDITOR_COMMAND_MOUSE_RELEASE,
    EDITOR_COMMAND_SCROLL,
    EDITOR_COMMAND_SCROLL_ABSOLUTE_01,
    EDITOR_COMMAND_UNDO,
    EDITOR_COMMAND_REDO,
    EDITOR_COMMAND_TOGGLE_LINE_WRAP,
    EDITOR_COMMAND_TOGGLE_NEWLINE_DISPLAY,
};

typedef uint32_t editor_axis;
enum editor_axis_enum {
    EDITOR_AXIS_NONE,
    EDITOR_AXIS_X,
    EDITOR_AXIS_Y,
};

typedef struct editor_command {
    int Type;
    int SelectionActive;
    // Multiline cursor extend/shrink.
    float X; // Used for mouse position and scrolling.
    float Y; // Used for mouse position and scrolling.
    editor_axis Axis; // Used for absolute scrolling.
} editor_command;

typedef struct draw_cursor
{
    // The scaled position of the cursor.
    float X;
    float Y;
} draw_cursor;

typedef uint32_t draw_command_flags;
enum draw_command_flags_enum
{
    DRAW_COMMAND_FLAG_NONE,
    DRAW_COMMAND_FLAG_SELECTED = (1 << 0),
    DRAW_COMMAND_FLAG_VISIBLE = (1 << 1),
};

typedef struct draw_command
{
    font *Font;
    int GlyphIndex;
    int CodepointIndex;
    float X;
    float Y;
    float Scale;
    draw_command_flags Flags;

    // These aren't strictly necessary, but useful for hit-testing and/or placeholders.
    float ScaledWidth;
    float ScaledHeight;
} draw_command;

typedef struct draw_command_list
{
    draw_command *Commands;
    size_t Count;
    size_t Capacity;

    draw_box *Selections;
    size_t SelectionsCount;
    size_t SelectionsCapacity;

    // In [0, 1]. Useful for drawing the scrollbar.
    float ScrollMinX;
    float ScrollMaxX;
    float ScrollMinY;
    float ScrollMaxY;

    draw_cursor Cursor;
    // The cursor position is in codepoints. However, glyph substitutions are free to
    // delete and number of glyphs, so we need to robustly snap the cursor to the highest-index
    // codepoint that still belongs before the cursor.
    int ClosestCodepointIndexToCursorPlusOne;
} draw_command_list;

typedef uint32_t editor_flags;
enum editor_flags_enum
{
    EDITOR_FLAG_NONE,

    EDITOR_FLAG_KEEP_DESIRED_X = (1 << 0),
    EDITOR_FLAG_KEEP_DESIRED_Y = (1 << 1),
    EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR = (1 << 2),
    EDITOR_FLAG_WRAP_LINES = (1 << 3),
    EDITOR_FLAG_DISPLAY_NEWLINES = (1 << 4),
};

typedef struct edit_position
{
    int CodepointIndex;
    int LineIndex;

    // X coordinate to snap the cursor to when moving it vertically.
    // Set by horizontal cursor movement.
    float DesiredX;

    int DesiredY;
} edit_position;

typedef struct undo_state_header undo_state_header;
struct undo_state_header
{
    undo_state_header *Prev;
    undo_state_header *Next;
};

typedef struct undo_state
{
    undo_state_header Header;
    
    ring_allocation Allocation;

    character* Text;
    int TextLength;

    float TargetScrollX;
    float TargetScrollY;

    edit_line *Lines;
    int LineCount;

    edit_position CursorPosition;
    edit_position SelectionPosition;
} undo_state;

typedef struct layout_glyph
{
    font *Font;
    int Id;
    int CodepointIndex;
    kbts_direction Direction;
    int AdvanceX;
    int AdvanceY;
    int OffsetX;
    int OffsetY;
    float Scale;
    kbts_break_flags BreakFlags;
    int NoShapeBreak;
    int IsNewline;
} layout_glyph;

#define MAX_FONT_COUNT 32
typedef struct editor
{
    arena Arena;
    arena_lifetime FrameLifetime;
    ring_allocator UndoAllocator;

    int FrameBufferWidth;

    editor_flags Flags;

    undo_state_header UndoSentinel;
    undo_state_header *UndoCursor;

    int MouseX;
    int MouseY;

    float RunningAdvance; // X cursor for wrapping.
    float CursorY;

    float CurrentScrollX;
    float CurrentScrollY;
    float TargetScrollX;
    float TargetScrollY;
    float MaxScrollX;
    float MaxScrollY;

    int FontCount;

    int Ascent;
    int Descent;
    int LineGap;
    int LineHeight;

    int FontPixelHeight;

    kbts_shape_context *KbtsContext;

    edit_line *Lines;
    int LineCount;
    int LineCapacity;

    layout_glyph *LineGlyphs;
    int LineGlyphCount;
    int LineGlyphCapacity;

    draw_box TextBounds;

    draw_command_list DrawList;

    character* Text;
    int TextLength;
    int TextCapacity;

    int FrameBufferHeight;
    int TotalHeightInPixels;

    int ImeStartCodepointIndex;
    int ImeLength;

    edit_position CursorPosition;
    edit_position SelectionPosition;

    int FontIndicesByPreference[TEXT_STYLE_COUNT][MAX_FONT_COUNT];
    font Fonts[MAX_FONT_COUNT];
} editor;

static float ClampFloat(float X, float Min, float Max)
{
    float Result = X;

    if(Result < Min)
    {
        Result = Min;
    }
    if(Result > Max)
    {
        Result = Max;
    }

    return Result;
}

static font *PushFont(editor *Editor, const char *Path)
{
    font *Result = 0;
    if(Editor->FontCount < MAX_FONT_COUNT)
    {
        Result = &Editor->Fonts[Editor->FontCount];

        void *FontData;
        int FontSize;
        // @Memory: We could use the arena here.
        Result->Kbts = kbts_FontFromFile(Path, 0, 0, 0, &FontData, &FontSize);

        if(kbts_FontIsValid(&Result->Kbts))
        {
            stbtt_InitFont(&Result->Stbtt, (unsigned char *)FontData, stbtt_GetFontOffsetForIndex((unsigned char *)FontData, 0));

            kbts_font_info Info;
            kbts_GetFontInfo(&Result->Kbts, &Info);
            Result->StyleFlags = Info.StyleFlags;

            Editor->FontCount += 1;
        }
    }

    return Result;
}

static inline font *KbtsFontToFont(kbts_font *Kbts)
{
    // All of our kbts fonts are inside of font structs, so this becomes a pointer offset.
    font *Result = POINTER_OFFSET(font, Kbts, -(int)offsetof(font, Kbts));
    return Result;
}

static int GetSelectionStart(editor* Editor) {
    if (Editor->SelectionPosition.CodepointIndex > Editor->CursorPosition.CodepointIndex)
        return Editor->CursorPosition.CodepointIndex;
    else
        return Editor->SelectionPosition.CodepointIndex;
}
static int GetSelectionEnd(editor* Editor) {
    if (Editor->SelectionPosition.CodepointIndex > Editor->CursorPosition.CodepointIndex)
        return Editor->SelectionPosition.CodepointIndex;
    else
        return Editor->CursorPosition.CodepointIndex;
}
static int IsCharacterSelected(editor* Editor, int CharacterIdx) {
    return CharacterIdx >= GetSelectionStart(Editor) && CharacterIdx < GetSelectionEnd(Editor);
}

static void EditorBeginLines(editor *Editor)
{
    Editor->LineCount = 0;
    Editor->TextBounds = InvalidDrawBox();
}

#define INVALID_CODEPOINT_INDEX ~0u

static edit_line *EditorBeginLine(editor *Editor, draw_command_list *DrawList)
{
    edit_line *Line = 0;

    if(Editor->LineCount < LINE_CAPACITY)
    {
        Line = &Editor->Lines[Editor->LineCount];
        Line->GlyphBox = InvalidDrawBox();
        Line->FirstCommandIndex = (uint32_t)DrawList->Count;
        Line->OnePastLastCommandIndex = (uint32_t)DrawList->Count;
        Line->FirstSelectionIndex = (uint32_t)DrawList->SelectionsCount;
        Line->OnePastLastSelectionIndex = (uint32_t)DrawList->SelectionsCount;
        Line->MinCodepointIndex = INT_MAX;
        Line->MaxCodepointIndex = INT_MIN;
        Line->Direction = KBTS_DIRECTION_DONT_KNOW;
        Line->ActualAlignment = Line->PreferredAlignment;
    }

    Editor->LineGlyphCount = 0;

    return Line;
}

static void FlushLine(draw_command_list *DrawList, editor *Editor);

static void EditorEndLine(editor *Editor, draw_command_list *DrawList)
{
    if(Editor->LineCount < LINE_CAPACITY)
    {
        FlushLine(DrawList, Editor);

        edit_line *Line = &Editor->Lines[Editor->LineCount];
        Line->OnePastLastCommandIndex = (uint32_t)DrawList->Count;
        Line->OnePastLastSelectionIndex = (uint32_t)DrawList->SelectionsCount;

        Editor->TextBounds = DrawBoxUnion(Editor->TextBounds.MinX, Editor->TextBounds.MinY, Editor->TextBounds.MaxX, Editor->TextBounds.MaxY,
                                          Line->GlyphBox.MinX, Line->GlyphBox.MinY, Line->GlyphBox.MaxX, Line->GlyphBox.MaxY);
        Editor->LineCount += 1;

        Editor->CursorY += (float)Editor->LineHeight;
    }
    
    Editor->RunningAdvance = 0;
}

static edit_line *EditorNextLine(editor *Editor, draw_command_list *DrawList)
{
    EditorEndLine(Editor, DrawList);
    edit_line *Result = EditorBeginLine(Editor, DrawList);

    return Result;
}

static void EditorEndLines(editor *Editor, draw_command_list *DrawList)
{
    // Having an empty line at the end of text is more trouble than it's worth.
    if(Editor->LineGlyphCount)
    {
        EditorEndLine(Editor, DrawList);
    }
}

static int DrawBoxIsValid(draw_box *Box)
{
    // When flushing lines, we know the X bounds of selections, but not the Y bounds.
    // We use this function at flush time, so we only take X bounds into account.
    int Result = Box->MinX != FLT_MAX;
    return Result;
}

#define CURSOR_THICKNESS 4

static edit_line *GetCurrentLine(editor *Editor)
{
    assert(Editor->LineCount < Editor->LineCapacity);
    edit_line *Result = &Editor->Lines[Editor->LineCount];
    return Result;
}

static void FlushLine(draw_command_list *DrawList, editor *Editor)
{
    float CursorX = 0;
    float CursorY = Editor->CursorY;
    float AscentPx = (float)Editor->Ascent;

    edit_line *Line = GetCurrentLine(Editor);

    layout_glyph *LineGlyphs = Editor->LineGlyphs;
    if(Line->Direction == KBTS_DIRECTION_RTL)
    {
        LineGlyphs = Editor->LineGlyphs + Editor->LineGlyphCapacity - Editor->LineGlyphCount;
    }

    font *CurrentFont = 0;
    kbts_direction CurrentDirection = KBTS_DIRECTION_DONT_KNOW;
    draw_box Selection = InvalidDrawBox();

    for(int GlyphIndex = 0;
        GlyphIndex < Editor->LineGlyphCount;
        ++GlyphIndex)
    {
        layout_glyph *Glyph = &LineGlyphs[GlyphIndex];
        float Scale = Glyph->Scale;

        if(Glyph->Direction != CurrentDirection)
        {
            // There can be maximum one selection rectangle per direction break.
            // We have to switch to a new selection between direction breaks because of the
            // visual discontinuity between LTR and RTL text.

            if(DrawBoxIsValid(&Selection))
            {
                // If there's a selection that's valid, keep it.
                // #TODO: This is the natural place to give it height, which should be passed in.
                DrawList->Selections[DrawList->SelectionsCount++] = Selection;
            }

            Selection = InvalidDrawBox();
            CurrentDirection = Glyph->Direction;
        }

        if(Glyph->Font != CurrentFont)
        {
            CurrentFont = Glyph->Font;
        }

        Line->MinCodepointIndex = MINIMUM(Line->MinCodepointIndex, Glyph->CodepointIndex);
        Line->MaxCodepointIndex = MAXIMUM(Line->MaxCodepointIndex, Glyph->CodepointIndex);

        float AdvanceXPx = (float)Glyph->AdvanceX * Scale;
        float AdvanceYPx = (float)Glyph->AdvanceY * Scale;
        int DoNotDisplay = 0;
        if(!(Editor->Flags & EDITOR_FLAG_DISPLAY_NEWLINES))
        {
            DoNotDisplay = (Glyph->IsNewline != 0);
        }

        if(DoNotDisplay)
        {
            AdvanceXPx = 0;
        }

        if(Glyph->Font)
        {
            int MinX, MinY, MaxX, MaxY;
            stbtt_GetGlyphBitmapBoxSubpixel(&Glyph->Font->Stbtt, Glyph->Id, Scale, Scale, 0, 0, &MinX, &MinY, &MaxX, &MaxY);

            if(DoNotDisplay)
            {
                // We set the width to 0 here, which makes it so the glyph will never be marked as DRAW_COMMAND_FLAG_VISIBLE in
                // the layout pass.
                // Keeping the draw commands around as end-of-line sentinels is useful.
                MaxX = MinX;
            }

            float GlyphX = CursorX + (float)Glyph->OffsetX * Scale;
            float GlyphY = AscentPx + CursorY - (float)Glyph->OffsetY * Scale;
            float GlyphWidthPx = (float)(MaxX - MinX);
            float GlyphHeightPx = (float)(MaxY - MinY);

            draw_command DummyCommand;
            draw_command *Command = &DummyCommand;

            if(DrawList->Count < DrawList->Capacity)
            {
                Command = &DrawList->Commands[DrawList->Count++];
                Command->Font = Glyph->Font;
                Command->GlyphIndex = Glyph->Id;
                Command->CodepointIndex = Glyph->CodepointIndex;
                Command->X = GlyphX;
                Command->Y = GlyphY;
                Command->ScaledWidth = GlyphWidthPx;
                Command->ScaledHeight = GlyphHeightPx;
                Command->Scale = Scale;
                Command->Flags = 0;
            }

            Line->GlyphBox = DrawBoxUnion(Line->GlyphBox.MinX, Line->GlyphBox.MinY, Line->GlyphBox.MaxX, Line->GlyphBox.MaxY,
                                          GlyphX, GlyphY, GlyphX + GlyphWidthPx, GlyphY + GlyphHeightPx);

            // Expand the bounding box of the selection on this line by the glyph.
            // Have to do this before and after advance, for both min and max, because LTR and RTL advance in different directions,
            // but the MinX/MaxX are visually always left/right.
            if (IsCharacterSelected(Editor, Glyph->CodepointIndex)) {
                Selection.MinX = MINIMUM(CursorX, Selection.MinX);
                Selection.MaxX = MAXIMUM(CursorX, Selection.MaxX);
                Selection.MinX = MINIMUM(CursorX + AdvanceXPx, Selection.MinX);
                Selection.MaxX = MAXIMUM(CursorX + AdvanceXPx, Selection.MaxX);
                Selection.MinY = MINIMUM(CursorY, Selection.MinY);
                Selection.MaxY = -INFINITY; // Filled in the layout pass.

                Command->Flags |= DRAW_COMMAND_FLAG_SELECTED;
            }
        }

        // @Cleanup @Duplication: There are some annoying off-by-one differences in behavior we have to deal with here
        // depending on which direction the run is going.
        if(Glyph->Direction == KBTS_DIRECTION_LTR)
        {
            if ((Glyph->CodepointIndex <= Editor->CursorPosition.CodepointIndex) &&
                (!DrawList->ClosestCodepointIndexToCursorPlusOne ||
                ((Glyph->CodepointIndex + 1) > DrawList->ClosestCodepointIndexToCursorPlusOne))) {
                DrawList->ClosestCodepointIndexToCursorPlusOne = Glyph->CodepointIndex + 1;

                DrawList->Cursor.X = CursorX;
                DrawList->Cursor.Y = CursorY;

                if(!(Editor->Flags & EDITOR_FLAG_KEEP_DESIRED_X))
                {
                    Editor->CursorPosition.DesiredX = CursorX;
                    Editor->CursorPosition.LineIndex = Editor->LineCount;
                }
            }
        }
        else
        {
            if ((Glyph->CodepointIndex <= Editor->CursorPosition.CodepointIndex) &&
                (!DrawList->ClosestCodepointIndexToCursorPlusOne ||
                ((Glyph->CodepointIndex + 1) >= DrawList->ClosestCodepointIndexToCursorPlusOne))) {
                DrawList->ClosestCodepointIndexToCursorPlusOne = Glyph->CodepointIndex + 1;

                DrawList->Cursor.X = CursorX + AdvanceXPx;
                DrawList->Cursor.Y = CursorY - AdvanceYPx;

                if(!(Editor->Flags & EDITOR_FLAG_KEEP_DESIRED_X))
                {
                    Editor->CursorPosition.DesiredX = CursorX + AdvanceXPx;
                    Editor->CursorPosition.LineIndex = Editor->LineCount;
                }
            }
        }

        CursorX += AdvanceXPx;
        CursorY -= AdvanceYPx;
    }

    // @Duplication
    if(DrawBoxIsValid(&Selection))
    {
        // If there's a selection that's valid, keep it.
        // #TODO: This is the natural place to give it height, which should be passed in.
        DrawList->Selections[DrawList->SelectionsCount++] = Selection;
    }
}

static void FlushVisualGlyphs(draw_command_list *DrawList, editor *Editor, kbts_direction ParagraphDirection,
                              kbts_direction Direction, layout_glyph *Glyphs, int GlyphCount)
{
    edit_line *Line = GetCurrentLine(Editor);

    if(!Line->Direction)
    {
        Line->Direction = ParagraphDirection;

        if(!Line->ActualAlignment)
        {
            Line->ActualAlignment = (ParagraphDirection == KBTS_DIRECTION_RTL) ? TEXT_ALIGNMENT_RIGHT : TEXT_ALIGNMENT_LEFT;
        }
    }

    layout_glyph *From = Glyphs;
    layout_glyph *To = Editor->LineGlyphs + Editor->LineGlyphCount;
    if(Line->Direction == KBTS_DIRECTION_RTL)
    {
        To = Editor->LineGlyphs + Editor->LineGlyphCapacity - Editor->LineGlyphCount - GlyphCount;
    }

    memcpy(To, From, sizeof(*From) * GlyphCount);
    Editor->LineGlyphCount += GlyphCount;
}

static void FlushDirection(draw_command_list *DrawList, editor *Editor, kbts_direction ParagraphDirection,
                           kbts_direction Direction, layout_glyph *Glyphs, int GlyphCount, int GlyphCapacity)
{   
    float RunningAdvance = Editor->RunningAdvance;
    
    int Rtl = (Direction == KBTS_DIRECTION_RTL);
    if(Rtl)
    {
        Glyphs = Glyphs + GlyphCapacity - GlyphCount;
    }

    int StartIndex = 0;
    int DirectionIncrement = 1;
    if(Rtl)
    {
        StartIndex = GlyphCount - 1;
        DirectionIncrement = -1;
    }

    if(Editor->Flags & EDITOR_FLAG_WRAP_LINES)
    {
        int LastShapeBreakGlyphIndexPlusOne = 0;
        int LastSoftLineBreakGlyphIndexPlusOne = 0;
        int LastShapeBreakUserId = -1;

        for(int GlyphIndex = StartIndex;
            (GlyphIndex >= 0) && (GlyphIndex < GlyphCount);
            GlyphIndex += DirectionIncrement)
        {
            layout_glyph *Glyph = &Glyphs[GlyphIndex];

            if((GlyphIndex != StartIndex) &&
               (Glyph->CodepointIndex != LastShapeBreakUserId) &&
                !Glyph->NoShapeBreak)
            {
                if(Glyph->BreakFlags & KBTS_BREAK_FLAG_LINE_SOFT)
                {
                    LastSoftLineBreakGlyphIndexPlusOne = GlyphIndex + 1;
                }

                LastShapeBreakGlyphIndexPlusOne = GlyphIndex + 1;
                LastShapeBreakUserId = Glyph->CodepointIndex;
            }

            RunningAdvance += (float)Glyph->AdvanceX * Glyph->Scale;
            if(RunningAdvance > (float)Editor->FrameBufferWidth)
            {
                int LastBreakIndex = GlyphIndex;
                if(LastSoftLineBreakGlyphIndexPlusOne)
                {
                    LastBreakIndex = LastSoftLineBreakGlyphIndexPlusOne - 1;
                }
                else if(LastShapeBreakGlyphIndexPlusOne)
                {
                    LastBreakIndex = LastShapeBreakGlyphIndexPlusOne - 1;
                }

                int First = StartIndex;
                int OnePastLast = LastBreakIndex;
                if(Rtl)
                {
                    int Swap = First;
                    First = OnePastLast + 1;
                    OnePastLast = Swap + 1;
                }

                if(OnePastLast > First)
                {
                    FlushVisualGlyphs(DrawList, Editor, ParagraphDirection, Direction, Glyphs + First, OnePastLast - First);

                    StartIndex = LastBreakIndex;
                    LastSoftLineBreakGlyphIndexPlusOne = 0;
                    LastShapeBreakGlyphIndexPlusOne = 0;
                    RunningAdvance = 0;

                    // Go back to the last break and resume counting from there.
                    GlyphIndex = LastBreakIndex - 1;

                    EditorNextLine(Editor, DrawList);
                }
            }
        }
    }

    int First = StartIndex;
    int OnePastLast = GlyphCount;
    if(Rtl)
    {
        First = 0;
        OnePastLast = StartIndex + 1;
    }
    FlushVisualGlyphs(DrawList, Editor, ParagraphDirection, Direction, Glyphs + First, OnePastLast - First);

    Editor->RunningAdvance = RunningAdvance;
}

static float AlignmentOffsetXForLine(edit_line *Line, float TextWidth)
{
    float Result = 0;

    if((Line->ActualAlignment == TEXT_ALIGNMENT_CENTER) ||
       (Line->ActualAlignment == TEXT_ALIGNMENT_RIGHT))
    {
        float LineWidth = Line->GlyphBox.MaxX - Line->GlyphBox.MinX;
        Result = TextWidth - LineWidth;

        if(Line->ActualAlignment == TEXT_ALIGNMENT_CENTER)
        {
            Result *= 0.5f;
        }
    }

    return Result;
}

static draw_command_list Draw(editor *Editor, int FontPixelHeight, int FrameBufferWidth, int FrameBufferHeight)
{
    if(!Editor->KbtsContext)
    {
        size_t UndoMemorySize = 8 * 1024ull * 1024ull;
        Editor->UndoAllocator = RingAllocatorInit(PushSize(&Editor->Arena, UndoMemorySize, 1), UndoMemorySize);
        Editor->UndoSentinel.Prev = Editor->UndoSentinel.Next = &Editor->UndoSentinel;

        PushFont(Editor, "NotoSans-Regular.ttf");           // Latin Greek Cyrillic
        PushFont(Editor, "NotoSansHebrew-Regular.ttf");     // Latin                               Hebrew (+ maybe something else?)
        PushFont(Editor, "NotoSansMyanmar-Regular.ttf");    // Latin                       Myanmar
        PushFont(Editor, "NotoSansArabic-Regular.ttf");     // Latin       Cyrillic Arabic
        PushFont(Editor, "NotoSans-Italic.ttf");            // Latin Greek Cyrillic
        PushFont(Editor, "NotoSans-Bold.ttf");              // Latin Greek Cyrillic
        PushFont(Editor, "NotoSans-BoldItalic.ttf");        // Latin Greek Cyrillic

        int8_t FontPreference[TEXT_STYLE_COUNT][MAX_FONT_COUNT];

        for(int FontIndex = 0;
            FontIndex < Editor->FontCount;
            ++FontIndex)
        {
            kbts_font_info Info;
            kbts_GetFontInfo(&Editor->Fonts[FontIndex].Kbts, &Info);

            int RegularPreference = 0;
            int ItalicPreference = 0;
            int BoldPreference = 0;
            int BoldItalicPreference = 0;

            if(Info.StyleFlags & KBTS_FONT_STYLE_FLAG_ITALIC)
            {
                RegularPreference -= 1;
                ItalicPreference += 1;
                BoldPreference -= 1;
                BoldItalicPreference += 1;
            }

            if(Info.StyleFlags & KBTS_FONT_STYLE_FLAG_BOLD)
            {
                RegularPreference -= 1;
                ItalicPreference -= 1;
                BoldPreference += 1;
                BoldItalicPreference += 1;
            }

            FontPreference[TEXT_STYLE_REGULAR][FontIndex] = (int8_t)RegularPreference;
            FontPreference[TEXT_STYLE_ITALIC][FontIndex] = (int8_t)ItalicPreference;
            FontPreference[TEXT_STYLE_BOLD][FontIndex] = (int8_t)BoldPreference;
            FontPreference[TEXT_STYLE_BOLD_ITALIC][FontIndex] = (int8_t)BoldItalicPreference;

            Editor->FontIndicesByPreference[TEXT_STYLE_REGULAR][FontIndex] = FontIndex;
            Editor->FontIndicesByPreference[TEXT_STYLE_ITALIC][FontIndex] = FontIndex;
            Editor->FontIndicesByPreference[TEXT_STYLE_BOLD][FontIndex] = FontIndex;
            Editor->FontIndicesByPreference[TEXT_STYLE_BOLD_ITALIC][FontIndex] = FontIndex;
        }

        for(int Iter = 0;
            Iter < Editor->FontCount;
            ++Iter)
        {
            for(int Right = 1;
                Right < Editor->FontCount;
                ++Right)
            {
                int Left = Right - 1;

                for(int TextStyle = 0;
                    TextStyle < TEXT_STYLE_COUNT;
                    ++TextStyle)
                {
                    int LeftPreference = FontPreference[TextStyle][Left];
                    int RightPreference = FontPreference[TextStyle][Right];

                    if(LeftPreference < RightPreference)
                    {
                        int Swap = Editor->FontIndicesByPreference[TextStyle][Left];

                        Editor->FontIndicesByPreference[TextStyle][Left] = Editor->FontIndicesByPreference[TextStyle][Right];
                        Editor->FontIndicesByPreference[TextStyle][Right] = Swap;

                        FontPreference[TextStyle][Left] = (int8_t)RightPreference;
                        FontPreference[TextStyle][Right] = (int8_t)LeftPreference;
                    }
                }
            }
        }

        Editor->KbtsContext = kbts_CreateShapeContext(0, 0); // @Memory

        for(int FontIndex = 0;
            FontIndex < Editor->FontCount;
            ++FontIndex)
        {
            font *Font = &Editor->Fonts[FontIndex];

            kbts_ShapePushFont(Editor->KbtsContext, &Font->Kbts);
        }

        // #TODO: Figure out a growth strategy.
        Editor->TextCapacity = TEXT_CAPACITY;
        Editor->TextLength = 0;
        Editor->Text = PushArray(&Editor->Arena, character, Editor->TextCapacity, 0);

        // @Hardcoded
        Editor->LineCapacity = LINE_CAPACITY;
        Editor->LineCount = 0;
        Editor->Lines = PushArray(&Editor->Arena, edit_line, Editor->LineCapacity, 0);

        // @Hardcoded
        Editor->LineGlyphCapacity = 1024;
        Editor->LineGlyphCount = 0;
        Editor->LineGlyphs = PushArray(&Editor->Arena, layout_glyph, Editor->LineGlyphCapacity, 0);
    }

    if(Editor->FontPixelHeight != FontPixelHeight)
    {
        // Update global font metrics.
        int Ascent = 0;
        int Descent = INT_MAX; // Descents are negative :)
        int LineGap = 0;
        for(int FontIndex = 0; FontIndex < (int)Editor->FontCount; ++FontIndex) {
            stbtt_fontinfo *StbttFont = &Editor->Fonts[FontIndex].Stbtt;
            float Scale = stbtt_ScaleForPixelHeight(StbttFont, (float)FontPixelHeight);

            int FontAscent, FontDescent, FontLineGap;
            stbtt_GetFontVMetrics(StbttFont, &FontAscent, &FontDescent, &FontLineGap);

            FontAscent = (int)roundf((float)FontAscent * Scale);
            FontDescent = (int)roundf((float)FontDescent * Scale);
            FontLineGap = (int)roundf((float)FontLineGap * Scale);

            Ascent = MAXIMUM(Ascent, FontAscent); // This aligns the baselines.
            Descent = MINIMUM(Descent, FontDescent);
            LineGap = MAXIMUM(LineGap, FontLineGap);
        }

        Editor->Ascent = Ascent;
        Editor->Descent = Descent;
        Editor->LineGap = LineGap;
        Editor->LineHeight = Ascent - Descent + LineGap;

        Editor->FontPixelHeight = FontPixelHeight;
    }

    ArenaEndLifetime(&Editor->FrameLifetime);
    Editor->FrameLifetime = ArenaBeginLifetime(&Editor->Arena);

    Editor->FrameBufferWidth = FrameBufferWidth;

    draw_command_list Result = ZERO;
    Result.Capacity = 4096; // @Hardcoded
    Result.Commands = PushArray(&Editor->Arena, draw_command, Result.Capacity, 0);
    Result.SelectionsCapacity = LINE_CAPACITY * 8; // @Hardcoded. Ultimately will be at most the number of runs in the current text.
    Result.Selections = PushArray(&Editor->Arena, draw_box, Result.SelectionsCapacity, 0);

    kbts_shape_context *Context = Editor->KbtsContext;

    kbts_ShapeBegin(Context, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);

    text_style CurrentStyle = TEXT_STYLE_COUNT;
    for (int I = 0; I < Editor->TextLength; ++I) {
        character* Character = &Editor->Text[I];
        text_style Style = Character->Style;

        if (Style != CurrentStyle)
        {
            kbts_ShapeManualBreak(Context);

            assert(Character->Style < TEXT_STYLE_COUNT);

            // Reorder fonts to fit our preference order for this style.
            while (kbts_ShapePopFont(Context));

            for (int FontIndexIndex = 0; FontIndexIndex < Editor->FontCount; ++FontIndexIndex) {
                int FontIndex = Editor->FontIndicesByPreference[Style][Editor->FontCount - 1 - FontIndexIndex];
                kbts_ShapePushFont(Context, &Editor->Fonts[FontIndex].Kbts);
            }

            CurrentStyle = Style;
        }

        kbts_ShapeCodepoint(Context, Editor->Text[I].Codepoint);
    }
    // Append the EOF.
    kbts_ShapeCodepoint(Context, '\n');
    kbts_ShapeEnd(Context);

    int CurrentDirectionGlyphCapacity = 1024;
    layout_glyph *CurrentDirectionGlyphs = PushArray(&Editor->Arena, layout_glyph, Editor->LineGlyphCapacity, 0);
    int CurrentDirectionGlyphCount = 0;
    kbts_direction CurrentDirection = KBTS_DIRECTION_DONT_KNOW;
    kbts_direction ParagraphDirection = 0;

    Editor->LineCount = 0;
    Editor->LineGlyphCount = 0;
    Editor->CursorY = 0;
    Editor->RunningAdvance = 0;

    int RunIndex = 0;

    EditorBeginLines(Editor);
    EditorBeginLine(Editor, &Result);

    kbts_run Run;
    while(kbts_ShapeRun(Context, &Run))
    {
        if((Run.Direction != CurrentDirection) ||
           (Run.Flags & KBTS_BREAK_FLAG_LINE_HARD))
        {
            FlushDirection(&Result, Editor, ParagraphDirection, CurrentDirection, CurrentDirectionGlyphs, CurrentDirectionGlyphCount, CurrentDirectionGlyphCapacity);
            CurrentDirection = Run.Direction;
            CurrentDirectionGlyphCount = 0;
        }

        if(Run.Flags & KBTS_BREAK_FLAG_LINE_HARD)
        {
            EditorNextLine(Editor, &Result);
        }
        
        if(Run.Flags & KBTS_BREAK_FLAG_PARAGRAPH_DIRECTION)
        {
            ParagraphDirection = Run.ParagraphDirection;
        }

        font *Font = KbtsFontToFont(Run.Font);
        float Scale = stbtt_ScaleForPixelHeight(&Font->Stbtt, (float)FontPixelHeight);

        size_t RunGlyphCount = 0;

        kbts_glyph *RunGlyph;
        while(kbts_GlyphIteratorNext(&Run.Glyphs, &RunGlyph))
        {
            if(CurrentDirectionGlyphCount < CurrentDirectionGlyphCapacity)
            {
                int CodepointIndex = RunGlyph->UserIdOrCodepointIndex;
                kbts_shape_codepoint ShapeCodepoint = ZERO;
                character *SourceCharacter = &Editor->Text[CodepointIndex];
                kbts_ShapeGetShapeCodepoint(Context, CodepointIndex, &ShapeCodepoint);

                SourceCharacter->BreakFlags = ShapeCodepoint.BreakFlags;

                layout_glyph *LayoutGlyph;
                if(CurrentDirection == KBTS_DIRECTION_RTL)
                {
                    LayoutGlyph = &CurrentDirectionGlyphs[CurrentDirectionGlyphCapacity - 1 - CurrentDirectionGlyphCount++];
                }
                else
                {
                    LayoutGlyph = &CurrentDirectionGlyphs[CurrentDirectionGlyphCount++];
                }

                memset(LayoutGlyph, 0, sizeof(*LayoutGlyph));

                LayoutGlyph->Font = KbtsFontToFont(Run.Font);
                LayoutGlyph->Id = RunGlyph->Id;
                LayoutGlyph->CodepointIndex = CodepointIndex;
                LayoutGlyph->Direction = Run.Direction;
                LayoutGlyph->AdvanceX = RunGlyph->AdvanceX;
                LayoutGlyph->AdvanceY = RunGlyph->AdvanceY;
                LayoutGlyph->OffsetX = RunGlyph->OffsetX;
                LayoutGlyph->OffsetY = RunGlyph->OffsetY;
                LayoutGlyph->Scale = Scale;
                LayoutGlyph->BreakFlags = ShapeCodepoint.BreakFlags;
                LayoutGlyph->NoShapeBreak = (RunGlyph->Flags & KBTS_GLYPH_FLAG_NO_BREAK) != 0;
                LayoutGlyph->IsNewline = (ShapeCodepoint.Codepoint == '\n');

                RunGlyphCount += 1;
            }
        }

        if(CurrentDirection == KBTS_DIRECTION_RTL)
        {
            // RTL runs are globally right-to-left and locally left-to-right.
            // We agglutinate them together by writing layout glyphs backwards.
            // To restore the global visual left-to-right order, we have to flip each run here.
            size_t BaseSwapIndex = CurrentDirectionGlyphCapacity - CurrentDirectionGlyphCount;

            for(size_t SwapIndex = 0;
                SwapIndex < (RunGlyphCount / 2);
                ++SwapIndex)
            {
                layout_glyph *Right = &CurrentDirectionGlyphs[BaseSwapIndex + SwapIndex];
                layout_glyph *Left = &CurrentDirectionGlyphs[BaseSwapIndex + RunGlyphCount - 1 - SwapIndex];

                layout_glyph Swap = *Left;
                *Left = *Right;
                *Right = Swap;
            }
        }

        RunIndex += 1;
    }

    FlushDirection(&Result, Editor, ParagraphDirection, CurrentDirection, CurrentDirectionGlyphs, CurrentDirectionGlyphCount, CurrentDirectionGlyphCapacity);

    EditorEndLines(Editor, &Result);

    float TextWidth = Editor->TextBounds.MaxX - Editor->TextBounds.MinX;
    float ScrollAreaHeight;
    float ViewportWidth = (float)FrameBufferWidth;
    float ViewportHeight = (float)FrameBufferHeight;

    { // Clip the scroll region.
        float MaxScrollX = TextWidth - ViewportWidth + CURSOR_THICKNESS;
        MaxScrollX = MAXIMUM(0, MaxScrollX);
        Editor->TargetScrollX = ClampFloat(Editor->TargetScrollX, 0, MaxScrollX);

        edit_line *LastLine = &Editor->Lines[Editor->LineCount - 1];
        float MaxScrollY = LastLine->GlyphBox.MinY - (float)Editor->Ascent;
        MaxScrollY = MAXIMUM(0, MaxScrollY);
        Editor->TargetScrollY = ClampFloat(Editor->TargetScrollY, 0, MaxScrollY);

        ScrollAreaHeight = MaxScrollY + ViewportHeight;

        Editor->MaxScrollX = MaxScrollX;
        Editor->MaxScrollY = MaxScrollY;
    }

    // @Incomplete: Animate this.
    Editor->CurrentScrollX = Editor->TargetScrollX;
    Editor->CurrentScrollY = Editor->TargetScrollY;

    float ViewportMinX = Editor->CurrentScrollX;
    float ViewportMinY = Editor->CurrentScrollY;

    { // In order to cull accurately, we figure out the cursor position out-of-band here.
        edit_line *CursorLine = &Editor->Lines[Editor->CursorPosition.LineIndex];
        float AbsoluteCursorX = Result.Cursor.X;
        float AbsoluteCursorY = Result.Cursor.Y;

        AbsoluteCursorX += AlignmentOffsetXForLine(CursorLine, TextWidth);

        if(Editor->Flags & EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR)
        {
            float AbsoluteCursorMaxX = AbsoluteCursorX + CURSOR_THICKNESS;
            float AbsoluteCursorMaxY = AbsoluteCursorY + (float)Editor->LineHeight;

            float AdjustMinX = ClampFloat(AbsoluteCursorMaxX - ViewportWidth, 0, Editor->MaxScrollX);
            float AdjustMinY = ClampFloat(AbsoluteCursorMaxY - ViewportHeight, 0, Editor->MaxScrollY);

            ViewportMinX = MINIMUM(ViewportMinX, AbsoluteCursorX);
            ViewportMinX = MAXIMUM(ViewportMinX, AdjustMinX);
            ViewportMinY = MINIMUM(ViewportMinY, AbsoluteCursorY);
            ViewportMinY = MAXIMUM(ViewportMinY, AdjustMinY);

            Editor->TargetScrollX = ViewportMinX;
            Editor->TargetScrollY = ViewportMinY;
            assert(Editor->TargetScrollX <= Editor->MaxScrollX);
            assert(Editor->TargetScrollY <= Editor->MaxScrollY);
        }
    }

    float ViewportMaxX = ViewportMinX + ViewportWidth;
    float ViewportMaxY = ViewportMinY + ViewportHeight;

    Result.ScrollMinX = 0;
    Result.ScrollMaxX = 1;
    Result.ScrollMinY = 0;
    Result.ScrollMaxY = 1;

    if(TextWidth != 0)
    {
        Result.ScrollMinX = ClampFloat(ViewportMinX / TextWidth, 0, 1);
        Result.ScrollMaxX = ClampFloat(ViewportMaxX / TextWidth, 0, 1);
    }

    if(ScrollAreaHeight != 0)
    {
        Result.ScrollMinY = ClampFloat(ViewportMinY / ScrollAreaHeight, 0, 1);
        Result.ScrollMaxY = ClampFloat(ViewportMaxY / ScrollAreaHeight, 0, 1);
    }

    int DrawSelectionsWritten = 0;

    // At this point, we know the dimensions of each line.
    // This is enough to do per-line layout, taking alignment into account.
    for(int LineIndex = 0;
        LineIndex < Editor->LineCount;
        ++LineIndex)
    {
        edit_line *Line = &Editor->Lines[LineIndex];
        float LogicalOffsetX = AlignmentOffsetXForLine(Line, TextWidth);
        float VisualOffsetX = -ViewportMinX + LogicalOffsetX;
        float VisualOffsetY = -ViewportMinY;
        int FirstCommandIndex = Line->FirstCommandIndex;
        int OnePastLastCommandIndex = Line->OnePastLastCommandIndex;
        int FirstSelectionIndex = Line->FirstSelectionIndex;
        int OnePastLastSelectionIndex = Line->OnePastLastSelectionIndex;

        Line->FirstSelectionIndex = DrawSelectionsWritten;

        float LineMinX = Line->GlyphBox.MinX + VisualOffsetX;
        float LineMinY = Line->GlyphBox.MinY + VisualOffsetY;
        float LineMaxX = Line->GlyphBox.MaxX + VisualOffsetX;
        float LineMaxY = Line->GlyphBox.MaxY + VisualOffsetY;

        int LineVisible = BoxOverlap(LineMinX, LineMinY, LineMaxX, LineMaxY, 0, 0, ViewportWidth, ViewportHeight);

        if(LineIndex == Editor->CursorPosition.LineIndex)
        {
            Result.Cursor.X += VisualOffsetX;
            Result.Cursor.Y += VisualOffsetY;

            if(!(Editor->Flags & EDITOR_FLAG_KEEP_DESIRED_X))
            {
                Editor->CursorPosition.DesiredX += LogicalOffsetX;
            }
        }

        for(int CommandIndex = FirstCommandIndex;
            CommandIndex < OnePastLastCommandIndex;
            ++CommandIndex)
        {
            draw_command *Command = &Result.Commands[CommandIndex];

            Command->X += VisualOffsetX;
            Command->Y += VisualOffsetY;

            if(LineVisible &&
               (Command->ScaledWidth > 0.0f) &&
               // @Incomplete: We need to handle cases where the newline is on the left/in the middle of the line.
               // !(Command->Flags & DRAW_COMMAND_FLAG_NEWLINE) && // Hide newlines.
               BoxOverlap(Command->X, Command->Y, Command->X + Command->ScaledWidth, Command->Y + Command->ScaledHeight,
                          0, 0, ViewportWidth, ViewportHeight))
            {
                Command->Flags |= DRAW_COMMAND_FLAG_VISIBLE;
            }
        }

        for(int SelectionIndex = FirstSelectionIndex;
            SelectionIndex < OnePastLastSelectionIndex;
            ++SelectionIndex)
        {
            draw_box *Selection = &Result.Selections[SelectionIndex];
            Selection->MaxY = Line->GlyphBox.MaxY;
            Selection->MinX += VisualOffsetX;
            Selection->MinY += VisualOffsetY;
            Selection->MaxX += VisualOffsetX;
            Selection->MaxY += VisualOffsetY;

            if(BoxOverlap(Selection->MinX, Selection->MinY, Selection->MaxX, Selection->MaxY,
                          0, 0, ViewportWidth, ViewportHeight))
            {
                Result.Selections[DrawSelectionsWritten++] = *Selection;
            }
        }

        Line->OnePastLastSelectionIndex = DrawSelectionsWritten;
    }

    Result.SelectionsCount = DrawSelectionsWritten;

    // This copy of the draw list is used when processing editor commands
    // (see DoCommand).
    Editor->CurrentScrollX = ViewportMinX;
    Editor->CurrentScrollY = ViewportMinY;
    Editor->DrawList = Result;
    Editor->FrameBufferHeight = (int)FrameBufferHeight;
    Editor->TotalHeightInPixels = (int)ceilf(Editor->Lines[Editor->LineCount - 1].GlyphBox.MaxY);
    Editor->Flags &= ~EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR;

    return Result;
}

static void CarrySelection(editor* Editor) {
    Editor->SelectionPosition = Editor->CursorPosition;
}

static void InsertCharacter(editor* Editor, character Char) {
    Editor->Flags &= ~(EDITOR_FLAG_KEEP_DESIRED_X | EDITOR_FLAG_KEEP_DESIRED_Y);

    if (Editor->TextLength < Editor->TextCapacity) {
        // Move all of the text after this character up one, to make room, then insert the new character and advance cursor.
        int Cursor = Editor->CursorPosition.CodepointIndex;
        int NumCharactersAfterCursor = Editor->TextLength - Cursor;
        memmove(Editor->Text + Cursor + 1, Editor->Text + Cursor, NumCharactersAfterCursor * sizeof(character));
        Editor->Text[Cursor] = Char;
        Editor->TextLength += 1;
        Editor->CursorPosition.CodepointIndex += 1;
        CarrySelection(Editor);

        Editor->Flags |= EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR;
    }
}

static void SelectAllText(editor* Editor) {
    Editor->CursorPosition.CodepointIndex = Editor->TextLength;
    Editor->SelectionPosition.CodepointIndex = 0;
}

static void ToggleSelectionStyle(editor* Editor, text_style Style) {
    assert((Style == TEXT_STYLE_BOLD) || (Style == TEXT_STYLE_ITALIC));
    for (int CodepointIndex = GetSelectionStart(Editor); CodepointIndex < GetSelectionEnd(Editor); ++CodepointIndex) {
        character* Character = &Editor->Text[CodepointIndex];
        Character->Style ^= Style;
    }
}

static int UndoStateIsValid(editor *Editor, undo_state_header *Header)
{
    int Result = 0;

    if(Header &&
       (Header != &Editor->UndoSentinel))
    {
        undo_state *Undo = (undo_state *)Header;

        if(RingAllocationIsValid(&Editor->UndoAllocator, &Undo->Allocation))
        {
            Result = 1;
        }
    }

    return Result;
}

static void UndoPush(editor* Editor)
{
    undo_state_header* UndoCursor = Editor->UndoCursor;
    if(UndoStateIsValid(Editor, UndoCursor))
    {
        undo_state *Undo = (undo_state *)UndoCursor;
        Editor->UndoSentinel.Prev = UndoCursor->Prev;
        Editor->UndoSentinel.Prev->Next = &Editor->UndoSentinel;
        RingAllocatorRewind(&Editor->UndoAllocator, &Undo->Allocation);
    }
    
    ring_allocation Allocation = RingAllocatorAlloc(&Editor->UndoAllocator, sizeof(undo_state));
    if(Allocation.Memory)
    {
        ring_allocation TextAllocation = RingAllocatorAlloc(&Editor->UndoAllocator, sizeof(character) * Editor->TextLength);
        ring_allocation LineAllocation = RingAllocatorAlloc(&Editor->UndoAllocator, sizeof(edit_line) * Editor->LineCount);

        if(TextAllocation.Memory &&
           LineAllocation.Memory &&
           RingAllocationIsValid(&Editor->UndoAllocator, &Allocation))
        {
            undo_state *Undo = (undo_state *)Allocation.Memory;
            Undo->Text = (character*)TextAllocation.Memory;
            memcpy(Undo->Text, Editor->Text, sizeof(*Editor->Text) * Editor->TextLength);
            Undo->Lines = (edit_line*)LineAllocation.Memory;
            memcpy(Undo->Lines, Editor->Lines, sizeof(*Editor->Lines) * Editor->LineCount);
            Undo->TextLength = Editor->TextLength;
            Undo->LineCount = Editor->LineCount;
            Undo->TargetScrollX = Editor->TargetScrollX;
            Undo->TargetScrollY = Editor->TargetScrollY;
            Undo->CursorPosition = Editor->CursorPosition;
            Undo->SelectionPosition = Editor->SelectionPosition;

            Undo->Header.Prev = Editor->UndoSentinel.Prev;
            Undo->Header.Next = &Editor->UndoSentinel;
            Undo->Header.Prev->Next = Undo->Header.Next->Prev = &Undo->Header;

            Undo->Allocation = Allocation;
        }
        else
        {
            RingAllocatorRewind(&Editor->UndoAllocator, &Allocation);
        }
    }

    Editor->UndoCursor = 0;
}

// Indices out of range are a no-op.
static void DeleteCharacters(editor* Editor, int StartIdx, int EndIdx, int SkipUndo) {
    Editor->Flags &= ~(EDITOR_FLAG_KEEP_DESIRED_X | EDITOR_FLAG_KEEP_DESIRED_Y);

    int NumToDelete = EndIdx - StartIdx;
    if (NumToDelete > 0 && StartIdx >= 0 && EndIdx <= Editor->TextLength) {
        if (!SkipUndo) {
            UndoPush(Editor);
        }

        // Move characters starting at End over to Start
        int NumCharactersToMove = Editor->TextLength - EndIdx;
        memmove(Editor->Text + StartIdx, Editor->Text + EndIdx, NumCharactersToMove * sizeof(character));
        Editor->TextLength -= NumToDelete;
        Editor->CursorPosition.CodepointIndex = StartIdx;
        Editor->Flags |= EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR;
    }
}

static void DeleteSelectedText(editor* Editor) {
    DeleteCharacters(Editor, GetSelectionStart(Editor), GetSelectionEnd(Editor), 0);
}

// Returns a UTF8 buffer with a copy of the currently selected text.
// If none is selected, returns NULL.
// The callee owns this memory, and thus should free it when they're done.
// #TODO: Probably shouldn't malloc this, but if we alloc in the arena have to give more clear lifetime guidelines.
//        I can imagine just having one slot for "currently selected text" which is just memoize copied in on demand.
static uint8_t* GetSelectedText(editor* Editor) {
    uint8_t* Result = 0;
    int FirstIndex = GetSelectionStart(Editor);
    int OnePastLastIndex = GetSelectionEnd(Editor);
    if (OnePastLastIndex > FirstIndex) {
        int NumWritten = 0;
        // Worst case is each character takes 4 bytes + null terminator.
        Result = (uint8_t *)malloc((OnePastLastIndex - FirstIndex) * 4 + 1);
        for (int CharacterIndex = FirstIndex; CharacterIndex < OnePastLastIndex; ++CharacterIndex) {
            character* Character = &Editor->Text[CharacterIndex];
            kbts_encode_utf8 Encode = kbts_EncodeUtf8(Character->Codepoint);
            if (Encode.Valid) {
                for (int I = 0; I < Encode.EncodedLength; ++I) {
                    Result[NumWritten++] = Encode.Encoded[I];
                }
            }
            Result[NumWritten] = 0;
        }
    }
    return Result;
}

// Insert a chunk of utf8 text at the current cursor position. Use this for both single character insertion and also pasting.
// If any text is selected when this happens, it is deleted (the inserted text is assumed to replace it).
static void InsertText(editor* Editor, const char* Utf8, int Length, int SkipUndo) {
    if ((Editor->TextLength + Length) <= TEXT_CAPACITY) {
        if (!SkipUndo) {
            UndoPush(Editor);
        }

        // Delete any selection if it exists.
        DeleteSelectedText(Editor);

        // Convert the UTF8 into a series of codepoints, and then combine those codepoints into characters to be inserted.
        const char *At = Utf8;
        const char *End = Utf8 + Length;
        while (At < End) {
            kbts_decode Decode = kbts_DecodeUtf8(At, (kbts_un)(End - At));
            if (Decode.Valid) {
                // For now, just 1:1 put the codepoint into a character and insert it.
                // Later, multiple codepoints might be combined into a single character, and then inserted as a whole.

                character Char = {0};
                Char.Codepoint = Decode.Codepoint;
                InsertCharacter(Editor, Char);
            }
            At += Decode.SourceCharactersConsumed;
        }
    }
}

static void ImeCompose(editor* Editor, const char* Utf8, int Length, int CursorOffset, int SelectionLengthFromCursor) {
    int ImeStart = Editor->ImeStartCodepointIndex;
    int ImeLength = Editor->ImeLength;

    if (ImeLength || Length) {
        if (ImeLength) {
            DeleteCharacters(Editor, ImeStart, ImeStart + ImeLength, 1);
        }

        ImeStart = Editor->CursorPosition.CodepointIndex;

        if (Length) {
            InsertText(Editor, Utf8, Length, 1);
        }

        Editor->ImeStartCodepointIndex = ImeStart;
        Editor->ImeLength = Length;
        Editor->CursorPosition.CodepointIndex = ImeStart + CursorOffset;
        Editor->SelectionPosition.CodepointIndex = ImeStart + CursorOffset + SelectionLengthFromCursor;
        Editor->Flags &= ~(EDITOR_FLAG_KEEP_DESIRED_X | EDITOR_FLAG_KEEP_DESIRED_Y);
    }
}

static int IsAnyTextSelected(editor* Editor) {
    return Editor->SelectionPosition.CodepointIndex != Editor->CursorPosition.CodepointIndex;
}

static int LineCodepointIndexAtX(editor* Editor, int LineIndex, float X) {
    int Result = 0;

    if (LineIndex >= 0 && LineIndex < Editor->LineCount) {
        edit_line* Line = &Editor->Lines[LineIndex];

        Result = Line->MinCodepointIndex;
        draw_command *Command = 0;
        int PrevCodepointIndex = ~0;
        for (int LineCommandIndex = Line->FirstCommandIndex; LineCommandIndex < Line->OnePastLastCommandIndex; ++LineCommandIndex) {
            Command = &Editor->DrawList.Commands[LineCommandIndex];
            Result = Command->CodepointIndex;
            // We try to round to the nearest cursor position, instead of simply snapping to the left side of the glyph.
            if ((Command->CodepointIndex != PrevCodepointIndex) && ((Command->X + Command->ScaledWidth * 0.5f) > X)) {
                break;
            }
            PrevCodepointIndex = Command->CodepointIndex;
        }

        if (Command) {
            if ((Line->Direction == KBTS_DIRECTION_RTL) && (PrevCodepointIndex != ~0)) {
                Result = PrevCodepointIndex;
            }
        }
    }

    return Result;
}

static void CollapseSelection(editor *Editor, int Forward)
{
    if(IsAnyTextSelected(Editor))
    {
        // If there is any text selected, move the cursor to the start or end of the selection, depending on direction.
        if((Forward != 0) == (Editor->CursorPosition.CodepointIndex > Editor->SelectionPosition.CodepointIndex))
        {
            Editor->SelectionPosition = Editor->CursorPosition;
        }
        else
        {
            Editor->CursorPosition = Editor->SelectionPosition;
        }
    }
}

typedef uint32_t move_granularity;
enum move_granularity_enum
{
    // Horizontal moves
    MOVE_GRANULARITY_BY_CODEPOINT,
    MOVE_GRANULARITY_BY_GRAPHEME,
    MOVE_GRANULARITY_BY_WORD,

    // Vertical moves
    MOVE_GRANULARITY_BY_LINE,
    MOVE_GRANULARITY_BY_PARAGRAPH,
};

static inline int MoveGranularityIsHorizontal(move_granularity Granularity)
{
    int Result = Granularity < MOVE_GRANULARITY_BY_LINE;
    return Result;
}

static void MoveCursor(editor* Editor, int Forward, int SelectionActive, move_granularity Granularity) {
    int Delta = Forward ? 1 : -1;
    if (MoveGranularityIsHorizontal(Granularity)) {
        // Note: Even if the cursor doesn't move, the intention to move the cursor, without selection active, clears the selection.
        // So it's important to execute the code regardless of whether we're getting clipped by the bounds.
        if (SelectionActive || !IsAnyTextSelected(Editor)) {
            // We use kbts_break_flags directly here for demonstration purposes.
            // If you are designing your own editor, you will probably have your own ideas of how the cursor is meant to move.
            kbts_break_flags BreakFlags = 0;
            switch(Granularity)
            {
            case MOVE_GRANULARITY_BY_GRAPHEME: BreakFlags = KBTS_BREAK_FLAG_GRAPHEME; break;
            case MOVE_GRANULARITY_BY_WORD: BreakFlags = KBTS_BREAK_FLAG_WORD; break;
            }

            character *Start = Editor->Text;
            character *End = Editor->Text + Editor->TextLength;
            character *At = Editor->Text + Editor->CursorPosition.CodepointIndex;

            for(;;) {
                character *Next = At + Delta;
                if ((Next >= Start) && (Next <= End)) {
                    At = Next;

                    if ((At == End) ||
                        ((At->BreakFlags & BreakFlags) == BreakFlags) /* Always true for BY_CODEPOINT */) {
                        break;
                    }
                } else {
                    break;
                }
            }

            Editor->CursorPosition.CodepointIndex = (int)(At - Start);
        } else {
            CollapseSelection(Editor, Forward);
        }
    } else {
        if (!SelectionActive) {
            CollapseSelection(Editor, Delta > 0);
        }

        float DesiredX = Editor->CursorPosition.DesiredX;

        int NextLineIndex = Editor->CursorPosition.LineIndex;
        if (Granularity == MOVE_GRANULARITY_BY_PARAGRAPH) {
            int NonEmptyLineCount = 0;

            while ((NextLineIndex >= 0) && (NextLineIndex < Editor->LineCount)) {
                edit_line *Line = &Editor->Lines[NextLineIndex];

                if (Line->MinCodepointIndex == Line->MaxCodepointIndex) {
                    if (NonEmptyLineCount) {
                        break;
                    }
                } else {
                    NonEmptyLineCount += 1;
                }

                NextLineIndex += Delta;
            }
        } else {
            NextLineIndex += Delta;
        }

        if (NextLineIndex < 0) {
            NextLineIndex = 0;
            DesiredX = -INFINITY;
        } else if (NextLineIndex >= (int)Editor->LineCount) {
            NextLineIndex = (int)Editor->LineCount - 1;
            DesiredX = INFINITY;
        }

        int NewCodepointIndex = LineCodepointIndexAtX(Editor, NextLineIndex, DesiredX);

        Editor->CursorPosition.LineIndex = NextLineIndex;
        Editor->CursorPosition.CodepointIndex = NewCodepointIndex;
        Editor->Flags |= EDITOR_FLAG_KEEP_DESIRED_X;
    }

    Editor->Flags |= EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR;
}

static void DeleteText(editor* Editor, int Forward, move_granularity Granularity) {
    if (!IsAnyTextSelected(Editor)) {
        MoveCursor(Editor, Forward, 1, Granularity);
    }

    DeleteSelectedText(Editor);
}

static void ApplyUndoState(editor *Editor, undo_state_header *Header)
{
    if(UndoStateIsValid(Editor, Header))
    {
        undo_state *Undo = (undo_state *)Header;

        memcpy(Editor->Text, Undo->Text, sizeof(*Undo->Text) * Undo->TextLength);
        Editor->TextLength = Undo->TextLength;
        Editor->TargetScrollX = Undo->TargetScrollX;
        Editor->TargetScrollY = Undo->TargetScrollY;
        memcpy(Editor->Lines, Undo->Lines, sizeof(*Undo->Lines) * Undo->LineCount);
        Editor->LineCount = Undo->LineCount;
        Editor->CursorPosition = Undo->CursorPosition;
        Editor->SelectionPosition = Undo->SelectionPosition;
    }

    Editor->UndoCursor = Header;
}

// Issue a given command from editor_commands.
// Set selection to true while doing movement commands to alter the selection.
static void DoCommand(editor* Editor, editor_command Command) {
    int SelectionActive = Command.SelectionActive;
    Editor->Flags &= ~(EDITOR_FLAG_KEEP_DESIRED_X | EDITOR_FLAG_KEEP_DESIRED_Y);

    switch (Command.Type) {
        case EDITOR_COMMAND_HOME:
        case EDITOR_COMMAND_END: {
            edit_line *Line = &Editor->Lines[Editor->CursorPosition.LineIndex];
            int NewCodepointIndex = (Command.Type == EDITOR_COMMAND_HOME) ? Line->MinCodepointIndex : Line->MaxCodepointIndex;
            Editor->CursorPosition.CodepointIndex = NewCodepointIndex;
            Editor->Flags |= EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR;
        } break;

        case EDITOR_COMMAND_PAGEUP:
        case EDITOR_COMMAND_PAGEDOWN: {
            float DesiredY = (float)Editor->CursorPosition.DesiredY;
            float DesiredX = Editor->CursorPosition.DesiredX;
            // @Cleanup: We set DesiredX in FlushLine, we can probably do the same thing with DesiredY?
            if (!(Editor->Flags & EDITOR_FLAG_KEEP_DESIRED_Y)) {
                DesiredY = Editor->Lines[Editor->CursorPosition.LineIndex].GlyphBox.MinY;
            }

            if (Command.Type == EDITOR_COMMAND_PAGEUP) {
                DesiredY -= (float)Editor->FrameBufferHeight;
            } else {
                DesiredY += (float)Editor->FrameBufferHeight;
            }
            if (DesiredY < Editor->Lines[0].GlyphBox.MinY) {
                DesiredY = -INFINITY;
                DesiredX = -INFINITY;
            } else if (DesiredY > (float)Editor->TotalHeightInPixels) {
                DesiredY = INFINITY;
                DesiredX = INFINITY;
            }

            int DesiredLine = 0;
            int LineIndexIncrement = -1;
            if (Command.Type == EDITOR_COMMAND_PAGEDOWN) {
                DesiredLine = Editor->LineCount - 1;
                LineIndexIncrement = 1;
            }

            for (int LineIndex = Editor->CursorPosition.LineIndex; (LineIndex >= 0) && (LineIndex < Editor->LineCount); LineIndex += LineIndexIncrement) {
                edit_line *Line = &Editor->Lines[LineIndex];
                float MinY = Line->GlyphBox.MinY;

                if (Line->GlyphBox.MinY >= DesiredY) {
                    DesiredLine = LineIndex;
                }

                if ((MinY >= DesiredY) == (LineIndexIncrement > 0)) {
                    break;
                }
            }

            // We've found a line.
            // Now, find the codpeoint.
            int CodepointIndex = LineCodepointIndexAtX(Editor, DesiredLine, DesiredX);

            Editor->CursorPosition.LineIndex = DesiredLine;
            Editor->CursorPosition.CodepointIndex = CodepointIndex;

            Editor->CursorPosition.DesiredY = (int)DesiredY;

            Editor->Flags |= EDITOR_FLAG_KEEP_DESIRED_Y | EDITOR_FLAG_KEEP_DESIRED_X | EDITOR_FLAG_MOVE_VIEWPOINT_TO_INCLUDE_CURSOR;
        } break;

        case EDITOR_COMMAND_LEFT:
        case EDITOR_COMMAND_RIGHT:
            MoveCursor(Editor, Command.Type == EDITOR_COMMAND_RIGHT, Command.SelectionActive, MOVE_GRANULARITY_BY_GRAPHEME);
        break;

        case EDITOR_COMMAND_PREV_WORD:
        case EDITOR_COMMAND_NEXT_WORD:
            MoveCursor(Editor, Command.Type == EDITOR_COMMAND_NEXT_WORD, Command.SelectionActive, MOVE_GRANULARITY_BY_WORD);
        break;

        case EDITOR_COMMAND_UP:
        case EDITOR_COMMAND_DOWN:
            MoveCursor(Editor, Command.Type == EDITOR_COMMAND_DOWN, Command.SelectionActive, MOVE_GRANULARITY_BY_LINE);
        break;

        case EDITOR_COMMAND_PREV_PARAGRAPH:
        case EDITOR_COMMAND_NEXT_PARAGRAPH:
            MoveCursor(Editor, Command.Type == EDITOR_COMMAND_NEXT_PARAGRAPH, Command.SelectionActive, MOVE_GRANULARITY_BY_PARAGRAPH);
        break;

        case EDITOR_COMMAND_DELETE:
        case EDITOR_COMMAND_BACKSPACE:
            // A grapheme cluster is, roughly, a base character followed by a bunch of marks.
            // BACKSPACE deletes from the end, so it can delete the last mark no problem.
            // However, DELETE deletes from the start, and it makes no sense to only delete the base character.
            // For this reason, DELETE deletes the whole grapheme.
            DeleteText(Editor, (Command.Type == EDITOR_COMMAND_BACKSPACE) ? 0 : 1, (Command.Type == EDITOR_COMMAND_BACKSPACE) ? MOVE_GRANULARITY_BY_CODEPOINT : MOVE_GRANULARITY_BY_GRAPHEME);
            SelectionActive = 0;
        break;

        case EDITOR_COMMAND_DELETE_WORD:
        case EDITOR_COMMAND_BACKSPACE_WORD:
            DeleteText(Editor, (Command.Type == EDITOR_COMMAND_BACKSPACE_WORD) ? 0 : 1, MOVE_GRANULARITY_BY_WORD);
            SelectionActive = 0;
        break;

        case EDITOR_COMMAND_MOUSE_MOVE:
        case EDITOR_COMMAND_MOUSE_PRESS: {
            int DesiredLineIndex = (int)((Editor->CurrentScrollY + Command.Y) / (float)Editor->LineHeight);
            int CodepointIndex = Editor->TextLength;
            if (DesiredLineIndex >= Editor->LineCount) {
                assert(Editor->LineCount);
                DesiredLineIndex = Editor->LineCount - 1;
                CodepointIndex = Editor->Lines[DesiredLineIndex].MaxCodepointIndex;
            } else {
                CodepointIndex = LineCodepointIndexAtX(Editor, DesiredLineIndex, Command.X);
            }
            if ((Command.Type == EDITOR_COMMAND_MOUSE_PRESS) || SelectionActive) {
                Editor->CursorPosition.LineIndex = DesiredLineIndex;
                Editor->CursorPosition.CodepointIndex = CodepointIndex;
            }
            SelectionActive = (Command.Type == EDITOR_COMMAND_MOUSE_MOVE); // Avoid killing the selection when moving the mouse!
        } break;

        case EDITOR_COMMAND_MOUSE_RELEASE: {
            SelectionActive = 1; // Avoid killing the selection at the end of a mouse select!
        } break;

        case EDITOR_COMMAND_SCROLL: {
            Editor->TargetScrollX += Command.X;
            Editor->TargetScrollY -= Command.Y;
            SelectionActive = 1; // Avoid killing the selection when scrolling!
        } break;

        case EDITOR_COMMAND_SCROLL_ABSOLUTE_01: {
            float Scroll01 = 0;
            if (Command.Axis == EDITOR_AXIS_X) {
                Scroll01 = Command.X;
            } else if (Command.Axis == EDITOR_AXIS_Y) {
                Scroll01 = Command.Y;
            }

            Scroll01 = ClampFloat(Scroll01, 0, 1);

            if (Command.Axis == EDITOR_AXIS_X) {
                Editor->TargetScrollX = Editor->MaxScrollX * Scroll01;
            } else if (Command.Axis == EDITOR_AXIS_Y) {
                Editor->TargetScrollY = Editor->MaxScrollY * Scroll01;
            }
            SelectionActive = 1; // Avoid killing the selection when scrolling!
        } break;

        case EDITOR_COMMAND_UNDO: {
            undo_state_header *UndoCursor = Editor->UndoCursor;
            if(UndoCursor)
            {
                UndoCursor = UndoCursor->Prev;
            }
            else
            {
                UndoCursor = Editor->UndoSentinel.Prev;
            }

            ApplyUndoState(Editor, UndoCursor);
        } break;

        case EDITOR_COMMAND_REDO: {
            undo_state_header *UndoCursor = Editor->UndoCursor;
            if(UndoCursor)
            {
                UndoCursor = UndoCursor->Next;
            }

            ApplyUndoState(Editor, UndoCursor);
        } break;

        case EDITOR_COMMAND_TOGGLE_LINE_WRAP: {
            Editor->Flags ^= EDITOR_FLAG_WRAP_LINES;
        } break;

        case EDITOR_COMMAND_TOGGLE_NEWLINE_DISPLAY: {
            Editor->Flags ^= EDITOR_FLAG_DISPLAY_NEWLINES;
        } break;
    }

    if (!SelectionActive) {
        CarrySelection(Editor);
    }
}
