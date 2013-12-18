/**
 * @file ItemCondition.h
 * @author Jun Jiang
 * @date 2011-06-01
 */

#ifndef ITEM_CONDITION_H
#define ITEM_CONDITION_H

#include "RecTypes.h"
#include <query-manager/QueryTypeDef.h> // FilteringType
#include <ir/index_manager/utility/BitVector.h>
#include <boost/shared_ptr.hpp>
#include <common/parsers/ConditionsTree.h>

namespace sf1r
{
class ItemManager;
class QueryBuilder;

struct ItemCondition
{
    ItemManager* itemManager_;

    /**filtering conditions */
    ConditionsNode filterTree_;


    /** bit 1 for meet condition, bit 0 for not meet condition */
    boost::scoped_ptr<izenelib::ir::indexmanager::BitVector> pBitVector_;

    ItemCondition();

    void createBitVector(QueryBuilder* queryBuilder);

    /**
     * Check whether @p itemId meets the condition.
     * @param item the item to check
     * @return true for the item meets condition, false for not meet condition.
     */
    bool isMeetCondition(itemid_t itemId) const;
};

} // namespace sf1r

#endif // ITEM_CONDITION_H
