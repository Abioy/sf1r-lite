#include "BoostLabelSelector.h"
#include "ProductScoreParam.h"
#include "../MiningManager.h"
#include "../group-manager/PropValueTable.h"
#include "../group-label-logger/GroupLabelLogger.h"
#include "../group-label-logger/GroupLabelKnowledge.h"
#include "../util/split_ustr.h"
#include "../util/convert_ustr.h"
#include <b5m-manager/product_matcher.h>
#include <la-manager/AttrTokenizeWrapper.h>
#include <util/ustring/UString.h>

using namespace sf1r;

namespace
{
const izenelib::util::UString::EncodingType kEncodingType =
    izenelib::util::UString::UTF_8;
}

BoostLabelSelector::BoostLabelSelector(
    MiningManager& miningManager,
    const faceted::PropValueTable& propValueTable,
    GroupLabelLogger* clickLogger,
    const GroupLabelKnowledge* labelKnowledge)
    : miningManager_(miningManager)
    , propValueTable_(propValueTable)
    , clickLogger_(clickLogger)
    , labelKnowledge_(labelKnowledge)
{
}

bool BoostLabelSelector::selectLabel(
    const ProductScoreParam& scoreParam,
    std::size_t limit,
    std::vector<category_id_t>& boostLabels)
{
    if (scoreParam.searchMode_ == SearchingMode::ZAMBEZI)
        return convertZambeziLabelIds_(scoreParam, limit, boostLabels);

    if (convertLabelIds_(scoreParam.groupParam_.boostGroupLabels_, boostLabels) ||
        getFreqLabel_(scoreParam.query_, limit, boostLabels) ||
        classifyQueryToLabel_(scoreParam.query_, boostLabels) ||
        getKnowledgeLabel_(scoreParam.querySource_, boostLabels))
    {
        if (boostLabels.size() > limit)
        {
            boostLabels.resize(limit);
        }
    }

    return !boostLabels.empty();
}

bool BoostLabelSelector::convertZambeziLabelIds_(    
    const ProductScoreParam& scoreParam,
    std::size_t limit,
    std::vector<category_id_t>& boostLabels)
{
    const std::string& query = scoreParam.rawQuery_;
    std::vector<char*>** cateinfo = AttrTokenizeWrapper::get()->get_TermCategory(query);

    if (cateinfo)
        for(uint32_t i = 0; i < (*cateinfo)->size(); ++i)
        {
            std::cout << std::string((*cateinfo)->at(i)) << std::endl;

            izenelib::util::UString UCategory(std::string((*cateinfo)->at(i)),izenelib::util::UString::UTF_8);

            std::vector<std::vector<izenelib::util::UString> > groupPaths;
            split_group_path(UCategory, groupPaths);
            if (groupPaths.empty())
                continue;

            faceted::PropValueTable::pvid_t topLabel =
                propValueTable_.propValueId(groupPaths[0], false);

            boostLabels.push_back(topLabel);
        }

    if (boostLabels.size() > limit)
    {
        boostLabels.resize(limit);
    }

    return !boostLabels.empty();
}

bool BoostLabelSelector::convertLabelIds_(
    const faceted::GroupParam::GroupPathVec& groupPathVec,
    std::vector<category_id_t>& boostLabels)
{
    for (faceted::GroupParam::GroupPathVec::const_iterator pathIt =
             groupPathVec.begin(); pathIt != groupPathVec.end(); ++pathIt)
    {
        std::vector<izenelib::util::UString> ustrPath;
        convert_to_ustr_vector(*pathIt, ustrPath);

        faceted::PropValueTable::pvid_t labelId =
            propValueTable_.propValueId(ustrPath, false);

        if (labelId == 0)
            continue;

        boostLabels.push_back(labelId);
    }

    return !boostLabels.empty();
}

bool BoostLabelSelector::getFreqLabel_(
    const std::string& query,
    std::size_t limit,
    std::vector<category_id_t>& boostLabels)
{
    if (clickLogger_)
    {
        std::vector<int> topFreqs;
        clickLogger_->getFreqLabel(query, limit, boostLabels, topFreqs);
    }

    return !boostLabels.empty();
}

bool BoostLabelSelector::classifyQueryToLabel_(
    const std::string& query,
    std::vector<category_id_t>& boostLabels)
{
    const izenelib::util::UString ustrQuery(query, kEncodingType);
    izenelib::util::UString backendCategory;
    if (!miningManager_.GetProductCategory(ustrQuery, backendCategory))
        return false;

    izenelib::util::UString frontendCategory;
    ProductMatcher* matcher = ProductMatcherInstance::get();

    if (!matcher->GetFrontendCategory(backendCategory, frontendCategory))
        return false;

    std::vector<std::vector<izenelib::util::UString> > groupPaths;
    split_group_path(frontendCategory, groupPaths);
    if (groupPaths.empty())
        return false;

    faceted::PropValueTable::pvid_t topLabel =
        propValueTable_.propValueId(groupPaths[0], false);
    if (topLabel == 0)
        return false;

    boostLabels.push_back(topLabel);
    return true;
}

bool BoostLabelSelector::getKnowledgeLabel_(
    const std::string& querySource,
    std::vector<category_id_t>& boostLabels)
{
    if (labelKnowledge_ && !querySource.empty())
    {
        labelKnowledge_->getKnowledgeLabel(querySource, boostLabels);
    }

    return !boostLabels.empty();
}
