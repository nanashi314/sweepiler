// The browser side of LabCommon. OPEN picks a file with the browser's picker,
// which delivers it later (web_opened in WebMain.cpp); saving downloads the file.
#include "LabCommon.h"
#include <emscripten.h>
#include <fstream>
namespace lab {
EM_JS(void,pickProgramFile,(),{
    var input=document.createElement('input'); input.type='file'; input.accept='.mc,.imsg,.txt';
    input.onchange=function(){
        var f=input.files[0]; if(!f) return;
        f.arrayBuffer().then(function(buffer){
            try{FS.mkdir('/uploads');}catch(e){}
            var path='/uploads/'+f.name.split('/').join('_').split(String.fromCharCode(92)).join('_'); FS.writeFile(path,new Uint8Array(buffer));
            Module.ccall('web_opened',null,['string'],[path]);
        });
    };
    input.click();
});
EM_JS(void,downloadText,(const char* name,const char* data,int size),{
    var blob=new Blob([HEAPU8.slice(data,data+size)],{type:'text/plain'});
    var a=document.createElement('a'); a.href=URL.createObjectURL(blob); a.download=UTF8ToString(name); a.click();
    setTimeout(function(){URL.revokeObjectURL(a.href);},1000);
});
std::optional<std::filesystem::path> openProgramDialog() {pickProgramFile(); return std::nullopt;}
std::optional<std::filesystem::path> saveProgramDialog(const std::string& suggested) {return std::filesystem::path("/downloads")/suggested;}
void writeTextFile(const std::filesystem::path& path,const std::string& text) {
    downloadText(path.filename().string().c_str(),text.data(),static_cast<int>(text.size()));
}
void loadFonts() {
    auto& io=ImGui::GetIO();
    io.Fonts->AddFontDefault();
    // Japanese text (the reference, comments in code), from the page's preloaded data.
    const char* f="/fonts/BIZUDGothic-Regular.ttf";
    if(std::filesystem::exists(f)) io.UserData=io.Fonts->AddFontFromFileTTF(f,18.0f,nullptr,io.Fonts->GetGlyphRangesJapanese());
}
}
