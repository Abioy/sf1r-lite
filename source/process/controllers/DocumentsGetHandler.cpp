/**
 * @file process/controllers/DocumentsGetHandler.cpp
 * @author Ian Yang
 * @date Created <2010-06-02 16:15:39>
 */
#include <bundles/index/IndexSearchService.h>
#include <bundles/mining/MiningSearchService.h>

#include "DocumentsGetHandler.h"

#include <renderers/DocumentsRenderer.h>
#include <parsers/SearchParser.h>
#include <parsers/SelectParser.h>
#include <common/ValueConverter.h>
#include <common/BundleSchemaHelpers.h>
#include <common/parsers/PageInfoParser.h>
#include <common/parsers/ConditionArrayParser.h>
#include <common/Keys.h>
#include <common/ResultType.h>
#include <common/Utilities.h>

#include <configuration-manager/Acl.h>
#include <util/swap.h>

namespace sf1r
{

static std::string GetPidByIsbn(const std::string& isbn)
{
    //static const int MD5_DIGEST_LENGTH = 32;
    std::string url = "http://www.b5m.com/spuid/isbn/"+isbn;
    return Utilities::generateMD5(url);
}

static std::string GetPidByUrl(const std::string& url)
{
    return Utilities::generateMD5(url);
}


using driver::Keys;

const std::size_t DocumentsGetHandler::kMaxSimilarDocumentCount = 200;

DocumentsGetHandler::DocumentsGetHandler(
        ::izenelib::driver::Request& request,
        ::izenelib::driver::Response& response,
        const CollectionHandler& collectionHandler)
    : request_(request)
    , response_(response)
    , indexSearchService_(collectionHandler.indexSearchService_)
    , miningSearchService_(collectionHandler.miningSearchService_)
    , indexSchema_(collectionHandler.indexSchema_)
    , miningSchema_(collectionHandler.miningSchema_)
    , zambeziConfig_(collectionHandler.zambeziConfig_)
    , actionItem_()
{
    actionItem_.env_.encodingType_ = "UTF-8";
    actionItem_.env_.ipAddress_ = request.header()[Keys::remote_ip].getString();
    actionItem_.collectionName_ = asString(request[Keys::collection]);
}

void DocumentsGetHandler::get()
{
    if (parseSelect() && parseSearchSession() && getIdListFromConditions())
    {
        if(!doGet()) return;
        response_[Keys::total_count] = response_[Keys::resources].size();

        aclFilter();
    }
}

bool DocumentsGetHandler::parseSelect()
{
    using std::swap;

    SelectParser selectParser(indexSchema_, zambeziConfig_, SearchingMode::NotUseSearchingMode); //  TODO update
    if (selectParser.parse(request_[Keys::select]))
    {
        response_.addWarning(selectParser.warningMessage());
        swap(
            actionItem_.displayPropertyList_,
            selectParser.mutableProperties()
        );

        return true;
    }
    else
    {
        response_.addError(selectParser.errorMessage());
        return false;
    }
}

bool DocumentsGetHandler::parseSearchSession()
{
    if (!nullValue(request_[Keys::search_session]))
    {
        SearchParser searchParser(indexSchema_, zambeziConfig_);
        if (searchParser.parse(request_[Keys::search_session]))
        {
            response_.addWarning(searchParser.warningMessage());

            swap(
                actionItem_.env_.queryString_,
                searchParser.mutableKeywords()
            );
            actionItem_.languageAnalyzerInfo_ = searchParser.analyzerInfo();
        }
        else
        {
            response_.addError(searchParser.errorMessage());
            return false;
        }
    }

    return true;
}

bool DocumentsGetHandler::parseDocumentId(const Value& inputId)
{
    if (nullValue(inputId))
    {
        response_.addError("Require ID");
        return false;
    }

    Value::StringType scdDocumentId;

    if (inputId.type() == Value::kObjectType)
    {
        if (inputId.size() != 1)
        {
            response_.addError("Accept exactly one ID");
            return false;
        }

        if (inputId.hasKey(Keys::_id))
        {
            internalId_ = asUint(inputId[Keys::_id]);
            return true;
        }
        else if (inputId.hasKey(Keys::DOCID))
        {
            scdDocumentId = asString(inputId[Keys::DOCID]);
        }
    }
    else
    {
        // assume it is a DOCID
        scdDocumentId = asString(inputId);
    }

    bool success = indexSearchService_->getInternalDocumentId(
                       actionItem_.collectionName_, Utilities::md5ToUint128(scdDocumentId), internalId_
                   );

    if (!success)
    {
        response_.addError("Cannot convert DOCID to internal ID");
    }
    return success;
}

bool DocumentsGetHandler::parsePageInfo()
{
    PageInfoParser pageInfoParser;
    if (!pageInfoParser.parse(request_.get()))
    {
        response_.addError(pageInfoParser.errorMessage());
        return false;
    }
    response_.addWarning(pageInfoParser.warningMessage());

    limit_ = pageInfoParser.limit();
    offset_ = pageInfoParser.offset();

    return true;
}

bool DocumentsGetHandler::getIdListFromConditions()
{
    const Value& conditions = request_[Keys::conditions];
    ConditionArrayParser conditionsParser;
    if (!conditionsParser.parse(conditions))
    {
        response_.addError(conditionsParser.errorMessage());
        return false;
    }
    response_.addWarning(conditionsParser.warningMessage());

    // TODO: filtering is not supported
    static const std::string kCodnitionErrorMessage =
        "Require exactly one condition with operator = or in on property " +
        Keys::_id + " or " + Keys::DOCID + " or Property Name(available and filterable)" + ".";

    if (conditionsParser.parsedConditionCount() != 1)
    {
        response_.addError(kCodnitionErrorMessage);
        return false;
    }

    const ConditionParser& theOnlyCondition =
        conditionsParser.parsedConditions(0);
    if (theOnlyCondition.op() != "in" &&
            theOnlyCondition.op() != "=")
    {
        response_.addError(kCodnitionErrorMessage);
        return false;
    }

    actionItem_.propertyName_.clear();
    if (theOnlyCondition.property() == Keys::_id)
    {
        for (std::size_t i = 0;
                i < theOnlyCondition.size(); ++i)
        {
            actionItem_.idList_.push_back(asUint(theOnlyCondition(i)));
        }
    }
    else if (theOnlyCondition.property() == Keys::DOCID)
    {
        std::vector<std::string> ids;
        for (std::size_t i = 0;
                i < theOnlyCondition.size(); ++i)
        {
            cout<<"DOCID"<<asString(theOnlyCondition(i))<<"type"<<theOnlyCondition.id_type()<<endl;
            if(theOnlyCondition.id_type()=="isbn")
            {
                std::string originid=GetPidByIsbn(asString(theOnlyCondition(i)));
                cout<<"originid"<<originid<<endl;
                actionItem_.docIdList_.push_back(originid);
            }
            else  if(theOnlyCondition.id_type()=="url")
            {
                std::string originid=GetPidByUrl(asString(theOnlyCondition(i)));
                actionItem_.docIdList_.push_back(originid);
            }
            else
                actionItem_.docIdList_.push_back(asString(theOnlyCondition(i)));
        }
    }
    else
    {

        actionItem_.propertyName_ = theOnlyCondition.property();
        if (!isPropertyFilterable(indexSchema_, actionItem_.propertyName_))
        {
            response_.addError(kCodnitionErrorMessage);
            return false;
        }

        sf1r::PropertyDataType dataType = getPropertyDataType(indexSchema_, actionItem_.propertyName_);

        for (std::size_t i = 0;
                i < theOnlyCondition.size(); ++i)
        {
            PropertyValue propertyValue;
            if (theOnlyCondition.property() == Keys::uuid)
            {
                if(theOnlyCondition.id_type()=="isbn")
                {
                    std::string originid=GetPidByIsbn(asString(theOnlyCondition(i)));
                    ValueConverter::driverValue2PropertyValue(dataType, originid, propertyValue);
                }
                else  if(theOnlyCondition.id_type()=="url")
                {
                    std::string originid=GetPidByUrl(asString(theOnlyCondition(i)));
                    ValueConverter::driverValue2PropertyValue(dataType, originid, propertyValue);
                }
                else
                    ValueConverter::driverValue2PropertyValue(dataType, theOnlyCondition(i), propertyValue);
                
            }
            else
            {
                ValueConverter::driverValue2PropertyValue(dataType, theOnlyCondition(i), propertyValue);
            }
            actionItem_.propertyValueList_.push_back(propertyValue);
        }
    }

    return true;
}

bool DocumentsGetHandler::doGet()
{
    // TODO: cache

    RawTextResultFromMIA result;

    if (!indexSearchService_->getDocumentsByIds(actionItem_, result))
    {
        response_.addError("failed to get document");
        return false;
    }

    if (!result.error_.empty())
    {
        response_.addError(result.error_);
        return false;
    }

    DocumentsRenderer renderer(miningSchema_);
    renderer.renderDocuments(
            actionItem_.displayPropertyList_,
            result,
            response_[Keys::resources]);

    return true;
}

/**
 * @brief Filter documents using ACL
 *
 * The ACL fields are always returned in response (see SelectParser). All
 * forbidden documents are removed from the result list.
 *
 * Because dummy token "@@ALL@@" has been removed in DocumentsRenderer, so we do
 * not need to add it in user token list. Acl::check can be used to check ACL
 * directly.
 */
void DocumentsGetHandler::aclFilter()
{
    // Check ACL on documents, remove forbidden documents.
    Value::ArrayType* resources =
        response_[Keys::resources].getPtr<Value::ArrayType>();

    if (resources == 0 || resources->size() == 0)
    {
        // no documents in result
        return;
    }

    if ((*resources)[0].hasKey("ACL_ALLOW")
            || (*resources)[0].hasKey("ACL_DENY"))
    {
        // document level ACL is enabled
        Value::ArrayType filteredResult;
        filteredResult.reserve(resources->size());

        Acl::token_set_type tokens;
        Acl::insertTokensTo(request_.aclTokens(), tokens);

        typedef Value::ArrayType::iterator iterator;
        for (iterator it = resources->begin(); it != resources->end(); ++it)
        {
            Acl acl;
            acl.allow(asString((*it)["ACL_ALLOW"]));
            acl.deny(asString((*it)["ACL_DENY"]));

            if (acl.check(tokens))
            {
                izenelib::util::swapBack(filteredResult, *it);
            }
        }

        resources->swap(filteredResult);
        response_[Keys::total_count] = response_[Keys::resources].size();
    }
}

} // namespace sf1r
