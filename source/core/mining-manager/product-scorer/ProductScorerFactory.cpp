#include "ProductScorerFactory.h"
#include "ProductScoreParam.h"
#include "ProductScoreSum.h"
#include "CustomScorer.h"
#include "CategoryScorer.h"
#include "CategoryClassifyScorer.h"
#include "TitleRelevanceScorer.h"
#include "../MiningManager.h"
#include "../custom-rank-manager/CustomRankManager.h"
#include "../product-score-manager/ProductScoreManager.h"
#include "../category-classify/CategoryClassifyTable.h"
#include "../suffix-match-manager/SuffixMatchManager.hpp"
#include "../title-scorer/TitleScoreList.h"
#include <common/PropSharedLockSet.h>
#include <common/QueryNormalizer.h>
#include <configuration-manager/ProductRankingConfig.h>
#include <la-manager/KNlpWrapper.h>
#include <knlp/doc_naive_bayes.h>
#include <glog/logging.h>
#include <memory> // auto_ptr
#include <sstream>

using namespace sf1r;

namespace
{
/**
 * in order to make the category score less than 10 (the minimum custom
 * score), we would select at most 9 top labels.
 */
const score_t kTopLabelLimit = 9;
}

ProductScorerFactory::ProductScorerFactory(
    const ProductRankingConfig& config,
    MiningManager& miningManager)
    : config_(config)
    , customRankManager_(miningManager.GetCustomRankManager())
    , categoryValueTable_(NULL)
    , productScoreManager_(miningManager.GetProductScoreManager())
    , categoryClassifyTable_(miningManager.GetCategoryClassifyTable())
    , titleScoreList_(miningManager.GetTitleScoreList())
{
    const ProductScoreConfig& categoryScoreConfig =
        config.scores[CATEGORY_SCORE];
    const std::string& categoryProp = categoryScoreConfig.propName;
    categoryValueTable_ = miningManager.GetPropValueTable(categoryProp);

    if (categoryValueTable_)
    {
        GroupLabelLogger* clickLogger =
            miningManager.GetGroupLabelLogger(categoryProp);
        const GroupLabelKnowledge* labelKnowledge =
            miningManager.GetGroupLabelKnowledge();

        labelSelector_.reset(new BoostLabelSelector(miningManager,
                                                    *categoryValueTable_,
                                                    clickLogger,
                                                    labelKnowledge));
    }

}

ProductScorer* ProductScorerFactory::createScorer(
    const ProductScoreParam& scoreParam)
{
    std::auto_ptr<ProductScoreSum> scoreSum(new ProductScoreSum);

    if (scoreParam.searchMode_ == SearchingMode::SUFFIX_MATCH)
    {
        createFuzzyModeScorer_(*scoreSum, scoreParam);
    }
    else
    {
        for (int i = 0; i < PRODUCT_SCORE_NUM; ++i)
        {
            ProductScorer* scorer = createScorerImpl_(config_.scores[i],
                                                      scoreParam);
            if (scorer)
            {
                scoreSum->addScorer(scorer);
            }
        }
    }

    if(scoreSum->empty())
        return NULL;

    return scoreSum.release();
}

void ProductScorerFactory::createFuzzyModeScorer_(
    ProductScoreSum& scoreSum,
    const ProductScoreParam& scoreParam)
{
    ProductScorer* scorer = categoryClassifyTable_ ?
        createCategoryClassifyScorer_(config_.scores[CATEGORY_CLASSIFY_SCORE], scoreParam) :
        createCategoryScorer_(config_.scores[CATEGORY_SCORE], scoreParam);
    if (scorer)
    {
        scoreSum.addScorer(scorer);
    }

    scorer = createCustomScorer_(config_.scores[CUSTOM_SCORE],
                                 scoreParam.query_);
    if (scorer)
    {
        scoreSum.addScorer(scorer);
    }

    scorer = createTitleRelevanceScorer_(config_.scores[TITLE_RELEVANCE_SCORE],
                                         scoreParam.queryScore_);
    if (scorer)
    {
        scoreSum.addScorer(scorer);
    }

    scorer = createPopularityScorer_(config_.scores[POPULARITY_SCORE]);
    if (scorer)
    {
        scoreSum.addScorer(scorer);
    }
}

ProductScorer* ProductScorerFactory::createScorerImpl_(
    const ProductScoreConfig& scoreConfig,
    const ProductScoreParam& scoreParam)
{
    if (scoreConfig.weight == 0)
        return NULL;

    switch(scoreConfig.type)
    {
    case CUSTOM_SCORE:
        return createCustomScorer_(scoreConfig, scoreParam.query_);

    case CATEGORY_SCORE:
        return createCategoryScorer_(scoreConfig, scoreParam);

    case RELEVANCE_SCORE:
        return createRelevanceScorer_(scoreConfig, scoreParam.relevanceScorer_);

    case POPULARITY_SCORE:
        return createPopularityScorer_(scoreConfig);

    default:
        return NULL;
    }
}

ProductScorer* ProductScorerFactory::createCustomScorer_(
    const ProductScoreConfig& scoreConfig,
    const std::string& query)
{
    if (customRankManager_ == NULL)
        return NULL;

    CustomRankDocId customDocId;
    bool result = customRankManager_->getCustomValue(query, customDocId);

    if (result && !customDocId.topIds.empty())
        return new CustomScorer(scoreConfig, customDocId.topIds);

    return NULL;
}

ProductScorer* ProductScorerFactory::createCategoryScorer_(
    const ProductScoreConfig& scoreConfig,
    const ProductScoreParam& scoreParam)
{
    if (!labelSelector_ ||
        QueryNormalizer::get()->isLongQuery(scoreParam.query_))
        return NULL;

    scoreParam.propSharedLockSet_.insertSharedLock(categoryValueTable_);

    std::vector<category_id_t> boostLabels;
    if (!labelSelector_->selectLabel(scoreParam, kTopLabelLimit, boostLabels))
        return NULL;

    return new CategoryScorer(scoreConfig,
                              *categoryValueTable_,
                              boostLabels);
}

ProductScorer* ProductScorerFactory::createTitleRelevanceScorer_(
    const ProductScoreConfig& scoreConfig,
    double score)
{
    if (titleScoreList_ == NULL)
        return NULL;

    return new TitleRelevanceScorer(scoreConfig, titleScoreList_, score);
}

ProductScorer* ProductScorerFactory::createCategoryClassifyScorer_(
    const ProductScoreConfig& scoreConfig,
    const ProductScoreParam& scoreParam)
{
    if (!categoryClassifyTable_)
        return NULL;

    KNlpWrapper* knlpWrapper = KNlpWrapper::get();
    const std::string& query = scoreParam.rawQuery_;

    LOG(INFO) << "for query [" << query << "]";

    const bool isLongQuery = QueryNormalizer::get()->isLongQuery(query);
    CategoryClassifyScorer::CategoryScoreMap categoryScoreMap =
        knlpWrapper->classifyToMultiCategories(query, isLongQuery);

    if (categoryScoreMap.empty())
    {
        LOG(INFO) << "no classified category";
        return NULL;
    }

    std::ostringstream oss;
    oss << "classified category:";

    for (CategoryClassifyScorer::CategoryScoreMap::const_iterator it =
             categoryScoreMap.begin(); it != categoryScoreMap.end(); ++it)
    {
        oss << " " << it->first << "/" << std::setprecision(4) << it->second;
    }
    LOG(INFO) << oss.str();

    scoreParam.propSharedLockSet_.insertSharedLock(categoryClassifyTable_);
    return new CategoryClassifyScorer(scoreConfig,
                                      *categoryClassifyTable_,
                                      categoryScoreMap);
}

ProductScorer* ProductScorerFactory::createRelevanceScorer_(
    const ProductScoreConfig& scoreConfig,
    ProductScorer* relevanceScorer)
{
    if (!relevanceScorer)
        return NULL;

    relevanceScorer->setWeight(scoreConfig.weight);
    return relevanceScorer;
}

ProductScorer* ProductScorerFactory::createPopularityScorer_(
    const ProductScoreConfig& scoreConfig)
{
    if (!productScoreManager_)
        return NULL;

    return productScoreManager_->createProductScorer(scoreConfig);
}
