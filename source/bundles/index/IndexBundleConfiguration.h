#ifndef INDEX_BUNDLE_CONFIGURATION_H
#define INDEX_BUNDLE_CONFIGURATION_H

#include <configuration-manager/PropertyConfig.h>
#include <configuration-manager/RankingManagerConfig.h>
#include <configuration-manager/CollectionPath.h>
#include <configuration-manager/ZambeziConfig.h>
#include <node-manager/Sf1rTopology.h>
#include <ir/index_manager/utility/IndexManagerConfig.h>
#include <util/osgi/BundleConfiguration.h>
#include <util/ustring/UString.h>

#include <la/analyzer/MultiLanguageAnalyzer.h>

namespace sf1r
{

class IndexBundleConfiguration : public ::izenelib::osgi::BundleConfiguration
{
public:
    IndexBundleConfiguration(const std::string& collectionName);

    void setSchema(const DocumentSchema& documentSchema);

    void setZambeziSchema(const DocumentSchema& documentSchema);

    void numberProperty();

    const bool isUnigramWildcard() { return wildcardType_ == "unigram"; }

    const bool isTrieWildcard() {return wildcardType_ == "trie"; }

    const bool hasUnigramProperty() { return bIndexUnigramProperty_; }

    const bool isUnigramSearchMode() { return bUnigramSearchMode_; }

    bool isMasterAggregator() const { return isMasterAggregator_; }

    bool isWorkerNode() const { return isWorkerNode_; }

    void setIndexMultiLangGranularity(const std::string& granularity);

    bool getPropertyConfig(const std::string& name, PropertyConfig& config) const;

    bool getAnalysisInfo(
        const std::string& propertyName,
        AnalysisInfo& analysisInfo,
        std::string& analysis,
        std::string& language
    ) const;

    std::set<PropertyConfig, PropertyComp>::const_iterator 
    findIndexProperty(PropertyConfig tempPropertyConfig, bool& isIndexSchema) const;

    std::string getSearchAnalyzer() const
    {
        return searchAnalyzer_;
    }

    std::string indexSCDPath() const
    {
        return collPath_.getScdPath() + "index/";
    }

    std::string masterIndexSCDPath() const
    {
    	return collPath_.getScdPath() + "master_index/";
    }

    std::string rebuildIndexSCDPath() const
    {
        return collPath_.getScdPath() + "rebuild_scd/";
    }

    std::string logSCDPath() const
    {
        return collPath_.getScdPath() + "log/";
    }

private:

    bool eraseProperty(const std::string& name)
    {
        PropertyConfig config;
        config.propertyName_ = name;
        return indexSchema_.erase(config);
    }

public:
    std::string collectionName_;

    // if there is any index;
    bool isSchemaEnable_;

    // <IndexBundle><NormalSchema>
    bool isNormalSchemaEnable_;

    // <IndexBundle><ZambeziSchema>
    bool isZambeziSchemaEnable_;

    CollectionPath collPath_;

    // config for NO.1 Inverted Index (Normal Index)
    IndexBundleSchema indexSchema_;

    // config for No.2 Inverted Index (Zambezi Index)
    ZambeziConfig zambeziConfig_;

    DocumentSchema documentSchema_;

    std::vector<std::string> indexShardKeys_;
    // shard node info for index/search service  
    MasterCollection  col_shard_info_;

    /// @brief whether log new created doc to LogServer
    bool logCreatedDoc_;

    /// @brief local host information
    std::string localHostUsername_;
    std::string localHostIp_;

    /// @brief whether add unigram properties
    bool bIndexUnigramProperty_;

    /// @brief whether search based on unigram index terms
    bool bUnigramSearchMode_;

    /// @brief the granularity of multi language support during indexing
    la::MultilangGranularity indexMultilangGranularity_;

    std::string languageIdentifierDbPath_;

    std::string productSourceField_;

    /// Parameters
    /// @brief config for IndexManager
    izenelib::ir::indexmanager::IndexManagerConfig indexConfig_;

    /// @brief cron indexing expression
    std::string cronIndexer_;

    /// @brief whether perform rebuild collection automatically
    bool isAutoRebuild_;

    /// @brief whether trigger Question Answering mode
    bool bTriggerQA_;

    /// @brief whether the parallel searching
    bool enable_parallel_searching_;

    /// @brief force get document even if it has been deleted
    bool enable_forceget_doc_;

    /// @brief document cache number
    size_t documentCacheNum_;

    /// @brief searchmanager cache number
    size_t searchCacheNum_;

    /// @brief whether refresh search cache periodically
    bool refreshSearchCache_;

    /// @brief refresh interval of search cache
    time_t refreshCacheInterval_;

    /// @brief filter cache number
    size_t filterCacheNum_;

    /// @brief master search cache number
    size_t masterSearchCacheNum_;

    /// @brief top results number
    size_t topKNum_;

    /// @brief top results number for KNN search
    size_t kNNTopKNum_;

    /// @brief Hamming Distance threshold for KNN search
    size_t kNNDist_;

    /// @brief sort cache update interval
    size_t sortCacheUpdateInterval_;

    /// @brief Whether current SF1R is a Master node and Aggregator is available for current collection.
    ///        (configured as distributed search)
    bool isMasterAggregator_;

    /// @brief whether current collection is a worker node
    bool isWorkerNode_;

    /// @brief The encoding type of the Collection
    izenelib::util::UString::EncodingType encoding_;

    /// @brief how wildcard queries are processed, 'unigram' or 'trie'
    std::string wildcardType_;

    std::vector<std::string> collectionDataDirectories_;

    /// @brief Configurations for RankingManager
    RankingManagerConfig rankingManagerConfig_;

    std::string searchAnalyzer_;
};
}

#endif
