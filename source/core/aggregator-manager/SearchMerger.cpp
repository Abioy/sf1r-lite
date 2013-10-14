#include "SearchMerger.h"
#include "MergeComparator.h"

#include <common/ResultType.h>
#include <common/Utilities.h>
#include <mining-manager/MiningManager.h>
#include <mining-manager/summarization-submanager/Summarization.h>
#include <mining-manager/taxonomy-generation-submanager/TaxonomyRep.h>
#include <mining-manager/taxonomy-generation-submanager/TaxonomyGenerationSubManager.h>

#include <algorithm> // min
#include <vector>

#define TOP_LABEL_NUM 4

namespace sf1r
{

void SearchMerger::getDistSearchInfo(const net::aggregator::WorkerResults<DistKeywordSearchInfo>& workerResults, DistKeywordSearchInfo& mergeResult)
{
    LOG(INFO) << "#[SearchMerger::getDistSearchInfo] " << workerResults.size() << endl;

    size_t resultNum = workerResults.size();

    for (size_t i = 0; i < resultNum; i++)
    {
        DistKeywordSearchInfo& wResult = const_cast<DistKeywordSearchInfo&>(workerResults.result(i));

        DocumentFrequencyInProperties::iterator dfiter;
        for (dfiter = wResult.dfmap_.begin(); dfiter != wResult.dfmap_.end(); dfiter++)
        {
            const std::string& property = dfiter->first;

            ID_FREQ_MAP_T& df = wResult.dfmap_[property];
            ID_FREQ_UNORDERED_MAP_T::iterator iter_;
            for (iter_ = df.begin(); iter_ != df.end(); iter_++)
            {
                mergeResult.dfmap_[property][iter_->first] += iter_->second;
            }
        }

        CollectionTermFrequencyInProperties::iterator ctfiter;
        for (ctfiter = wResult.ctfmap_.begin(); ctfiter != wResult.ctfmap_.end(); ctfiter++)
        {
            const std::string& property = ctfiter->first;

            ID_FREQ_MAP_T& ctf = wResult.ctfmap_[property];
            ID_FREQ_UNORDERED_MAP_T::iterator iter_;
            for (iter_ = ctf.begin(); iter_ != ctf.end(); iter_++)
            {
                mergeResult.ctfmap_[property][iter_->first] += iter_->second;
            }
        }

        MaxTermFrequencyInProperties::iterator maxtf_it;
        for (maxtf_it = wResult.maxtfmap_.begin(); maxtf_it != wResult.maxtfmap_.end(); maxtf_it++)
        {
            const std::string& property = maxtf_it->first;

            ID_FREQ_MAP_T& maxtf = wResult.maxtfmap_[property];
            ID_FREQ_UNORDERED_MAP_T::iterator iter_;
            for (iter_ = maxtf.begin(); iter_ != maxtf.end(); iter_++)
            {
                mergeResult.maxtfmap_[property][iter_->first] += iter_->second;
            }
        }
    }
    LOG(INFO) << "#[SearchMerger::getDistSearchInfo] end" << endl;
}

void SearchMerger::getDistSearchResult(const net::aggregator::WorkerResults<KeywordSearchResult>& workerResults, KeywordSearchResult& mergeResult)
{
    LOG(INFO) << "#[SearchMerger::getDistSearchResult] " << workerResults.size() << endl;

    size_t workerNum = workerResults.size();
    if (workerNum == 0)
    {
        LOG(ERROR) << "empty worker result .";
        mergeResult.error_ = "empty worker.";
        return;
    }

    for(size_t workerId = 0; workerId < workerNum; ++workerId)
    {
        if(!workerResults.result(workerId).error_.empty())
        {
            mergeResult.error_ = workerResults.result(workerId).error_;
            LOG(ERROR) << "!!! getDistSearchResult error for worker: " << workerId << ", error: " << mergeResult.error_;
            return;
        }
    }

    const KeywordSearchResult& result0 = workerResults.result(0);

    // only one result
    if (workerNum == 1)
    {
        mergeResult = result0;
        mergeResult.topKWorkerIds_.resize(mergeResult.topKDocs_.size(), workerResults.workerId(0));
        return;
    }

    // Set basic info
    mergeResult.collectionName_ = result0.collectionName_;
    mergeResult.encodingType_ = result0.encodingType_;
    mergeResult.rawQueryString_ = result0.rawQueryString_;
    mergeResult.start_ = result0.start_;
    mergeResult.count_ = result0.count_;
    mergeResult.analyzedQuery_ = result0.analyzedQuery_;
    mergeResult.queryTermIdList_ = result0.queryTermIdList_;
    mergeResult.propertyQueryTermList_ = result0.propertyQueryTermList_;
    mergeResult.totalCount_ = 0;
    mergeResult.TOP_K_NUM = result0.TOP_K_NUM;
    mergeResult.distSearchInfo_.isDistributed_ = result0.distSearchInfo_.isDistributed_;

    size_t totalTopKCount = 0;
    bool hasCustomRankScore = false;
    float rangeLow = numeric_limits<float>::max(), rangeHigh = numeric_limits<float>::min();
    mergeResult.attrRep_ = result0.attrRep_;
    std::list<const faceted::OntologyRep*> otherAttrReps;
    bool is_need_get_docs = false;
    for (size_t i = 0; i < workerNum; i++)
    {
        const KeywordSearchResult& wResult = workerResults.result(i);
        //wResult.print();

        if (!wResult.distSearchInfo_.include_summary_data_)
            is_need_get_docs = true;

        mergeResult.totalCount_ += wResult.totalCount_;
        totalTopKCount += wResult.topKDocs_.size();

        if (wResult.topKCustomRankScoreList_.size() > 0)
            hasCustomRankScore = true;

        if (wResult.propertyRange_.lowValue_ < rangeLow)
        {
            rangeLow = wResult.propertyRange_.lowValue_;
        }
        if (wResult.propertyRange_.highValue_ > rangeHigh)
        {
            rangeHigh = wResult.propertyRange_.highValue_;
        }

        mergeResult.groupRep_.merge(wResult.groupRep_);
        faceted::GroupParam::mergeScoreGroupLabel(mergeResult.autoSelectGroupLabels_, wResult.autoSelectGroupLabels_, TOP_LABEL_NUM);

        std::map<std::string,unsigned>::const_iterator cit = wResult.counterResults_.begin();
        for(; cit != wResult.counterResults_.end(); ++cit)
        {
            mergeResult.counterResults_[cit->first] += cit->second;
        }
        if (i > 0)
        {
            otherAttrReps.push_back(&wResult.attrRep_);
        }
    }
    mergeResult.propertyRange_.lowValue_ = rangeLow;
    mergeResult.propertyRange_.highValue_ = rangeHigh;
    mergeResult.attrRep_.merge(0, otherAttrReps);

    //size_t endOffset = mergeResult.start_ + mergeResult.count_;
    size_t topKStart = Utilities::roundDown(mergeResult.start_, mergeResult.TOP_K_NUM);
    size_t topKCount = 0;
    if (topKStart < totalTopKCount)
        topKCount = totalTopKCount - topKStart;

    topKCount = std::min((size_t)mergeResult.TOP_K_NUM, topKCount);

    LOG(INFO) << "SearchMerger topKStart : << " << topKStart
        << ", topKCount: " << topKCount
        << ", totalTopKCount: " << totalTopKCount
        << ", TOP_K_NUM: " << mergeResult.TOP_K_NUM
        << ", top label num: " << mergeResult.autoSelectGroupLabels_["Category"].size() << endl;

    mergeResult.topKDocs_.resize(topKCount);
    mergeResult.topKWorkerIds_.resize(topKCount);
    mergeResult.topKRankScoreList_.resize(topKCount);

    if (hasCustomRankScore)
        mergeResult.topKCustomRankScoreList_.resize(topKCount);

    // Prepare comparator for each sub result (compare by sort properties).
    DocumentComparator** docComparators = new DocumentComparator*[workerNum];
    for (size_t i = 0; i < workerNum; i++)
    {
        docComparators[i] = new DocumentComparator(workerResults.result(i));
    }
    // Merge topK docs (use Loser Tree to optimize k-way merge)
    size_t maxi;
    size_t* iter = new size_t[workerNum];
    memset(iter, 0, sizeof(size_t)*workerNum);
    std::vector<std::pair<size_t, size_t> > pageOffsetInWorker;
    pageOffsetInWorker.resize(topKCount);

    for (size_t cnt = 0; cnt < topKCount; cnt++)
    {
        // find doc which should be merged firstly from heads of multiple doc lists (sorted).
        maxi = size_t(-1);
        for (size_t i = 0; i < workerNum; i++)
        {
            const std::vector<docid_t>& subTopKDocs = workerResults.result(i).topKDocs_;
            if (iter[i] >= subTopKDocs.size())
                continue;

            if (maxi == size_t(-1))
            {
                maxi = i;
                continue;
            }

            if (greaterThan(docComparators[i], iter[i], docComparators[maxi], iter[maxi]))
            {
                maxi = i;
            }
        }

        //std::cout << "maxi: "<< maxi<<", iter[maxi]: " << iter[maxi]<<endl;
        if (maxi == size_t(-1))
        {
            break;
        }

        // get a result
        const workerid_t& workerid = workerResults.workerId(maxi);
        const KeywordSearchResult& wResult = workerResults.result(maxi);

        pageOffsetInWorker[cnt] = std::make_pair(maxi, iter[maxi]);
        mergeResult.topKDocs_[cnt] = wResult.topKDocs_[iter[maxi]];
        mergeResult.topKWorkerIds_[cnt] = workerid;
        mergeResult.topKRankScoreList_[cnt] = wResult.topKRankScoreList_[iter[maxi]];
        if (hasCustomRankScore && wResult.topKCustomRankScoreList_.size() > 0)
            mergeResult.topKCustomRankScoreList_[cnt] = wResult.topKCustomRankScoreList_[iter[maxi]];

        // next
        iter[maxi] ++;
    }

    delete[] iter;
    for (size_t i = 0; i < workerNum; i++)
    {
        delete docComparators[i];
    }
    delete[] docComparators;
    LOG(INFO) << "#[SearchMerger::getDistSearchResult] finished";

    getMiningResult(workerResults, mergeResult);

    mergeResult.distSearchInfo_.include_summary_data_ = !is_need_get_docs;
    if (!is_need_get_docs)
    {
        size_t pageCount = mergeResult.count_;
        if( mergeResult.start_ < mergeResult.topKDocs_.size() )
        {
            pageCount = std::min(pageCount, mergeResult.topKDocs_.size() - mergeResult.start_);
        }
        else
        {
            pageCount = 0;
        }
        size_t displayPropertyNum = workerResults.result(0).snippetTextOfDocumentInPage_.size();
        size_t isSummaryOn = workerResults.result(0).rawTextOfSummaryInPage_.size();
        LOG(INFO) << "begin merge the documents since the data is included. "
            << "displayPropertyNum: " << displayPropertyNum << ", summary: " << isSummaryOn;

        // initialize summary info for result
        mergeResult.snippetTextOfDocumentInPage_.clear();
        mergeResult.snippetTextOfDocumentInPage_.resize(displayPropertyNum);
        mergeResult.fullTextOfDocumentInPage_.clear();
        mergeResult.fullTextOfDocumentInPage_.resize(displayPropertyNum);
        mergeResult.rawTextOfSummaryInPage_.clear();
        if (isSummaryOn)
        {
            mergeResult.rawTextOfSummaryInPage_.resize(isSummaryOn);
        }
        for (size_t dis = 0; dis < displayPropertyNum; dis++)
        {
            mergeResult.snippetTextOfDocumentInPage_[dis].resize(pageCount);
            mergeResult.fullTextOfDocumentInPage_[dis].resize(pageCount);
        }
        for (size_t dis = 0; dis < isSummaryOn; dis++)
        {
            mergeResult.rawTextOfSummaryInPage_[dis].resize(pageCount);
        }

        size_t pageEnd = std::min(mergeResult.topKDocs_.size(), mergeResult.start_ + pageCount);
        for (size_t topkIndex = mergeResult.start_; topkIndex < pageEnd; ++topkIndex)
        {
            std::size_t curWorker = pageOffsetInWorker[topkIndex].first;
            std::size_t workerOffset = pageOffsetInWorker[topkIndex].second;
            const KeywordSearchResult& workerResult = workerResults.result(curWorker);

            size_t i = topkIndex - mergeResult.start_;

            for (size_t dis = 0; dis < displayPropertyNum; ++dis)
            {
                if (workerResult.snippetTextOfDocumentInPage_.size() > dis && workerResult.snippetTextOfDocumentInPage_[dis].size() > workerOffset)
                    mergeResult.snippetTextOfDocumentInPage_[dis][i] = workerResult.snippetTextOfDocumentInPage_[dis][workerOffset];
                if (workerResult.fullTextOfDocumentInPage_.size() > dis && workerResult.fullTextOfDocumentInPage_[dis].size() > workerOffset)
                    mergeResult.fullTextOfDocumentInPage_[dis][i] = workerResult.fullTextOfDocumentInPage_[dis][workerOffset];
            }
            for (size_t dis = 0; dis < isSummaryOn; ++dis)
            {
                if (workerResult.rawTextOfSummaryInPage_.size() > dis && workerResult.rawTextOfSummaryInPage_[dis].size() > workerOffset)
                    mergeResult.rawTextOfSummaryInPage_[dis][i] = workerResult.rawTextOfSummaryInPage_[dis][workerOffset];
            }
        }
    }
}

void SearchMerger::getSummaryResult(const net::aggregator::WorkerResults<KeywordSearchResult>& workerResults, KeywordSearchResult& mergeResult)
{
    const size_t workerNum = workerResults.size();

    if (workerNum == 0)
    {
        LOG(ERROR) << "empty worker result .";
        return;
    }

    for(size_t workerId = 0; workerId < workerNum; ++workerId)
    {
        if(!workerResults.result(workerId).error_.empty())
        {
            mergeResult.error_ = workerResults.result(workerId).error_;
            LOG(ERROR) << "!!! getSummaryResult error for worker: " << workerId << ", error: " << mergeResult.error_;
            return;
        }
        LOG(INFO) << "displayNum: " << workerResults.result(workerId).snippetTextOfDocumentInPage_.size();
        LOG(INFO) << "displayNum 2: " << workerResults.result(workerId).fullTextOfDocumentInPage_.size();
    }

    size_t pageCount = mergeResult.count_;
    size_t displayPropertyNum = workerResults.result(0).snippetTextOfDocumentInPage_.size();
    size_t isSummaryOn = workerResults.result(0).rawTextOfSummaryInPage_.size();

    LOG(INFO) << "#[SearchMerger::getSummaryResult] begin. pageCount:" << pageCount
        << ", displayPropertyNum:" << displayPropertyNum << ", summary:" << isSummaryOn
        << ", workNum: " << workerNum;

    // initialize summary info for result
    mergeResult.snippetTextOfDocumentInPage_.clear();
    mergeResult.snippetTextOfDocumentInPage_.resize(displayPropertyNum);
    mergeResult.fullTextOfDocumentInPage_.clear();
    mergeResult.fullTextOfDocumentInPage_.resize(displayPropertyNum);
    mergeResult.rawTextOfSummaryInPage_.clear();
    if (isSummaryOn)
    {
        mergeResult.rawTextOfSummaryInPage_.resize(isSummaryOn);
    }

    for (size_t dis = 0; dis < displayPropertyNum; dis++)
    {
        mergeResult.snippetTextOfDocumentInPage_[dis].resize(pageCount);
        mergeResult.fullTextOfDocumentInPage_[dis].resize(pageCount);
    }
    for (size_t dis = 0; dis < isSummaryOn; dis++)
    {
        mergeResult.rawTextOfSummaryInPage_[dis].resize(pageCount);
    }

    std::vector<std::size_t> workerOffsetVec(workerNum);
    for (size_t i = 0; i < pageCount; ++i)
    {
        std::size_t curWorker = 0;
        while (curWorker < workerNum)
        {
            const std::size_t workerOffset = workerOffsetVec[curWorker];
            const std::vector<std::size_t>& pageOffsetList = workerResults.result(curWorker).pageOffsetList_;

            if (workerOffset < pageOffsetList.size() && pageOffsetList[workerOffset] == i)
                break;

            ++curWorker;
        }

        if (curWorker == workerNum)
            break;

        const KeywordSearchResult& workerResult = workerResults.result(curWorker);
        std::size_t& workerOffset = workerOffsetVec[curWorker];

        for (size_t dis = 0; dis < displayPropertyNum; ++dis)
        {
            if (workerResult.snippetTextOfDocumentInPage_.size() > dis && workerResult.snippetTextOfDocumentInPage_[dis].size() > workerOffset)
                mergeResult.snippetTextOfDocumentInPage_[dis][i] = workerResult.snippetTextOfDocumentInPage_[dis][workerOffset];
            if (workerResult.fullTextOfDocumentInPage_.size() > dis && workerResult.fullTextOfDocumentInPage_[dis].size() > workerOffset)
                mergeResult.fullTextOfDocumentInPage_[dis][i] = workerResult.fullTextOfDocumentInPage_[dis][workerOffset];
        }
        for (size_t dis = 0; dis < isSummaryOn; ++dis)
        {
            if (workerResult.rawTextOfSummaryInPage_.size() > dis && workerResult.rawTextOfSummaryInPage_[dis].size() > workerOffset)
                mergeResult.rawTextOfSummaryInPage_[dis][i] = workerResult.rawTextOfSummaryInPage_[dis][workerOffset];
        }

        ++workerOffset;
    }
    LOG(INFO) << "#[SearchMerger::getSummaryResult] end";
}

void SearchMerger::getSummaryMiningResult(const net::aggregator::WorkerResults<KeywordSearchResult>& workerResults, KeywordSearchResult& mergeResult)
{
    LOG(INFO) << "#[SearchMerger::getSummaryMiningResult] " << workerResults.size() << endl;

    getSummaryResult(workerResults, mergeResult);
    LOG(INFO) << "#[SearchMerger::getSummaryMiningResult] end";
}

void SearchMerger::getMiningResult(const net::aggregator::WorkerResults<KeywordSearchResult>& workerResults, KeywordSearchResult& mergeResult)
{
    if(!miningManager_) return;
    LOG(INFO) <<"call mergeMiningResult"<<std::endl;
    if (workerResults.size() == 0)
        return;

    size_t workerNum = workerResults.size();
    for(size_t i = 0; i < workerNum; ++i)
    {
        if(!workerResults.result(i).error_.empty())
        {
            mergeResult.error_ = workerResults.result(i).error_;
            LOG(ERROR) << "getMiningResult error for worker: " << i << ", error :" << mergeResult.error_;
            return;
        }
    }
    // note : the QrResult, TgResult, FacetedResult using any single result is enough.
    // the DupResult, SimilarityResult need to be re-design to get the right result.
    
    // OrResult
    mergeResult.relatedQueryList_ = workerResults.result(0).relatedQueryList_;
    mergeResult.rqScore_ = workerResults.result(0).rqScore_;
    // DupResult and SimilarityResult
    mergeResult.numberOfDuplicatedDocs_.resize(mergeResult.topKDocs_.size(), 0);
    mergeResult.numberOfSimilarDocs_.resize(mergeResult.topKDocs_.size(), 0);
    // the merge of duplicate and SimilarityResult should be the same as merge of rawtext.
    //
    //size_t dup_actual_size = 0;
    //size_t sim_actual_size = 0;
    //for (size_t doc_i = 0; doc_i < mergeResult.topKDocs_.size(); ++doc_i)
    //{
    //    for (size_t i = 0; i < workerResults.size(); ++i)
    //    {
    //        const KeywordSearchResult& result = workerResults.result(i);
    //        if (result.numberOfDuplicatedDocs_.size() > doc_i)
    //        {
    //            mergeResult.numberOfDuplicatedDocs_[doc_i] += result.numberOfDuplicatedDocs_[doc_i];
    //            dup_actual_size = doc_i + 1;
    //        }
    //        if (result.numberOfSimilarDocs_.size() > doc_i)
    //        {
    //            mergeResult.numberOfSimilarDocs_[doc_i] += result.numberOfSimilarDocs_[doc_i];
    //            sim_actual_size = doc_i + 1;
    //        }
    //    }
    //}
    //mergeResult.numberOfDuplicatedDocs_.resize(dup_actual_size);
    //mergeResult.numberOfSimilarDocs_.resize(sim_actual_size);
    // FacetedResult
    mergeResult.onto_rep_ = workerResults.result(0).onto_rep_;
    std::list<const faceted::OntologyRep*> other_onto_reps;
    for (size_t i = 1; i < workerResults.size(); ++i)
    {
        other_onto_reps.push_back(&workerResults.result(i).onto_rep_);
    }
    mergeResult.onto_rep_.merge(0, other_onto_reps);

    // TgResult.
    boost::shared_ptr<TaxonomyGenerationSubManager> tg_manager = miningManager_->GetTgManager();
    if(tg_manager)
    {
        std::vector<std::pair<uint32_t, idmlib::cc::CCInput32> > input_list;
        for(uint32_t i=0;i<workerResults.size();i++)
        {
            const idmlib::cc::CCInput32& tg_input = workerResults.result(i).tg_input;
            if(tg_input.concept_list.size()>0)
            {
                input_list.push_back(std::make_pair(workerResults.workerId(i), tg_input) );
            }
        }
        if( input_list.size()>0)
        {
            std::vector<sf1r::wdocid_t> top_wdoclist;
            mergeResult.getTopKWDocs(top_wdoclist);
            idmlib::cc::CCInput64 input;
            LOG(INFO)<<"before merging, size : "<<input_list.size()<<std::endl;
            tg_manager->AggregateInput(top_wdoclist, input_list, input);
            LOG(INFO)<<"after merging, concept size : "<<input.concept_list.size()<<" , doc size : "<<input.doc_list.size()<<std::endl;
            TaxonomyRep taxonomyRep;
            if( tg_manager->GetResult(input, taxonomyRep, mergeResult.neList_) )
            {
                taxonomyRep.fill(mergeResult);
            }
        }
    }
}

void SearchMerger::getDocumentsByIds(const net::aggregator::WorkerResults<RawTextResultFromSIA>& workerResults, RawTextResultFromSIA& mergeResult)
{
    LOG(INFO) << "#[SearchMerger::getDocumentsByIds] " << workerResults.size() << endl;

    size_t workerNum = workerResults.size();
    if (workerNum == 0)
    {
        mergeResult.error_ = "empty worker result.";
        LOG(ERROR) << "getDocumentsByIds empty worker result. ";
        return;
    }

    for(size_t i = 0; i < workerNum; ++i)
    {
        if(!workerResults.result(i).error_.empty())
        {
            mergeResult.error_ = workerResults.result(i).error_;
            LOG(ERROR) << "getDocumentsByIds error for worker: " << i << ", err:" << mergeResult.error_;
            return;
        }
    }

    // only one result
    if (workerNum == 1)
    {
        mergeResult = workerResults.result(0);
        mergeResult.workeridList_.resize(mergeResult.idList_.size(), workerResults.workerId(0));
        return;
    }

    mergeResult.idList_.clear();

    for (size_t w = 0; w < workerNum; w++)
    {
        workerid_t workerid = workerResults.workerId(w);
        const RawTextResultFromSIA& wResult = workerResults.result(w);

        for (size_t i = 0; i < wResult.idList_.size(); i++)
        {
            if (mergeResult.idList_.empty())
            {
                size_t displayPropertySize = wResult.fullTextOfDocumentInPage_.size();
                mergeResult.fullTextOfDocumentInPage_.resize(displayPropertySize);
                mergeResult.snippetTextOfDocumentInPage_.resize(displayPropertySize);
                mergeResult.rawTextOfSummaryInPage_.resize(wResult.rawTextOfSummaryInPage_.size());
            }

            // id and corresponding workerid
            mergeResult.idList_.push_back(wResult.idList_[i]);
            mergeResult.workeridList_.push_back(workerid);

            for (size_t p = 0; p < wResult.fullTextOfDocumentInPage_.size(); p++)
            {
                mergeResult.fullTextOfDocumentInPage_[p].push_back(wResult.fullTextOfDocumentInPage_[p][i]);
                mergeResult.snippetTextOfDocumentInPage_[p].push_back(wResult.snippetTextOfDocumentInPage_[p][i]);
            }

            for (size_t s = 0; s <wResult.rawTextOfSummaryInPage_.size(); s++)
            {
                mergeResult.snippetTextOfDocumentInPage_[s].push_back(wResult.snippetTextOfDocumentInPage_[s][i]);
            }
        }
    }
    LOG(INFO) << "#[SearchMerger::getDocumentsByIds] end"<< endl;
}

void SearchMerger::getInternalDocumentId(const net::aggregator::WorkerResults<uint64_t>& workerResults, uint64_t& mergeResult)
{
    mergeResult = 0;

    size_t workerNum = workerResults.size();
    for (size_t w = 0; w < workerNum; w++)
    {
        // Assume and DOCID should be unique in global space
        uint64_t wResult = workerResults.result(w);
        if (wResult != 0)
        {
            mergeResult = net::aggregator::Util::GetWDocId(workerResults.workerId(w), (uint32_t)wResult);
            return;
        }
    }
}

void SearchMerger::clickGroupLabel(const net::aggregator::WorkerResults<bool>& workerResults, bool& mergeResult)
{
    mergeResult = false;

    size_t workerNum = workerResults.size();
    for (size_t i = 0; i < workerNum; ++i)
    {
        if (workerResults.result(i))
        {
            mergeResult = true;
            return;
        }
    }
}

void SearchMerger::splitSearchResultByWorkerid(const KeywordSearchResult& totalResult, std::map<workerid_t, KeywordSearchResult>& resultMap)
{
    std::size_t i = 0;
    if (totalResult.TOP_K_NUM == 0 || totalResult.topKDocs_.size() == 0)
        return;

    size_t start_inpage = totalResult.start_ % totalResult.TOP_K_NUM;

    for (size_t topstart = start_inpage; topstart < totalResult.topKDocs_.size(); ++topstart)
    {
        if ( topstart >= start_inpage + totalResult.count_ )
        {
            break;
        }
        workerid_t curWorkerId = totalResult.topKWorkerIds_[topstart];
        std::pair<std::map<workerid_t, KeywordSearchResult>::iterator, bool> inserted_ret = resultMap.insert(std::make_pair(curWorkerId, KeywordSearchResult()));
        KeywordSearchResult& workerResult = inserted_ret.first->second;
        if (inserted_ret.second)
        {
            workerResult.propertyQueryTermList_ = totalResult.propertyQueryTermList_;
            workerResult.rawQueryString_ = totalResult.rawQueryString_;
            //workerResult.pruneQueryString_ = totalResult.pruneQueryString_;
            workerResult.encodingType_ = totalResult.encodingType_;
            workerResult.collectionName_ = totalResult.collectionName_;
            //workerResult.analyzedQuery_ = totalResult.analyzedQuery_;
            //workerResult.queryTermIdList_ = totalResult.queryTermIdList_;
            //workerResult.totalCount_ = totalResult.totalCount_;
            workerResult.TOP_K_NUM = totalResult.TOP_K_NUM;
            workerResult.distSearchInfo_.isDistributed_ = totalResult.distSearchInfo_.isDistributed_;
            workerResult.docsInPage_.reserve(totalResult.topKDocs_.size()/2);
        }
        workerResult.docsInPage_.push_back(totalResult.topKDocs_[topstart]);
        workerResult.pageOffsetList_.push_back(i);
        ++i;
        ++workerResult.count_;
        //workerResult.topKDocs_.push_back(totalResult.topKDocs_[topstart]);
        //workerResult.topKWorkerIds_.push_back(curWorkerId);
    }
}

bool SearchMerger::splitGetDocsActionItemByWorkerid(
    const GetDocumentsByIdsActionItem& actionItem,
    std::map<workerid_t, GetDocumentsByIdsActionItem>& actionItemMap)
{
    if (actionItem.idList_.empty())
        return false;

    std::vector<sf1r::docid_t> idList;
    std::vector<sf1r::workerid_t> workeridList;
    actionItem.getDocWorkerIdLists(idList, workeridList);

    // split
    for (size_t i = 0; i < workeridList.size(); i++)
    {
        workerid_t& curWorkerid = workeridList[i];
        GetDocumentsByIdsActionItem& subActionItem = actionItemMap[curWorkerid];

        if (subActionItem.idList_.empty())
        {
            subActionItem = actionItem;
            subActionItem.idList_.clear();
        }

        subActionItem.idList_.push_back(actionItem.idList_[i]);
    }

    return true;
}

void SearchMerger::getDistDocNum(const net::aggregator::WorkerResults<uint32_t>& workerResults, uint32_t& mergeResult)
{
    mergeResult = 0;
    size_t workerNum = workerResults.size();
    for (size_t w = 0; w < workerNum; w++)
    {
        mergeResult += workerResults.result(w);
    }
}

void SearchMerger::getDistKeyCount(const net::aggregator::WorkerResults<uint32_t>& workerResults, uint32_t& mergeResult)
{
    mergeResult = 0;
    size_t workerNum = workerResults.size();
    for (size_t w = 0; w < workerNum; w++)
    {
        mergeResult += workerResults.result(w);
    }
}

void SearchMerger::GetSummarizationByRawKey(const net::aggregator::WorkerResults<Summarization>& workerResults, Summarization& mergeResult)
{
    mergeResult.clear();
    size_t workerNum = workerResults.size();
    for (size_t w = 0; w < workerNum; w++)
    {
        // Assume and DOCID should be unique in global space
        mergeResult = workerResults.result(w);
        if (!mergeResult.isEmpty())
        {
            return;
        }
    }
}

void SearchMerger::HookDistributeRequestForSearch(const net::aggregator::WorkerResults<bool>& workerResults, bool& mergeResult)
{
    mergeResult = true;
    size_t workerNum = workerResults.size();
    for (size_t i = 0; i < workerNum; ++i)
    {
        if (!workerResults.result(i))
        {
            mergeResult = false;
            return;
        }
    }
}

} // namespace sf1r
