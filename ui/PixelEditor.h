#pragma once
#include "imgui.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
namespace lab {
// Syntax classes of MineC source, for highlighting (shared with the reference viewer).
enum class Tok : unsigned char {Text,Keyword,Type,Builtin,Number,Comment,Directive,Function,Punct};
struct CodeColors {
    ImU32 bg,text,keyword,type,builtin,number,comment,directive,function,punct;
    ImU32 gutterBg,gutter,currentLine,selection,cursor,errorFill,bracket;
    ImU32 popupBg,popupEdge,popupSelect,popupText,popupKind,popupHint;
};
const CodeColors& pico8Code();   // the PICO-8 palette used by the pixel UI
// Token class per byte of a line; inBlock carries an open /* comment across lines.
void classify(std::string_view line,bool& inBlock,std::vector<Tok>& out);
// Draws one line of code at p in the pixel font (cells of 6 x 10 font pixels);
// characters beyond ASCII take two cells.
void drawCodeLine(ImDrawList* d,ImVec2 p,float scale,std::string_view line,bool& inBlock,const CodeColors& c);

// A code editor drawn in the pixel font with MineC highlighting, a gutter,
// selection, clipboard, undo, auto-indent and completion.
class PixelEditor {
public:
    void setText(const std::string& text);
    std::string text() const;
    // Draws and edits inside the rectangle (screen pixels). scale is the pixel
    // font scale. Returns true when the text changed this frame.
    bool draw(const char* id,ImVec2 pos,ImVec2 size,float scale,const CodeColors& c,int errorLine);
    bool focused() const {return focused_;}
    void focus() {focused_=true;}
    void setCursor(int line,int col) {line=std::clamp(line,0,static_cast<int>(lines_.size())-1); cursor_=anchor_={line,std::clamp(col,0,static_cast<int>(lines_[static_cast<size_t>(line)].size()))};}
    int lineLength(int line) const {return line>=0 && line<static_cast<int>(lines_.size())?static_cast<int>(lines_[static_cast<size_t>(line)].size()):0;}
    void moveToEnd() {cursor_=anchor_={static_cast<int>(lines_.size())-1,static_cast<int>(lines_.back().size())};}
private:
    struct Pos {int line{},col{};};   // col is a byte offset at a character boundary
    struct Item {std::string label,insert,hint; char kind;};  // kind: K keyword, T type, F built-in, S snippet, N name
    struct Snapshot {std::vector<std::string> lines; Pos cursor;};
    bool keyboard();
    void mouse(ImVec2 origin,float cw,float lh,bool hovered,bool clickedInside);
    void insert(const std::string& s);
    void eraseSelection();
    bool hasSelection() const {return anchor_.line!=cursor_.line || anchor_.col!=cursor_.col;}
    std::pair<Pos,Pos> ordered() const;
    std::string selected() const;
    void remember(int kind);   // undo snapshot; kind groups typing
    void moveTo(Pos p,bool select);
    Pos left(Pos p) const; Pos right(Pos p) const;
    Pos wordLeft(Pos p) const; Pos wordRight(Pos p) const;
    int column(Pos p) const;   // visual cells before p
    int colAtCell(int line,int cell) const;
    std::string wordBefore() const;
    void updateCompletion(bool force);
    void accept();
    void drawCompletion(ImDrawList* d,ImVec2 origin,float cw,float lh,float scale,const CodeColors& c,ImVec2 clipMax);
    void ensureVisible(int rows,int cols);
    std::vector<std::string> lines_{""};
    Pos cursor_,anchor_;
    int wantCell_{-1};            // column kept when moving up and down
    int top_{},leftCell_{};       // first visible row and cell
    bool focused_{},dragging_{},changed_{};
    std::vector<Snapshot> undo_,redo_;
    int lastKind_{-1}; double lastEdit_{};
    std::vector<Item> items_; int pick_{}; bool popup_{};
    double blink_{};
};
}
