#include "ProductBundleActivator.h"
#include "ProductSearchService.h"
#include "ProductTaskService.h"
#include "ProductIndexHooker.h"
#include "ProductScdReceiver.h"
#include <bundles/index/IndexTaskService.h>
#include <bundles/index/IndexSearchService.h>

#include <common/SFLogger.h>
#include <document-manager/DocumentManager.h>
#include <aggregator-manager/SearchWorker.h>
#include <aggregator-manager/IndexWorker.h>
#include <index-manager/InvertedIndexManager.h>
#include <product-manager/product_manager.h>
#include <product-manager/collection_product_data_source.h>
#include <product-manager/scd_operation_processor.h>
#include <product-manager/product_price_trend.h>
#include <product-manager/product_cron_job_handler.h>
#include <util/singleton.h>

#include <boost/filesystem.hpp>

#include <memory> // for auto_ptr

namespace bfs = boost::filesystem;
using namespace izenelib::util;

namespace sf1r
{

using namespace izenelib::osgi;
ProductBundleActivator::ProductBundleActivator()
    : searchTracker_(0)
    , taskTracker_(0)
    , context_(0)
    , searchService_(0)
    , searchServiceReg_(0)
    , taskService_(0)
    , taskServiceReg_(0)
    , refIndexTaskService_(0)
    , config_(0)
    , data_source_(0)
    , price_trend_(0)
    , scd_receiver_(0)
{
}

ProductBundleActivator::~ProductBundleActivator()
{
    if (data_source_)
    {
        delete data_source_;
    }
    if (price_trend_)
    {
        delete price_trend_;
    }
    if (scd_receiver_)
    {
        delete scd_receiver_;
    }
}

void ProductBundleActivator::start( IBundleContext::ConstPtr context )
{
    context_ = context;

    boost::shared_ptr<BundleConfiguration> bundleConfigPtr = context->getBundleConfig();
    config_ = static_cast<ProductBundleConfiguration*>(bundleConfigPtr.get());

    searchTracker_ = new ServiceTracker( context, "IndexSearchService", this );
    searchTracker_->startTracking();
    taskTracker_ = new ServiceTracker( context, "IndexTaskService", this );
    taskTracker_->startTracking();
}

void ProductBundleActivator::stop( IBundleContext::ConstPtr context )
{
    if(searchTracker_)
    {
        searchTracker_->stopTracking();
        delete searchTracker_;
        searchTracker_ = 0;
    }
    if(taskTracker_)
    {
        taskTracker_->stopTracking();
        delete taskTracker_;
        taskTracker_ = 0;
    }

    if(searchServiceReg_)
    {
        searchServiceReg_->unregister();
        delete searchServiceReg_;
        delete searchService_;
        searchServiceReg_ = 0;
        searchService_ = 0;
    }
    if(taskServiceReg_)
    {
        taskServiceReg_->unregister();
        delete taskServiceReg_;
        delete taskService_;
        taskServiceReg_ = 0;
        taskService_ = 0;
    }
}

bool ProductBundleActivator::addingService( const ServiceReference& ref )
{
    if ( ref.getServiceName() == "IndexSearchService" )
    {
        Properties props = ref.getServiceProperties();
        if ( props.get( "collection" ) == config_->collectionName_)
        {
            IndexSearchService* service = reinterpret_cast<IndexSearchService*> ( const_cast<IService*>(ref.getService()) );
            std::cout << "[ProductBundleActivator#addingService] Calling IndexSearchService..." << std::endl;

            if(config_->mode_=="m" || config_->mode_=="o")//in m
            {
                productManager_ = createProductManager_(service);

                searchService_ = new ProductSearchService(config_);
                searchService_->productManager_ = productManager_;

                if(refIndexTaskService_ )
                {
                    addIndexHook_(refIndexTaskService_);
                }
            }

            taskService_ = new ProductTaskService(config_);
            searchServiceReg_ = context_->registerService( "ProductSearchService", searchService_, props );
            taskServiceReg_ = context_->registerService( "ProductTaskService", taskService_, props );
            return true;
        }
        else
        {
            return false;
        }
    }
    else if( ref.getServiceName() == "IndexTaskService" )
    {
        Properties props = ref.getServiceProperties();
        if ( props.get( "collection" ) == config_->collectionName_)
        {
            refIndexTaskService_ = reinterpret_cast<IndexTaskService*> ( const_cast<IService*>(ref.getService()) );
            if(config_->mode_=="m" || config_->mode_=="o")//in m
            {
                if(productManager_)
                {
                    addIndexHook_(refIndexTaskService_);
                }
                if (config_->mode_ == "o")
                {
                    std::string offer_syncid = config_->productId_ + "_offer_comment";
                    LOG(INFO)<<"Scd Reciever init with offer syncid : "<< offer_syncid << std::endl;
                    scd_receiver_ = new ProductScdReceiver(offer_syncid, config_->collectionName_, config_->callback_);
                    scd_receiver_->Set(refIndexTaskService_);
                }
            }
            else if(config_->mode_=="a")//in a
            {
                LOG(INFO)<<"Scd Reciever init with id : "<<config_->productId_<<std::endl;
                scd_receiver_ = new ProductScdReceiver(config_->productId_, config_->collectionName_, config_->callback_);
                scd_receiver_->Set(refIndexTaskService_);
            }

        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    return true;
}

boost::shared_ptr<ProductManager>
ProductBundleActivator::createProductManager_(IndexSearchService* indexService)
{
    std::cout<<"ProductBundleActivator::createProductManager_"<<std::endl;
    openDataDirectories_();
    std::string dir = getCurrentCollectionDataPath_()+"/product";
    std::cout<<"product dir : "<<dir<<std::endl;
    boost::filesystem::create_directories(dir);
    std::string scd_dir =  config_->collPath_.getScdPath() +"/product_scd";
    boost::filesystem::create_directories(scd_dir);
    data_source_ = new CollectionProductDataSource(indexService->searchWorker_->documentManager_,
                                                   indexService->searchWorker_->invertedIndexManager_,
                                                   indexService->searchWorker_->idManager_,
                                                   indexService->searchWorker_->searchManager_,
                                                   config_->pm_config_,
                                                   config_->indexSchema_);
    LOG(INFO)<<"Scd Processor init with id : "<<config_->productId_<<std::endl;
    if (config_->pm_config_.enable_price_trend)
    {
        price_trend_ = new ProductPriceTrend(config_->cassandraConfig_,
                                             dir,
                                             config_->pm_config_.group_property_names,
                                             config_->pm_config_.time_interval_days);
        ProductCronJobHandler* handler = ProductCronJobHandler::getInstance();
        if (!handler->cronStart(config_->cron_))
        {
            std::cerr << "Init price trend cron task failed." << std::endl;
        }
        handler->addCollection(price_trend_);
    }
    std::string work_dir = dir+"/work_dir";
    boost::shared_ptr<ProductManager> product_manager(new ProductManager(work_dir,
                                                                         indexService->searchWorker_->documentManager_,
                                                                         data_source_,
                                                                         price_trend_,
                                                                         config_->pm_config_));
    return product_manager;
}

void ProductBundleActivator::addIndexHook_(IndexTaskService* indexService) const
{
    indexService->indexWorker_->hooker_.reset(new ProductIndexHooker(productManager_));
}

void ProductBundleActivator::removedService( const ServiceReference& ref )
{
}

bool ProductBundleActivator::openDataDirectories_()
{
    std::vector<std::string>& directories = config_->collectionDataDirectories_;
    if( directories.size() == 0 )
    {
        LOG(ERROR)<<"no data dir config";
        return false;
    }
    directoryRotator_.setCapacity(directories.size());
    typedef std::vector<std::string>::const_iterator iterator;
    for (iterator it = directories.begin(); it != directories.end(); ++it)
    {
        bfs::path dataDir = bfs::path( getCollectionDataPath_() ) / *it;
        if (!directoryRotator_.appendDirectory(dataDir))
        {
            std::string msg = dataDir.string() + " corrupted, delete it!";
            LOG(ERROR) << msg;
            //clean the corrupt dir
            boost::filesystem::remove_all( dataDir );
            directoryRotator_.appendDirectory(dataDir);
        }
    }

    directoryRotator_.rotateToNewest();
    boost::shared_ptr<Directory> newest = directoryRotator_.currentDirectory();
    if (newest)
    {
        bfs::path p = newest->path();
        currentCollectionDataName_ = p.filename().string();
        //std::cout << "Current Index Directory: " << indexPath_() << std::endl;
        return true;
    }
    return false;
}

std::string ProductBundleActivator::getCurrentCollectionDataPath_() const
{
    return config_->collPath_.getCollectionDataPath()+"/"+currentCollectionDataName_;
}

std::string ProductBundleActivator::getCollectionDataPath_() const
{
    return config_->collPath_.getCollectionDataPath();
}

std::string ProductBundleActivator::getQueryDataPath_() const
{
    return config_->collPath_.getQueryDataPath();
}

}
