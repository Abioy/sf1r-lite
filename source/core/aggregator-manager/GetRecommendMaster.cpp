#include "GetRecommendMaster.h"
#include <node-manager/MasterManagerBase.h>
#include <node-manager/sharding/RecommendShardStrategy.h>
#include <node-manager/Sf1rTopology.h>

#include <memory> // for auto_ptr
#include <set>

namespace
{

template <class ContainerType>
void limitSize(ContainerType& container, std::size_t limit)
{
    if (container.size() > limit)
    {
        container.erase(container.begin() + limit,
                        container.end());
    }
}

}

namespace sf1r
{

GetRecommendMaster::GetRecommendMaster(
    const std::string& collection,
    RecommendShardStrategy* shardStrategy,
    GetRecommendWorker* localWorker
)
    : collection_(collection)
    , merger_(new GetRecommendMerger)
    , shardStrategy_(shardStrategy)
{
    std::auto_ptr<GetRecommendMergerProxy> mergerProxy(new GetRecommendMergerProxy(merger_.get()));
    merger_->bindCallProxy(*mergerProxy);

    std::auto_ptr<GetRecommendWorkerProxy> localWorkerProxy;
    if (localWorker)
    {
        localWorkerProxy.reset(new GetRecommendWorkerProxy(localWorker));
        localWorker->bindCallProxy(*localWorkerProxy);
    }

    aggregator_.reset(
        new GetRecommendAggregator(mergerProxy.get(), localWorkerProxy.get(),
            Sf1rTopology::getServiceName(Sf1rTopology::RecommendService), collection));

    mergerProxy.release();
    localWorkerProxy.release();

    MasterManagerBase::get()->registerAggregator(aggregator_);
}

GetRecommendMaster::~GetRecommendMaster()
{
    MasterManagerBase::get()->unregisterAggregator(aggregator_);
}

void GetRecommendMaster::recommendPurchase(
    const RecommendInputParam& inputParam,
    idmlib::recommender::RecommendItemVec& results
)
{
    std::set<itemid_t> candidateSet;
    aggregator_->distributeRequest(collection_, 0, "getCandidateSet",
                                   inputParam.inputItemIds, candidateSet);

    if (candidateSet.empty())
        return;

    aggregator_->distributeRequest(collection_, 0, "recommendFromCandidateSet",
                                   inputParam, candidateSet, results);

    limitSize(results, inputParam.limit);
}

void GetRecommendMaster::recommendPurchaseFromWeight(
    const RecommendInputParam& inputParam,
    idmlib::recommender::RecommendItemVec& results
)
{
    std::set<itemid_t> candidateSet;
    aggregator_->distributeRequest(collection_, 0, "getCandidateSetFromWeight",
                                   inputParam.itemWeightMap, candidateSet);

    if (candidateSet.empty())
        return;

    aggregator_->distributeRequest(collection_, 0, "recommendFromCandidateSetWeight",
                                   inputParam, candidateSet, results);

    limitSize(results, inputParam.limit);
}

void GetRecommendMaster::recommendVisit(
    const RecommendInputParam& inputParam,
    idmlib::recommender::RecommendItemVec& results
)
{
    shardid_t workerId = shardStrategy_->getShardId(inputParam.inputItemIds[0]);
    aggregator_->singleRequest(collection_, 0, "recommendVisit", inputParam, results, workerId);

    limitSize(results, inputParam.limit);
}

} // namespace sf1r
