#pragma once
#include <cstdint>

namespace veyra::engine {
// Provider totals belong to one swapchain/session. A resize can replace that
// session without changing the user's settings revision.
class ProviderSubmissionCounter {
public:
    struct Delta {uint64_t presented=0,generated=0;bool restarted=false;};
    void reset(uint64_t generation,uint64_t presented,uint64_t generated){
        generation_=generation;presented_=presented;generated_=generated;initialized_=true;
    }
    Delta observe(uint64_t generation,uint64_t presented,uint64_t generated){
        const bool restarted=initialized_&&(generation!=generation_||presented<presented_||generated<generated_);
        const bool continuous=initialized_&&!restarted;
        const Delta delta{presented-(continuous?presented_:0),generated-(continuous?generated_:0),restarted};
        reset(generation,presented,generated);
        return delta;
    }
private:
    uint64_t generation_=0,presented_=0,generated_=0;
    bool initialized_=false;
};
}
