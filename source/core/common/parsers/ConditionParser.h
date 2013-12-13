#ifndef SF1R_DRIVER_PARSERS_CONDITION_PARSER_H
#define SF1R_DRIVER_PARSERS_CONDITION_PARSER_H
/**
 * @file core/common/parsers/ConditionParser.h
 * @author Ian Yang
 * @date Created <2010-06-10 19:33:04>
 */
#include <util/driver/Value.h>
#include <util/driver/Parser.h>
#include <iostream>

namespace sf1r {

using namespace izenelib::driver;
class ConditionParser: public ::izenelib::driver::Parser
{
public:
    ConditionParser();

    std::size_t size() const
    {
        return array_.size();
    }

    const Value& operator()(std::size_t index) const;

    const std::string& property() const
    {
        return property_;
    }

    const std::string& id_type() const
    {
        return id_type_;
    }

    const std::string& op() const
    {
        return op_;
    }

    const Value::ArrayType& array() const
    {
        return array_;
    }

    bool parse(const Value& condition);

    void swap(ConditionParser& other)
    {
        using std::swap;
        swap(array_, other.array_);
        swap(property_, other.property_);
        swap(op_, other.op_);
        swap(error(), other.error());
    }
        void print()
    {
        std::cout<<"property: "<<property_ << " op: " << op_ << std::endl;;
    }

private:
    Value::ArrayType array_;
    std::string property_;
    std::string op_;
    std::string id_type_;

};

inline void swap(ConditionParser& a, ConditionParser& b)
{
    a.swap(b);
}

}

#endif // SF1R_DRIVER_PARSERS_CONDITION_PARSER_H
