#pragma once
#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <utility>

namespace veyra::engine {
// Owner-thread mailbox for infrequent display/driver queries. One query may
// be in flight; polling never waits. A window/graph replacement invalidates
// its result without destroying (and therefore joining) the running future.
// The worker must capture values only, never presenter/GPU resources. The
// future joins once at destruction, outside the per-frame playback path.
template<class Value> class AsyncDisplayQuery {
    struct Result {uint64_t generation;Value value;};
    std::future<Result> pending_;
    uint64_t generation_=0;
public:
    void invalidate(){++generation_;}
    bool pending()const{return pending_.valid();}
    template<class Query> bool request(Query query){
        if(pending())return false;
        const auto generation=generation_;
        pending_=std::async(std::launch::async,[generation,query=std::move(query)]()mutable{
            return Result{generation,query()};
        });
        return true;
    }
    std::optional<Value> poll(){
        if(!pending()||pending_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
        auto result=pending_.get();
        if(result.generation!=generation_)return {};
        return std::move(result.value);
    }
};
}
