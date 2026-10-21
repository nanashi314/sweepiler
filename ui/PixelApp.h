#pragma once
#include "LabCommon.h"
#include "PixelEditor.h"
#include <string_view>
namespace lab {
// A pixel-art, game-like UI. Everything is laid out on a 400 x 250 virtual
// screen of chunky pixels, scaled by a whole number to fit the window, with
// the PICO-8 palette and a 5 x 7 pixel font.
class PixelApp:public LabUi {
public:
    explicit PixelApp(ims::MachineSession& session);
    const char* name() const override {return "Pixel";}
    void activate() override;
    void frame(float width,float height) override;
    void choose(size_t program) override;
    void buildChosen() override;
    void showTab(int tab) override {tab_=tab;}   // 0 screen, 1 board, 2 code
    bool runCodeFile(const std::filesystem::path& path) override;
    void setBoardZoom(double z) override {camera_.zoom=z;}
    // The reference window that REF and the CODE tab open.
    void setDocs(class PixelDocs* docs) {docs_=docs;}
    // Opens a sample's source in the editor (from the reference).
    void openSample(const std::string& label);
    // What NEW does: an untitled MineC program with this text, in the editor.
    void newCode(const std::string& text);
    // What OPEN does with the chosen file: its text in the editor.
    void openCodeFile(const std::filesystem::path& path);
    // For automated runs: types text at the end of the code, as if from the keyboard.
    void showSamples() {tab_=2; samples_=true;}
    // For recording: a fixed scale and origin of the virtual screen, the board camera and the editor.
    void setFixedLayout(float scale,ImVec2 origin) {fixedScale_=scale; fixedOrigin_=origin;}
    // Upright phones: the screen, the status and the program list in one column.
    void setPortrait(bool on) {portrait_=on;}
    // The virtual screen in chunky pixels: 400 x 250, or 264 x 441 in one column.
    ImVec2 virtualSize() const {return portrait_?ImVec2{264,441}:ImVec2{400,250};}
    BoardCamera& camera() {return camera_;}
    PixelEditor& editor() {return editor_;}
    int tab() const {return tab_;}
    ImVec2 capCenter(size_t key) const {return key<capCenters_.size()?capCenters_[key]:ImVec2{-1,-1};}
    void typeInEditor(const std::string& text) {tab_=2; editor_.focus(); editor_.moveToEnd(); ImGui::GetIO().AddInputCharactersUTF8(text.c_str());}
private:
    // Virtual-pixel drawing.
    ImVec2 at(float x,float y) const {return {origin_.x+x*scale_,origin_.y+y*scale_};}
    void rect(float x,float y,float w,float h,ImU32 c);
    void text(float x,float y,std::string_view t,ImU32 c,float size=1);
    void textCentered(float cx,float y,std::string_view t,ImU32 c,float size=1);
    void panel(float x,float y,float w,float h,std::string_view title);
    bool button(const char* id,float x,float y,float w,float h,std::string_view label,ImU32 face,ImU32 ink,bool enabled=true);
    void menu(const ims::MachineSession::Status& s);
    void screen(const ims::MachineSession::Status& s,float x,float y,float w,float h);
    void board(float x,float y,float w,float h);
    void status(const ims::MachineSession::Status& s);
    void code(float x,float y,float w,float h);
    void loadCode(size_t program);
    void runCode();
    ims::MachineSession& session_;
    std::vector<ims::MachineProgram> programs_;
    size_t choice_{0};
    std::optional<std::filesystem::path> file_;
    int tab_{0};
    BoardCamera camera_;
    ImDrawList* draw_{};
    ImVec2 origin_{};
    float scale_{1};
    double time_{};
    // The code editor: its text, language, name and file, and the last compile error.
    PixelEditor editor_;
    ims::ProgramLanguage codeLanguage_{ims::ProgramLanguage::MineC};
    std::string codeName_{"untitled"};
    std::optional<std::filesystem::path> codePath_;
    bool dirty_{};
    std::string error_;
    int errorLine_{};
    class PixelDocs* docs_{};
    std::vector<uint8_t> capHeld_;   // key caps held down with the mouse
    std::vector<ImVec2> capCenters_;
    bool samples_{};                 // the sample picker is open
    int listScroll_{};               // first row shown in the program list
    int pickScroll_{};
    bool portrait_{};
    float fixedScale_{};
    ImVec2 fixedOrigin_{};               // first row of cards shown in the sample picker
    size_t listFollowed_{static_cast<size_t>(-1)};
    std::vector<size_t> sampleGates_;
    void samplePicker(float x,float y,float w,float h);
};
}
