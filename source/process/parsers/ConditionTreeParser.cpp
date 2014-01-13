
#include "ConditionTreeParser.h"
#include "FilteringParserHelper.h"
#include <boost/algorithm/string/case_conv.hpp>

#include <vector>
#include <common/Keys.h>
#include <iostream>
using namespace std;
namespace sf1r {
/**
 * @class ConditionTreeParser
 *
 * The @b conditions field here is a tree-like construction. It also can be a array, see @b ConditionArrayParser. It specifies filtering conditions on
 * properties. The returned result must satisfied all conditions.
 *
 * Every item is an object of following fields:
 * - @b property* (@c String): Which property current condition applies on.
 * - @b operator* (@c String): Which kind operator.
 *   - @c = : Property must equal to the specified value.
 *   - @c <> : Property should not equal to the specified value.
 *   - @c < : Property must be less than the specified value.
 *   - @c <= : Property must be less than or equal to the specified value.
 *   - @c > : Property must be larger than the specified value.
 *   - @c >= : Property must be larger than or equal to the specified value.
 *   - @c between : Property must be in the range inclusively.
 *   - @c in : Property must be equal to any of the list values.
 *   - @c not_in : Property should not be equal to any of the list values.
 *   - @c starts_with : Property must start with the specified prefix.
 *   - @c ends_with : Property must end with the specified suffix.
 *   - @c contains : Property must contains the specified sub-string.
 * - @b value (@c Any): If this is an array, all items are used as the
 *   parameters of the operator. If this is Null, then the operator takes 0
 *   parameters. Otherwise this field is used as the only 1 parameter for the
 *   operator.
 * - @b relation* (@c String): Which boolean of the conditions node in this level.
 * - @b condition_array : the array of all these condition filter nodes in the same level of this tree-like construction, 
 *   All these conditons combined using @b relation.
 *
 * The property must be filterable. Check field indices in schema/get (see
 * SchemaController::get() ). All index names (the @b name field in every
 * index) which @b filter is @c true can be used here.
 * 
 * Tree-like Filter Example:
 * 
 * @code
 *  "conditions":
 *  {
 *    "condition_array":
 *     [
 *       {
 *         "condition_array":
 *          [
 *            {"property":"Price", "operator":">", "value":[100]},
 *            {"property":"Price", "operator":"<", "value":[60]}
 *          ],
 *          "relation":"OR"
 *       },
 *       {...},
 *       {"property":"Price", "operator":"<", "value":[110]}
 *     ],
 *     "relation":"AND"
 *   }
 * @endcode
 */
    using driver::Keys;

    bool ConditionTreeParser::parseTree(const Value& conditions, ConditionsNode& node)
    {
        Value conditionArray;
        std::string relation;
        conditionArray = conditions[Keys::condition_array];
        relation = asString(conditions[Keys::relation]);

        node.setRelation(relation);
        const Value::ArrayType* cond_array = conditionArray.getPtr<Value::ArrayType>();
        if (!cond_array)
        {
            error() = "Conditions must be an array";
            return false;
        }
        
        for (unsigned int i = 0; i < cond_array->size(); ++i)
        {
            std::string property_ = asString(((*cond_array)[i])[Keys::property]); // check if is leaf-node
            if (property_.empty())
            {
                ConditionsNode conditionNode;
                if ( !parseTree((*cond_array)[i], conditionNode))
                    return false;
                node.conditionsNodeList_.push_back(conditionNode);
            }
            else
            {
                ConditionParser conditionParser;
                conditionParser.parse((*cond_array)[i]);
                QueryFiltering::FilteringType filterLeafNode;


                if (!do_paser(conditionParser, indexSchema_, filterLeafNode))
                {
                    error() = "Property's data type is unknown: " +
                                        conditionParser.property();
                    return false;
                }
                
                node.conditionLeafList_.push_back(filterLeafNode);
            }
        }

        return true;
    }
    
    bool ConditionTreeParser::parse(const Value& conditions) //{}
    {
        clearMessages();

        if (conditions.type() == Value::kNullType) // for the only {} situation;
        {
            return true; 
        }

        if (conditions.type() != Value::kObjectType)
        {
            //error() = "Condition must be an object.";
            return false;
        }

        return parseTree(conditions, conditionsTree_);
    }
}
