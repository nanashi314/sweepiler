#include "BuildControl.h"
namespace ims {
namespace {thread_local BuildScope* current{};}
BuildScope::BuildScope(std::stop_token stop,std::function<void(BuildProgress)> progress):stop_(stop),progress_(std::move(progress)),previous_(current){current=this;}
BuildScope::~BuildScope(){current=previous_;}
void buildCheckpoint(const char* phase,size_t done,size_t total){if(!current)return;if(current->stop_.stop_requested())throw BuildCancelled();if(current->progress_)current->progress_({phase,done,total});}
}
