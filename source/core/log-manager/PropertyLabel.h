#ifndef _PROPERTY_LABEL_H_
#define _PROPERTY_LABEL_H_

#include "DbRecordBase.h"

namespace sf1r {

class PropertyLabel : public DbRecordBase {

public:

    enum Column { Collection, LabelName, HitDocsNum, EoC };

    static const char* ColumnName[EoC];

    static const char* ColumnMeta[EoC];

    static const char* TableName;

    DEFINE_DB_RECORD_COMMON_ROUTINES(PropertyLabel)

    PropertyLabel() : DbRecordBase(),
        collectionPresent_(false),
        labelNamePresent_(false),
        hitDocsNumPresent_(false){}

    ~PropertyLabel(){}

    inline const std::string & getCollection()
    {
        return collection_;
    }

    inline void setCollection( const std::string & collection ) {
        collection_ = collection;
        collectionPresent_ = true;
    }

    inline bool hasCollection() {
        return collectionPresent_;
    }

    inline const std::string & getLabelName()
    {
        return labelName_;
    }

    inline void setLabelName( const std::string & labelName ) {
        labelName_ = labelName;
        labelNamePresent_ = true;
    }

    inline bool hasLabelName() {
        return labelNamePresent_;
    }

    inline const size_t getHitDocsNum()
    {
        return hitDocsNum_;
    }

    inline void setHitDocsNum( const size_t hitDocsNum ) {
        hitDocsNum_ = hitDocsNum;
        hitDocsNumPresent_ = true;
    }

    inline bool hasHitDocsNum() {
        return hitDocsNumPresent_;
    }

    void save( std::map<std::string, std::string> & rawdata );

    void load( const std::map<std::string, std::string> & rawdata );

private:

    std::string collection_;
    bool collectionPresent_;

    std::string labelName_;
    bool labelNamePresent_;

    size_t hitDocsNum_;
    bool hitDocsNumPresent_;
};

}
#endif
