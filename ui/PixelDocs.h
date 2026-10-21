#pragma once
#include <functional>
#include <string>
#include <vector>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ImGuiContext;
struct ImFont;
namespace lab {
// One page of the MineC reference. Markup in body: blank lines separate
// paragraphs; "- " bullets; "| a | b" rows of a two-column table; "```"
// fences code; "@sample NAME" a button that opens the sample in the editor.
struct DocTopic {const char* group; const char* title; const char* body;};
const std::vector<DocTopic>& mineCTopics();

// The reference in its own window, drawn in the pixel style with a topic tree.
// It has its own swap chain and ImGui context on the lab's D3D device; the
// lab calls render() once per frame.
class PixelDocs {
public:
    PixelDocs(ID3D11Device* device,ID3D11DeviceContext* context);
    ~PixelDocs();
    void open();
    bool isOpen() const {return window_!=nullptr;}
    void render();
    void* window() const {return window_;}
    void showTopic(size_t topic) {if(topic<mineCTopics().size()) {topic_=topic; reveal_=true;}}
    void capture(const std::string& path) {capturePath_=path;}   // saved at the next render
    // For recording: draws the reference in the current ImGui context (the lab's), at a topic and scroll.
    void drawHere(float width,float height,float pixel=0) {pixel_=pixel; ui(width,height); pixel_=0;}
    void setScroll(float scroll) {scroll_=scroll; scrolledFor_=topic_;}
    void openGroup(const std::string& group,bool open);
    std::function<void(const std::string&)> openSample;   // "open in editor"
    // Messages for the window (used by its window procedure).
    long long handle(void* hwnd,unsigned msg,unsigned long long w,long long l);
private:
    void ui(float width,float height);
    void close();
    ID3D11Device* device_; ID3D11DeviceContext* context_;
    void* window_{};
    void* swapChain_{}; void* target_{};
    ImGuiContext* imgui_{};
    ImFont* japanese_{};
    unsigned resizeW_{},resizeH_{};
    bool closing_{};
    std::string capturePath_;
    size_t topic_{0};
    std::vector<bool> open_;       // expanded groups
    float scroll_{},treeScroll_{};
    float pixel_{};                // a fixed virtual pixel size (recording), 0 picks one by the window size
    bool reveal_{true};            // open the chosen topic's group once (after paging)
    bool dragBar_{};
    size_t scrolledFor_{static_cast<size_t>(-1)};
};
}
