#ifndef SF1R_PARSERS_FILTERING_PARSER_H
#define SF1R_PARSERS_FILTERING_PARSER_H
/**
 * @file core/common/parsers/FilteringParser.h
 * @author Ian Yang
 * @date Created <2010-06-11 17:22:42>
 */

#include "FilteringParserHelper.h"
#include <util/driver/Parser.h>
#include <util/driver/Value.h>

#include <bundles/index/IndexBundleConfiguration.h>
#include <bundles/mining/MiningBundleConfiguration.h>
#include <query-manager/QueryTypeDef.h>

#include <common/parsers/ConditionsTree.h>
#include <string>
#include <stack>

namespace sf1r {

using namespace izenelib::driver;
class FilteringParser : public ::izenelib::driver::Parser
{
public:
    explicit FilteringParser(
        const IndexBundleSchema& indexSchema,
        const MiningSchema& miningSchema)
    : indexSchema_(indexSchema)
    , miningSchema_(miningSchema)
    , filterConditionTree_(new ConditionsNode())
    {}

    bool parse(const Value& conditions);
    
    bool parse_tree(const Value& conditions);

    boost::shared_ptr<ConditionsNode>&
    mutableFilteringTreeRules()
    {
        return filterConditionTree_;
    }

    // static QueryFiltering::FilteringOperation toFilteringOperation(
    //     const std::string& op
    // );
    
private:
    const IndexBundleSchema& indexSchema_;
    const MiningSchema& miningSchema_;

    boost::shared_ptr<ConditionsNode> filterConditionTree_;
};

} // namespace sf1r

#endif // SF1R_PARSERS_FILTERING_PARSER_H
