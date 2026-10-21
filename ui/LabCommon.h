#pragma once
#include "MachineSession.h"
#include "imgui.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
namespace lab {
// One UI under trial. Several can live side by side; the lab switches between
// them while they share one machine session.
class LabUi {
public:
    virtual ~LabUi()=default;
    virtual const char* name() const=0;
    virtual void activate()=0;                  // style and colours when it becomes the shown UI
    virtual void frame(float width,float height)=0;
    // For automated runs.
    virtual void choose(size_t program)=0;
    virtual void buildChosen()=0;
    virtual void showTab(int tab)=0;            // 0 the program's screen, 1 the board, 2 code
    // Loads a program file into the UI's code editor and runs it; false if it
    // did not start a build (no editor, or a compile error shown instead).
    virtual bool runCodeFile(const std::filesystem::path&) {return false;}
    virtual void setBoardZoom(double) {}        // pixels per cell, for screenshots
};
// Keys by name: direction names use arrows and WASD, the others Z, X, C, V in order.
std::vector<std::vector<ImGuiKey>> bindingsFor(const std::vector<std::string>& keys);
// Feeds the keyboard state to the session's keys; extra holds keys pressed
// on screen (one flag per key), or is empty.
void feedKeys(ims::MachineSession& session,const ims::MachineSession::Status& s,const std::vector<uint8_t>& extra={},bool keyboard=true);
// A .mc or .imsg file chosen in the Windows file dialog.
std::optional<std::filesystem::path> openProgramDialog();
std::optional<std::filesystem::path> saveProgramDialog(const std::string& suggested);
std::string readTextFile(const std::filesystem::path& path);
void writeTextFile(const std::filesystem::path& path,const std::string& text);
// Fonts: ImGui's default and a Japanese font (once, before the first frame).
void loadFonts();
ImFont* jpFont();
// A swap chain's back buffer as a 24-bit BMP (call before Present).
bool saveBackBuffer(ID3D11Device* device,ID3D11DeviceContext* context,IDXGISwapChain* swap,const std::string& path);
// The line number in a "line N: ..." error, or 0.
int errorLineOf(const std::string& message);
// Pan and zoom over the newest frames of the board.
struct BoardCamera {
    double x{},y{},zoom{};
    bool follow{true};
    int64_t followed{-1};
    void fit() {zoom=0; follow=true; followed=-1;}   // the newest frame, whole
    // wire: the cell is one of the circuit's own cells (decided by the solver), not the fixed scaffold.
    using CellFn=std::function<void(ims::FrameMachine::Cell,int digit,float sx,float sy,float size,bool wire)>;
    // Applies dragging and the wheel over the rectangle, then calls cell() for
    // the visible cells (sampled when far out) and border() with the screen x
    // of each frame's right edge and the board's top and bottom. A solved safe
    // cell is shown opened, with the count of mines around it: every cell of a
    // frame is forced, so the digit is the board's one answer.
    void show(const ims::FrameMachine& m,ImVec2 p0,ImVec2 size,bool hovered,bool active,const CellFn& cell,const std::function<void(float,float,float)>& border);
};
// One board cell drawn like classic Minesweeper: raised grey tiles, a flag on
// every mine (orange-backed on key cells), revealed digits in the classic
// colours, 0 included. Far out (size < 8 px) cells become plain colours.
// wire tints the circuit's own cells very slightly, so the wires show.
void drawClassicCell(ImDrawList* d,ims::FrameMachine::Cell c,int digit,float x,float y,float size,bool wire=false);
}
