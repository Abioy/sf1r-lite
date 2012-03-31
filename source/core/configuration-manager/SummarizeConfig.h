#ifndef SF1R_SUMMARIZATION_CONFIG_H_
#define SF1R_SUMMARIZATION_CONFIG_H_

#include <string>

#include <boost/serialization/access.hpp>

namespace sf1r
{

/**
 * @brief The type of attr property configuration.
 */
class SummarizeConfig
{
public:
    /// property name
    std::string docidPropName;
    std::string uuidPropName;
    std::string contentPropName;

    friend class boost::serialization::access;

    template <typename Archive>
    void serialize( Archive & ar, const unsigned int version )
    {
        ar & docidPropName;
        ar & uuidPropName;
        ar & contentPropName;
    }
};

} // namespace

#endif //_ATTR_CONFIG_H_
