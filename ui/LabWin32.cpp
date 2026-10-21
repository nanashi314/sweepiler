// The Windows side of LabCommon: file dialogs, files, fonts and screenshots.
#include "LabCommon.h"
#include <fstream>
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
namespace lab {
std::optional<std::filesystem::path> openProgramDialog() {
    wchar_t path[MAX_PATH]{}; OPENFILENAMEW ofn{sizeof(ofn)}; ofn.lpstrFilter=L"Programs (*.mc, *.imsg)\0*.mc;*.imsg\0All files\0*.*\0"; ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&ofn)) return std::filesystem::path(path);
    return std::nullopt;
}
std::optional<std::filesystem::path> saveProgramDialog(const std::string& suggested) {
    wchar_t path[MAX_PATH]{}; for(size_t i=0;i<suggested.size() && i+1<MAX_PATH;++i) path[i]=static_cast<wchar_t>(static_cast<unsigned char>(suggested[i]));
    OPENFILENAMEW ofn{sizeof(ofn)}; ofn.lpstrFilter=L"MineC (*.mc)\0*.mc\0Machine language (*.imsg)\0*.imsg\0All files\0*.*\0"; ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrDefExt=L"mc"; ofn.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
    if(GetSaveFileNameW(&ofn)) return std::filesystem::path(path);
    return std::nullopt;
}
void writeTextFile(const std::filesystem::path& path,const std::string& text) {
    std::ofstream out(path,std::ios::binary); out<<text; if(!out) throw std::runtime_error("Cannot write "+path.string());
}
void loadFonts() {
    auto& io=ImGui::GetIO();
    io.Fonts->AddFontDefault();
    // Japanese text (the reference, comments in code); the pixel font covers ASCII.
    for(const char* f:{"C:/Windows/Fonts/BIZ-UDGothicR.ttc","C:/Windows/Fonts/YuGothM.ttc","C:/Windows/Fonts/meiryo.ttc"})
        if(std::filesystem::exists(f)) {io.UserData=io.Fonts->AddFontFromFileTTF(f,18.0f,nullptr,io.Fonts->GetGlyphRangesJapanese()); break;}
}
bool saveBackBuffer(ID3D11Device* device,ID3D11DeviceContext* context,IDXGISwapChain* swap,const std::string& path) {
    ID3D11Texture2D* back{}; if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC desc; back->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    ID3D11Texture2D* staging{}; bool ok=SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging));
    if(ok) {
        context->CopyResource(staging,back);
        D3D11_MAPPED_SUBRESOURCE map{};
        ok=SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map));
        if(ok) {
            const int w=static_cast<int>(desc.Width),h=static_cast<int>(desc.Height),row=(w*3+3)&~3;
            BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
            fh.bfType=0x4D42; fh.bfOffBits=sizeof(fh)+sizeof(ih); fh.bfSize=fh.bfOffBits+static_cast<DWORD>(row*h);
            ih.biSize=sizeof(ih); ih.biWidth=w; ih.biHeight=h; ih.biPlanes=1; ih.biBitCount=24;
            std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<char*>(&fh),sizeof fh); out.write(reinterpret_cast<char*>(&ih),sizeof ih);
            std::string line(static_cast<size_t>(row),'\0');
            for(int y=h-1;y>=0;--y) {
                const auto* src=static_cast<const uint8_t*>(map.pData)+static_cast<size_t>(y)*map.RowPitch;
                for(int x=0;x<w;++x) {line[static_cast<size_t>(x*3)]=static_cast<char>(src[x*4+2]); line[static_cast<size_t>(x*3+1)]=static_cast<char>(src[x*4+1]); line[static_cast<size_t>(x*3+2)]=static_cast<char>(src[x*4]);}
                out.write(line.data(),row);
            }
            context->Unmap(staging,0); ok=out.good();
        }
        staging->Release();
    }
    back->Release(); return ok;
}
}
