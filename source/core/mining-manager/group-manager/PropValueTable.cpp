#include "PropValueTable.h"
#include <mining-manager/util/fcontainer_febird.h>
#include <mining-manager/MiningException.hpp>

#include <iostream>
#include <fstream>
#include <cassert>
#include <set>
#include <algorithm> // reverse

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include <glog/logging.h>

namespace
{
const char* SUFFIX_PROP_STR = ".prop_str.bin";
const char* SUFFIX_PARENT_ID = ".parent_id.bin";
const char* SUFFIX_INDEX_ID = ".index_id.bin";
const char* SUFFIX_VALUE_ID = ".value_id.bin";
const char* SUFFIX_PARENT_STR = ".parent_str.txt";

const izenelib::util::UString::EncodingType ENCODING_TYPE =
    izenelib::util::UString::UTF_8;

inline unsigned int getDistance(
    const izenelib::util::UString& s1,
    const izenelib::util::UString& s2)
{
    izenelib::util::UString ls1(s1);
    izenelib::util::UString ls2(s2);
    ls1.toLowerString();
    ls2.toLowerString();
    const unsigned int HEIGHT = ls1.length() + 1;
    const unsigned int WIDTH = ls2.length() + 1;
    unsigned int eArray[HEIGHT][WIDTH];
    unsigned int i;
    unsigned int j;

    for (i = 0; i < HEIGHT; i++)
        eArray[i][0] = i;

    for (j = 0; j < WIDTH; j++)
        eArray[0][j] = j;

    for (i = 1; i < HEIGHT; i++)
    {
        for (j = 1; j < WIDTH; j++)
        {
            eArray[i][j] = min(
                eArray[i - 1][j - 1] +
                (ls1[i-1] == ls2[j-1] ? 0 : 1),
                min(eArray[i - 1][j] + 1, eArray[i][j - 1] + 1));
        }
    }

    return eArray[HEIGHT - 1][WIDTH - 1];
}

}

NS_FACETED_BEGIN

// as id 0 is reserved for empty value,
// members are initialized to size 1
PropValueTable::PropValueTable(const std::string& dirPath, const std::string& propName)
    : dirPath_(dirPath)
    , propName_(propName)
    , propStrVec_(1)
    , savePropStrNum_(0)
    , parentIdVec_(1)
    , saveParentIdNum_(0)
    , childMapTable_(1)
    , saveIndexNum_(0)
    , saveValueNum_(0)
{
}

PropValueTable::PropValueTable(const PropValueTable& table)
    : dirPath_(table.dirPath_)
    , propName_(table.propName_)
    , propStrVec_(table.propStrVec_)
    , savePropStrNum_(table.savePropStrNum_)
    , parentIdVec_(table.parentIdVec_)
    , saveParentIdNum_(table.saveParentIdNum_)
    , childMapTable_(table.childMapTable_)
    , valueIdTable_(table.valueIdTable_)
    , saveIndexNum_(table.saveIndexNum_)
    , saveValueNum_(table.saveValueNum_)
{
}

void PropValueTable::reserveDocIdNum(std::size_t num)
{
    ScopedWriteLock lock(mutex_);

    valueIdTable_.indexTable_.reserve(num);
}

void PropValueTable::appendPropIdList(const std::vector<pvid_t>& inputIdList)
{
    ScopedWriteLock lock(mutex_);

    valueIdTable_.appendIdList(inputIdList);
}

void PropValueTable::propValueStr(
    pvid_t pvId,
    izenelib::util::UString& ustr,
    bool isLock) const
{
    ScopedReadBoolLock lock(mutex_, isLock);

    ustr = propStrVec_[pvId];
}

PropValueTable::pvid_t PropValueTable::insertPropValueId(const std::vector<izenelib::util::UString>& path)
{
    ScopedWriteLock lock(mutex_);

    pvid_t pvId = 0;

    for (std::vector<izenelib::util::UString>::const_iterator pathIt = path.begin();
        pathIt != path.end(); ++pathIt)
    {
        PropStrMap& propStrMap = childMapTable_[pvId];
        const izenelib::util::UString& pathNode = *pathIt;

        PropStrMap::const_iterator findIt = propStrMap.find(pathNode);
        if (findIt != propStrMap.end())
        {
            pvId = findIt->second;
        }
        else
        {
            pvid_t parentId = pvId;
            pvId = propStrVec_.size();

            if (pvId != 0)
            {
                propStrMap.insert(PropStrMap::value_type(pathNode, pvId));
                propStrVec_.push_back(pathNode);
                parentIdVec_.push_back(parentId);
                childMapTable_.push_back(PropStrMap());
            }
            else
            {
                // overflow
                throw MiningException(propName_ + 
                    ": property value count is out of range",
                    boost::lexical_cast<std::string>(propStrVec_.size()),
                    "PropValueTable::insertPropValueId"
                );
            }
        }
    }

    return pvId;
}

PropValueTable::pvid_t PropValueTable::propValueId(
    const std::vector<izenelib::util::UString>& path,
    bool isLock) const
{
    ScopedReadBoolLock lock(mutex_, isLock);

    pvid_t pvId = 0;

    for (std::vector<izenelib::util::UString>::const_iterator pathIt = path.begin();
        pathIt != path.end(); ++pathIt)
    {
        const PropStrMap& propStrMap = childMapTable_[pvId];
        PropStrMap::const_iterator it = propStrMap.find(*pathIt);
        if (it != propStrMap.end())
        {
            pvId = it->second;
        }
        else
        {
            return 0;
        }
    }

    return pvId;
}

bool PropValueTable::open()
{
    ScopedWriteLock lock(mutex_);

    if (!load_container_febird(dirPath_, propName_ + SUFFIX_PROP_STR, propStrVec_, savePropStrNum_) ||
        !load_container_febird(dirPath_, propName_ + SUFFIX_PARENT_ID, parentIdVec_, saveParentIdNum_) ||
        !load_container_febird(dirPath_, propName_ + SUFFIX_INDEX_ID, valueIdTable_.indexTable_, saveIndexNum_) ||
        !load_container_febird(dirPath_, propName_ + SUFFIX_VALUE_ID, valueIdTable_.multiValueTable_, saveValueNum_))
    {
        return false;
    }

    const unsigned int valueNum = propStrVec_.size();
    if (valueNum != parentIdVec_.size())
    {
        LOG(ERROR) << "unequal property value number in propStrVec_ and parentIdVec_ ";
        return false;
    }

    LOG(INFO) << "loading " << valueNum << " prop values into map";
    childMapTable_.clear();
    childMapTable_.resize(valueNum);
    for (unsigned int i = 1; i < valueNum; ++i)
    {
        pvid_t parentId = parentIdVec_[i];
        const izenelib::util::UString& valueStr = propStrVec_[i];
        childMapTable_[parentId][valueStr] = i;
    }

    return true;
}

bool PropValueTable::flush()
{
    ScopedReadLock lock(mutex_);

    return saveParentId_(dirPath_, propName_ + SUFFIX_PARENT_STR) &&
           save_container_febird(dirPath_, propName_ + SUFFIX_PROP_STR, propStrVec_, savePropStrNum_) &&
           save_container_febird(dirPath_, propName_ + SUFFIX_PARENT_ID, parentIdVec_, saveParentIdNum_) &&
           save_container_febird(dirPath_, propName_ + SUFFIX_INDEX_ID, valueIdTable_.indexTable_, saveIndexNum_) &&
           save_container_febird(dirPath_, propName_ + SUFFIX_VALUE_ID, valueIdTable_.multiValueTable_, saveValueNum_);
}

void PropValueTable::clear()
{
    ScopedWriteLock lock(mutex_);

    propStrVec_.resize(1);
    savePropStrNum_ = 0;

    parentIdVec_.resize(1);
    saveParentIdNum_ = 0;

    childMapTable_.clear();
    childMapTable_.resize(1);

    valueIdTable_.clear();
    saveIndexNum_ = 0;
    saveValueNum_ = 0;
}

bool PropValueTable::saveParentId_(const std::string& dirPath, const std::string& fileName) const
{
    const unsigned int valueNum = propStrVec_.size();
    if (valueNum != parentIdVec_.size())
    {
        LOG(ERROR) << "unequal property value number in propStrVec_ and parentIdVec_";
        return false;
    }

    if (savePropStrNum_ >= valueNum)
        return true;

    boost::filesystem::path filePath(dirPath);
    filePath /= fileName;
    std::string pathStr = filePath.string();

    LOG(INFO) << "saving file: " << fileName
              << ", element num: " << valueNum;

    std::ofstream ofs(pathStr.c_str());
    if (! ofs)
    {
        LOG(ERROR) << "failed opening file " << pathStr;
        return false;
    }

    std::string convertBuffer;
    for (unsigned int valueId = 1; valueId < valueNum; ++valueId)
    {
        propStrVec_[valueId].convertString(convertBuffer, ENCODING_TYPE);

        // columns: id, str, parentId
        ofs << valueId << "\t" << convertBuffer << "\t"
            << parentIdVec_[valueId] << std::endl;
    }

    return true;
}

bool PropValueTable::testDoc(docid_t docId, pvid_t labelId) const
{
    std::set<pvid_t> parentSet;
    parentIdSet(docId, parentSet);

    return parentSet.find(labelId) != parentSet.end();
}

void PropValueTable::propValuePath(
    pvid_t pvId,
    std::vector<izenelib::util::UString>& path,
    bool isLock) const
{
    ScopedReadBoolLock lock(mutex_, isLock);

    // from leaf to root
    for (; pvId; pvId = parentIdVec_[pvId])
    {
        path.push_back(propStrVec_[pvId]);
    }

    // from root to leaf
    std::reverse(path.begin(), path.end());
}

PropValueTable::pvid_t PropValueTable::getFirstValueId(docid_t docId) const
{
    PropIdList propIdList;
    getPropIdList(docId, propIdList);

    if (propIdList.empty())
        return 0;

    return propIdList[0];
}

void PropValueTable::getParentIds(pvid_t pvId, std::vector<pvid_t>& parentIds) const
{
    parentIds.clear();

    for (; pvId; pvId = parentIdVec_[pvId])
    {
        parentIds.push_back(pvId);
    }
}

PropValueTable::pvid_t PropValueTable::getRootValueId(pvid_t pvId) const
{
    pvid_t rootId = 0;

    for (; pvId; pvId = parentIdVec_[pvId])
    {
        rootId = pvId;
    }

    return rootId;
}

NS_FACETED_END
