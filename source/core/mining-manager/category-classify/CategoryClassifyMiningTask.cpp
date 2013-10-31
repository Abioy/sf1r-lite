#include "CategoryClassifyMiningTask.h"
#include <document-manager/DocumentManager.h>
#include <common/ResourceManager.h>
#include <common/NumericPropertyTableBase.h>
#include <knlp/doc_naive_bayes.h>
#include <glog/logging.h>
#include <boost/filesystem/path.hpp>
#include <sstream>

using namespace sf1r;
namespace bfs = boost::filesystem;

namespace
{
const std::string kOriginalCategoryPropName("Category");
const std::string kSourcePropName("Source");
const std::string kClassifyCategoryValueBook("文娱>书籍杂志");

void getDocPropValue(
    const Document& doc,
    const std::string& propName,
    std::string& propValue)
{
    doc.getProperty(propName, propValue);
}

}

CategoryClassifyMiningTask::CategoryClassifyMiningTask(
    DocumentManager& documentManager,
    CategoryClassifyTable& classifyTable,
    const std::string& targetCategoryPropName,
    const boost::shared_ptr<const NumericPropertyTableBase>& priceTable)
    : documentManager_(documentManager)
    , classifyTableReader_(classifyTable)
    , targetCategoryPropName_(targetCategoryPropName)
    , priceTable_(priceTable)
    , startDocId_(0)
{
}

bool CategoryClassifyMiningTask::buildDocument(docid_t docID, const Document& doc)
{
    std::string title;
    getDocPropValue(doc, classifyTableWriter_.propName(), title);

    if (title.empty())
        return true;

    std::string classifyCategory;
    bool isRule = true;

    if (ruleByOriginalCategory_(doc, classifyCategory) ||
        ruleBySource_(doc, classifyCategory) ||
        classifyByTitle_(title, docID, classifyCategory, isRule))
    {
        classifyTableWriter_.setCategory(docID, classifyCategory, isRule);
    }

    return true;
}

bool CategoryClassifyMiningTask::ruleByOriginalCategory_(
    const Document& doc,
    std::string& classifyCategory)
{
    std::string originalCategory;
    getDocPropValue(doc, kOriginalCategoryPropName, originalCategory);

    if (!originalCategory.empty())
    {
        boost::shared_ptr<KNlpWrapper> knlpWrapper = KNlpResourceManager::getResource();
        classifyCategory = knlpWrapper->mapFromOriginalCategory(originalCategory);
    }

    return !classifyCategory.empty();
}

bool CategoryClassifyMiningTask::ruleBySource_(
    const Document& doc,
    std::string& classifyCategory)
{
    if (targetCategoryPropName_.empty())
        return false;

    std::string targetCategory;
    getDocPropValue(doc, targetCategoryPropName_, targetCategory);

    if (!targetCategory.empty())
        return false;

    std::string source;
    getDocPropValue(doc, kSourcePropName, source);

    if (source == "文轩网官网")
    {
        classifyCategory = kClassifyCategoryValueBook;
        return true;
    }

    return false;
}

bool CategoryClassifyMiningTask::classifyByTitle_(
    const std::string& title,
    docid_t docID,
    std::string& classifyCategory,
    bool& isRule)
{
    try
    {
        std::ostringstream titlePrice;
        titlePrice << title;

        std::pair<double, double> pricePair;
        if (priceTable_->getDoublePairValue(docID, pricePair))
        {
            titlePrice << "[[" << pricePair.second << "]]";
        }

        boost::shared_ptr<KNlpWrapper> knlpWrapper = KNlpResourceManager::getResource();
        KNlpWrapper::category_score_map_t categoryScoreMap =
            knlpWrapper->classifyToMultiCategories(titlePrice.str(), true);

        classifyCategory = knlpWrapper->getBestCategory(categoryScoreMap);
        isRule = false;
        return true;
    }
    catch(std::exception& ex)
    {
        LOG(ERROR) << "exception: " << ex.what()
                   << ", title: " << title;
        return false;
    }
}

bool CategoryClassifyMiningTask::preProcess(int64_t timestamp)
{
    startDocId_ = classifyTableReader_.docIdNum();
    const docid_t endDocId = documentManager_.getMaxDocId();

    LOG(INFO) << "category classify mining task"
              << ", start docid: " << startDocId_
              << ", end docid: " << endDocId;

    if (startDocId_ > endDocId)
        return false;

    classifyTableWriter_ = classifyTableReader_;
    classifyTableWriter_.resize(endDocId + 1);

    return true;
}

bool CategoryClassifyMiningTask::postProcess()
{
    if (!classifyTableWriter_.flush())
    {
        LOG(ERROR) << "failed in CategoryClassifyTable::flush()";
        return false;
    }

    classifyTableWriter_.swap(classifyTableReader_);

    // release the memory of classifyTableWriter_
    CategoryClassifyTable().swap(classifyTableWriter_);

    return true;
}
