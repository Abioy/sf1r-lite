#include "MasterManagerBase.h"
#include "SuperNodeManager.h"
#include "NodeManagerBase.h"
#include "ZooKeeperNamespace.h"
#include "DistributeTest.hpp"

#include <boost/lexical_cast.hpp>

using namespace sf1r;

// note lock:
// you should never sync call the interface which may hold a lock in the NodeManagerBase .
//
MasterManagerBase::MasterManagerBase()
: isDistributeEnable_(false)
, masterState_(MASTER_STATE_INIT)
, stopping_(false)
, write_prepared_(false)
, new_write_disabled_(false)
, is_mine_primary_(false)
, is_ready_for_new_write_(false)
, waiting_request_num_(0)
, CLASSNAME("MasterManagerBase")
{
}

void MasterManagerBase::initCfg()
{
    topologyPath_ = ZooKeeperNamespace::getTopologyPath();
    serverParentPath_ = ZooKeeperNamespace::getServerParentPath();
    serverPath_ = ZooKeeperNamespace::getServerPath();

    sf1rTopology_ = NodeManagerBase::get()->getSf1rTopology();

    write_req_queue_ = ZooKeeperNamespace::getWriteReqQueueNode(sf1rTopology_.curNode_.nodeId_);
    write_req_queue_parent_ = ZooKeeperNamespace::getCurrWriteReqQueueParent(sf1rTopology_.curNode_.nodeId_);
    write_req_queue_root_parent_ = ZooKeeperNamespace::getRootWriteReqQueueParent();
    write_prepare_node_ =  ZooKeeperNamespace::getWriteReqPrepareNode(sf1rTopology_.curNode_.nodeId_);
    write_prepare_node_parent_ =  ZooKeeperNamespace::getWriteReqPrepareParent();
}

void MasterManagerBase::updateTopologyCfg(const Sf1rTopology& cfg)
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    LOG(INFO) << "topology changed.";
    LOG(INFO) << cfg.toString();
    bool shard_changed = false;
    if (cfg.all_shard_nodes_ != sf1rTopology_.all_shard_nodes_)
        shard_changed = true;

    sf1rTopology_ = cfg;

    if (!zookeeper_ || !zookeeper_->isConnected())
        return;

    if (masterState_ == MASTER_STATE_STARTING_WAIT_WORKERS ||
        masterState_ == MASTER_STATE_STARTED)
    {
        if (stopping_)
            return;
        if (shard_changed)
            detectWorkers();
    }

    ZNode znode;
    std::string olddata;
    if(zookeeper_->getZNodeData(serverRealPath_, olddata, ZooKeeper::WATCH))
    {
        if (olddata.empty())
            return;
        znode.loadKvString(olddata);
        setServicesData(znode);
        zookeeper_->setZNodeData(serverRealPath_, znode.serialize());
    }
    else
    {
        LOG(WARNING) << "get old server service data error";
    }

    resetAggregatorConfig();
}

bool MasterManagerBase::init()
{
    // initialize zookeeper client
    zookeeper_ = ZooKeeperManager::get()->createClient(this);
    if (!zookeeper_)
        return false;
    stopping_ = false;
    return true;
}

void MasterManagerBase::start()
{
    if (masterState_ == MASTER_STATE_INIT)
    {
        if (!init())
        {
            throw std::runtime_error(std::string("failed to initialize ") + CLASSNAME);
        }

        if (!checkZooKeeperService())
        {
            masterState_ = MASTER_STATE_STARTING_WAIT_ZOOKEEPER;
            LOG (ERROR) << CLASSNAME << " waiting for ZooKeeper Service...";
            return;
        }

        boost::lock_guard<boost::mutex> lock(state_mutex_);
        if (masterState_ != MASTER_STATE_INIT)
        {
            LOG(INFO) << "already starting.";
            return;
        }
        masterState_ = MASTER_STATE_STARTING;
        doStart();
    }
    // call init for all service.
    ServiceMapT::const_iterator cit = all_distributed_services_.begin();
    while(cit != all_distributed_services_.end())
    {
        cit->second->initMaster();
        ++cit;
    }
}

void MasterManagerBase::stop()
{
	{
		boost::lock_guard<boost::mutex> lock(state_mutex_);
		stopping_ = true;
	}
    if (zookeeper_ && zookeeper_->isConnected())
    {
        std::vector<std::string> childrenList;
        zookeeper_->deleteZNode(serverRealPath_);
        zookeeper_->getZNodeChildren(serverParentPath_, childrenList, ZooKeeper::NOT_WATCH, false);
        if (childrenList.size() == 0)
        {
            zookeeper_->deleteZNode(serverParentPath_);
        }
        childrenList.clear();
        zookeeper_->isZNodeExists(write_req_queue_parent_, ZooKeeper::NOT_WATCH);
        zookeeper_->isZNodeExists(write_req_queue_root_parent_, ZooKeeper::NOT_WATCH);
        // disconnect will wait other ZooKeeper event finished,
        // so we can not do it in state_mutex_ lock.
        zookeeper_->disconnect();
    }
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    masterState_ = MASTER_STATE_INIT;
    waiting_request_num_ = 0;
}

shardid_t MasterManagerBase::getMyShardId()
{
    return sf1rTopology_.curNode_.nodeId_;
}

bool MasterManagerBase::getShardReceiver(
        unsigned int shardid,
        std::string& host,
        unsigned int& recvPort)
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);

    WorkerMapT::iterator it = workerMap_.find(shardid);
    if (it != workerMap_.end())
    {
        host = it->second->host_;
        recvPort = it->second->dataPort_;
        return true;
    }
    else
    {
        return false;
    }
}

//bool MasterManagerBase::getCollectionShardids(const std::string& service, const std::string& collection, std::vector<shardid_t>& shardidList)
//{
//    //boost::lock_guard<boost::mutex> lock(state_mutex_);
//    return sf1rTopology_.curNode_.master_.getShardidList(service, collection, shardidList);
//}
//
//bool MasterManagerBase::checkCollectionShardid(const std::string& service, const std::string& collection, unsigned int shardid)
//{
//    //boost::lock_guard<boost::mutex> lock(state_mutex_);
//    return sf1rTopology_.curNode_.master_.checkCollectionWorker(service, collection, shardid);
//}

void MasterManagerBase::registerIndexStatus(const std::string& collection, bool isIndexing)
{
    std::string indexStatus = isIndexing ? "indexing" : "notindexing";

    std::string data;
    if (zookeeper_ && zookeeper_->getZNodeData(serverRealPath_, data))
    {
        ZNode znode;
        znode.loadKvString(data);
        znode.setValue(collection, indexStatus);
    }

    std::string nodePath = getNodePath(sf1rTopology_.curNode_.replicaId_,  sf1rTopology_.curNode_.nodeId_);
    if (zookeeper_ && zookeeper_->getZNodeData(nodePath, data, ZooKeeper::WATCH))
    {
        ZNode znode;
        znode.loadKvString(data);
        znode.setValue(collection, indexStatus);
    }

}

std::string MasterManagerBase::findReCreatedServerPath()
{
    std::string new_created_path;

    std::vector<std::string> childrenList;
    zookeeper_->getZNodeChildren(serverParentPath_, childrenList);
    for (size_t i = 0; i < childrenList.size(); ++i)
    {
        std::string sdata;
        zookeeper_->getZNodeData(childrenList[i], sdata, ZooKeeper::NOT_WATCH);
        ZNode znode;
        znode.loadKvString(sdata);
        if (znode.getStrValue(ZNode::KEY_HOST) == sf1rTopology_.curNode_.host_)
        {
            LOG(INFO) << "found server real path for current : " << childrenList[i];
            new_created_path = childrenList[i];
            zookeeper_->isZNodeExists(new_created_path, ZooKeeper::WATCH);
            break;
        }
    }

    return new_created_path;
}

void MasterManagerBase::process(ZooKeeperEvent& zkEvent)
{
    LOG(INFO) << CLASSNAME << ", "<< state2string(masterState_) <<", "<<zkEvent.toString();

    if (stopping_)
        return;
    if (zkEvent.type_ == ZOO_SESSION_EVENT && zkEvent.state_ == ZOO_CONNECTED_STATE)
    {
        boost::lock_guard<boost::mutex> lock(state_mutex_);
        if (masterState_ == MASTER_STATE_STARTING_WAIT_ZOOKEEPER)
        {
            masterState_ = MASTER_STATE_STARTING;
            doStart();
        }
        else if (masterState_ != MASTER_STATE_INIT && masterState_!= MASTER_STATE_STARTING)
        {
            LOG(INFO) << "auto-reconnect in master." << serverRealPath_;
            if (!zookeeper_->isZNodeExists(serverRealPath_, ZooKeeper::WATCH))
            {
                // because the zookeeper will auto re-create the ephemeral node,
                // we need try to find the re-created node.
                std::string new_server_real = findReCreatedServerPath();
                if (new_server_real.empty())
                {
                    LOG(INFO) << "serverPath_ disconnected, waiting reconnect.";
                    return;
                }
                else
                {
                    serverRealPath_ = new_server_real;
                    LOG(INFO) << "serverRealPath_ reconnected after auto-reconnect : " << serverRealPath_;
                }
                watchAll();
            }
            else
            {
                watchAll();
                //checkForWriteReq();
            }
            updateServiceReadStateWithoutLock("ReadyForRead", true);
        }
    }
    else if (zkEvent.type_ == ZOO_SESSION_EVENT && zkEvent.state_ == ZOO_EXPIRED_SESSION_STATE)
    {
        {
            boost::lock_guard<boost::mutex> lock(state_mutex_);
            LOG(WARNING) << "master node disconnected by zookeeper, state : " << zookeeper_->getStateString();
            LOG(WARNING) << "try reconnect: " << sf1rTopology_.curNode_.toString();
            stopping_ = true;
        }

        // reconnect
        zookeeper_->disconnect();

        if (!checkZooKeeperService())
        {
            boost::lock_guard<boost::mutex> lock(state_mutex_);
            stopping_ = false;
            masterState_ = MASTER_STATE_STARTING_WAIT_ZOOKEEPER;
            LOG (ERROR) << CLASSNAME << " waiting for ZooKeeper Service...";
            return;
        }

        boost::lock_guard<boost::mutex> lock(state_mutex_);
        masterState_ = MASTER_STATE_STARTING;
        doStart();
        LOG (WARNING) << " restarted in MasterManagerBase for ZooKeeper Service finished";
        updateServiceReadStateWithoutLock("ReadyForRead", true);
    }
}

void MasterManagerBase::onNodeCreated(const std::string& path)
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (stopping_)
        return;

    if (path.find(topologyPath_) == std::string::npos)
    {
        LOG(INFO) << "created path not care :" << path;
        return;
    }

    if (masterState_ == MASTER_STATE_STARTING_WAIT_WORKERS)
    {
        // try detect workers
        masterState_ = MASTER_STATE_STARTING;
        detectWorkers();
    }
    else if (masterState_ == MASTER_STATE_STARTED)
    {
        // try recover
        recover(path);
    }
    updateServiceReadStateWithoutLock("ReadyForRead", true);
    //else if (masterState_ == MASTER_STATE_STARTED
    //             && masterState_ != MASTER_STATE_FAILOVERING)
    //{
    //    // try recover
    //    failover(path);
    //}
}

void MasterManagerBase::onNodeDeleted(const std::string& path)
{
    LOG(INFO) << "node deleted: " << path;
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (stopping_)
        return;

    if (masterState_ == MASTER_STATE_STARTED ||
        masterState_ == MASTER_STATE_STARTING_WAIT_WORKERS)
    {
        if (path.find(topologyPath_) != std::string::npos)
        {
            // try failover
            failover(path);
            // reset watch.
            std::string sdata;
            zookeeper_->getZNodeData(path, sdata, ZooKeeper::WATCH);
            updateServiceReadStateWithoutLock("ReadyForRead", true);
        }
    }
    checkForWriteReq();
}

void MasterManagerBase::onChildrenChanged(const std::string& path)
{
    LOG(INFO) << "node children changed : " << path;
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (stopping_)
        return;

    if (masterState_ > MASTER_STATE_STARTING_WAIT_ZOOKEEPER)
    {
        if (path.find(topologyPath_) != std::string::npos)
        {
            // reset watch.
            std::string sdata;
            zookeeper_->getZNodeData(path, sdata, ZooKeeper::WATCH);
            detectReplicaSet(path);
            updateServiceReadStateWithoutLock("ReadyForRead", true);
        }
    }
    checkForWriteReq();
}

void MasterManagerBase::onDataChanged(const std::string& path)
{
    LOG(INFO) << "node data changed : " << path;
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (stopping_)
        return;

    if (masterState_ == MASTER_STATE_STARTING_WAIT_WORKERS)
    {
        if (path.find(topologyPath_) != std::string::npos)
        {
            // try detect workers
            masterState_ = MASTER_STATE_STARTING;
            detectWorkers();
        }
    }
    // reset watch.
    if (path.find(topologyPath_) != std::string::npos)
    {
        zookeeper_->isZNodeExists(path, ZooKeeper::WATCH);
        updateServiceReadStateWithoutLock("ReadyForRead", true);
    }

    checkForWriteReq();
}

bool MasterManagerBase::prepareWriteReq()
{
    if (!isDistributeEnable_)
        return true;
    if (stopping_)
        return false;
    if (!isMinePrimary())
    {
        LOG(WARNING) << "non-primary master can not prepare a write request!";
        zookeeper_->isZNodeExists(write_prepare_node_, ZooKeeper::NOT_WATCH);
        zookeeper_->isZNodeExists(write_req_queue_parent_, ZooKeeper::NOT_WATCH);
        return false;
    }
    if (new_write_disabled_)
    {
        LOG(INFO) << "prepare a write request failed for new write temporal disabled!";
        return false;
    }
    if (NodeManagerBase::isAsyncEnabled())
    {
        write_prepared_ = true;
        return true;
    }
    ZNode znode;
    znode.setValue(ZNode::KEY_MASTER_SERVER_REAL_PATH, serverRealPath_);
    if (!zookeeper_->createZNode(write_prepare_node_, znode.serialize(), ZooKeeper::ZNODE_EPHEMERAL))
    {
        if (zookeeper_->getErrorCode() == ZooKeeper::ZERR_ZNODEEXISTS)
        {
            LOG(INFO) << "There is another write request running, prepareWriteReq failed on server: " << serverRealPath_;
        }
        else
        {
            LOG (ERROR) <<" Failed to prepare write request for (" << zookeeper_->getErrorString()
                << "), please retry. on server : " << serverRealPath_;
        }
        zookeeper_->isZNodeExists(write_prepare_node_, ZooKeeper::WATCH);
        return false;
    }
    LOG(INFO) << "prepareWriteReq success on server : " << serverRealPath_;
    write_prepared_ = true;
    DistributeTestSuit::testFail(PrimaryFail_At_Master_PrepareWrite);
    return true;
}

bool MasterManagerBase::getWriteReqNodeData(ZNode& znode)
{
    std::string sdata;
    if (zookeeper_->getZNodeData(write_prepare_node_, sdata))
    {
        ZNode znode;
        znode.loadKvString(sdata);
    }
    else
    {
        LOG(WARNING) << "get write request data failed on :" << serverRealPath_;
    }
    return false;
}

void MasterManagerBase::checkForWriteReq()
{
    //DistributeTestSuit::loadTestConf();
    if (!isDistributeEnable_)
        return;

    if (!isMinePrimary())
    {
        if (!zookeeper_ || !zookeeper_->isConnected())
            return;
        if (!cached_write_reqlist_.empty())
        {
            LOG(INFO) << "non primary master but has cached write request. clear cache" << serverRealPath_;
            cached_write_reqlist_ = std::queue< std::pair<std::string, std::pair<std::string, std::string> > >();
        }
        LOG(INFO) << "not a primary master while check write request, ignore." << serverRealPath_;
        zookeeper_->isZNodeExists(write_prepare_node_, ZooKeeper::NOT_WATCH);
        zookeeper_->isZNodeExists(write_req_queue_parent_, ZooKeeper::NOT_WATCH);
        return;
    }

    switch(masterState_)
    {
    //case MASTER_STATE_WAIT_WORKER_FINISH_REQ:
    //    checkForWriteReqFinished();
    //    break;
    case MASTER_STATE_STARTED:
    case MASTER_STATE_STARTING_WAIT_WORKERS:
        checkForNewWriteReq();
        break;
    default:
        break;
    }
}

bool MasterManagerBase::cacheNewWriteFromZNode()
{
    if (!cached_write_reqlist_.empty())
        return false;
    std::vector<std::string> reqchild;
    zookeeper_->getZNodeChildren(write_req_queue_parent_, reqchild, ZooKeeper::NOT_WATCH);
    if (reqchild.empty())
    {
        LOG(INFO) << "no write request anymore while check request on server: " << serverRealPath_;
        zookeeper_->getZNodeChildren(write_req_queue_parent_, reqchild, ZooKeeper::WATCH);
        return false;
    }

    waiting_request_num_ = reqchild.size();
    LOG(INFO) << "there are some write request waiting: " << reqchild.size();
    size_t pop_num = reqchild.size() > 1000 ? 1000:reqchild.size();

    for(size_t i = 0; i < pop_num; ++i)
    {
        ZNode znode;
        std::string sdata;
        zookeeper_->getZNodeData(reqchild[i], sdata);
        znode.loadKvString(sdata);
        cached_write_reqlist_.push(std::make_pair(reqchild[i], std::make_pair(znode.getStrValue(ZNode::KEY_REQ_DATA),
                znode.getStrValue(ZNode::KEY_REQ_TYPE))));
        //zookeeper_->deleteZNode(reqchild[i]);
    }
    return true;
}

// check if any new request can be processed.
void MasterManagerBase::checkForNewWriteReq()
{
    if (masterState_ != MASTER_STATE_STARTED && masterState_ != MASTER_STATE_STARTING_WAIT_WORKERS)
    {
        LOG(INFO) << "current master state is not ready while check write, state:" << masterState_;
        return;
    }
    if (write_prepared_)
    {
        LOG(INFO) << "a prepared write is still waiting worker ";
        return;
    }
    //if (!isAllWorkerIdle())
    if (!is_ready_for_new_write_)
    {
        return;
    }
    if (!endWriteReq())
    {
        zookeeper_->isZNodeExists(write_prepare_node_, ZooKeeper::WATCH);
        return;
    }

    if (cached_write_reqlist_.empty())
    {
        if (!cacheNewWriteFromZNode())
            return;
    }

    if (!cached_write_reqlist_.empty())
    {
        LOG(INFO) << "there are some cached write request : " << cached_write_reqlist_.size();
        DistributeTestSuit::testFail(PrimaryFail_At_Master_checkForNewWrite);
        if (on_new_req_available_)
        {
            bool ret = on_new_req_available_();
            if (!ret)
            {
                LOG(ERROR) << "the write request handler return failed.";
                write_prepared_ = false;
                endWriteReq();
                zookeeper_->isZNodeExists(write_req_queue_parent_, ZooKeeper::WATCH);
            }
            else
            {
                LOG(INFO) << "all new write requests have been delivered success.";
            }
        }
        else
        {
            LOG(ERROR) << "the new request handler not set!!";
            return;
        }
    }
}

// make sure prepare success before call this.
bool MasterManagerBase::popWriteReq(std::string& reqdata, std::string& type)
{
    if (!isDistributeEnable_)
        return false;

    if (cached_write_reqlist_.empty())
    {
        if (!cacheNewWriteFromZNode())
            return false;
    }

    reqdata = cached_write_reqlist_.front().second.first;
    type = cached_write_reqlist_.front().second.second;
    LOG(INFO) << "a request poped : " << cached_write_reqlist_.front().first << " on the server: " << serverRealPath_;
    if(!zookeeper_->deleteZNode(cached_write_reqlist_.front().first))
    {
        if (!zookeeper_->isConnected())
            return false;
    }
    cached_write_reqlist_.pop();
    return true;
}

bool MasterManagerBase::isAllShardNodeOK(const std::vector<shardid_t>& shardids)
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (!zookeeper_ || !zookeeper_->isConnected())
        return false;
    for (size_t i = 0; i < shardids.size(); ++i)
    {
        if (shardids[i] == sf1rTopology_.curNode_.nodeId_)
            continue;
        WorkerMapT::const_iterator it = workerMap_.find(shardids[i]);
        if (it == workerMap_.end())
        {
            LOG(INFO) << "shardid not found while check for ok. " << shardids[i];
        }
        if (!it->second->worker_.isGood_)
        {
            LOG(INFO) << "shardid not ready." << shardids[i];
            return false;
        }
    }

    return true;
}

bool MasterManagerBase::pushWriteReqToShard(const std::string& reqdata,
    const std::vector<shardid_t>& shardids)
{
    if (!zookeeper_ || !zookeeper_->isConnected())
    {
        LOG(ERROR) << "Master is not connecting to ZooKeeper, write request pushed failed." <<
            "," << reqdata;
        return false;
    }

    ZNode znode;
    znode.setValue(ZNode::KEY_REQ_TYPE, "api_from_shard");
    znode.setValue(ZNode::KEY_REQ_DATA, reqdata);
    //std::vector<shardid_t> shardids;
    //getCollectionShardids(Sf1rTopology::getServiceName(Sf1rTopology::SearchService), coll, shardids);

    for (size_t i = 0; i < shardids.size(); ++i)
    {
        if (shardids[i] == sf1rTopology_.curNode_.nodeId_)
            continue;
        std::string write_queue = ZooKeeperNamespace::getWriteReqQueueNode(shardids[i]);
        if(zookeeper_->createZNode(write_queue, znode.serialize(), ZooKeeper::ZNODE_SEQUENCE))
        {
            LOG(INFO) << "a write request pushed to the shard queue : "
                << zookeeper_->getLastCreatedNodePath()
                << ", " << write_queue;
        }
        else
        {
            LOG(ERROR) << "write request pushed failed for shard queue" <<
                "," << write_queue;
        }
    }
    return true;
}

bool MasterManagerBase::pushWriteReq(const std::string& reqdata, const std::string& type)
{
    if (!isDistributeEnable_)
    {
        LOG(ERROR) << "Master is not configured as distributed, write request pushed failed." <<
            "," << reqdata;
        return false;
    }
    if (stopping_)
    {
        LOG(ERROR) << "Master is stopping, write request pushed failed." <<
            "," << reqdata;
        return false;
    }
    // boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (!zookeeper_ || !zookeeper_->isConnected())
    {
        LOG(ERROR) << "Master is not connecting to ZooKeeper, write request pushed failed." <<
            "," << reqdata;
        return false;
    }

    if (!isMinePrimary())
    {
        if (NodeManagerBase::isAsyncEnabled())
        {
            usleep(10*1000);
        }
        else
        {
            usleep(500*1000);
        }
    }
    else if (waiting_request_num_ > 10000)
    {
        LOG(INFO) << "too many write request waiting, slow down send. " << waiting_request_num_;
        sleep(1);
    }

    ZNode znode;
    //znode.setValue(ZNode::KEY_REQ_CONTROLLER, controller_name);
    znode.setValue(ZNode::KEY_REQ_TYPE, type);
    znode.setValue(ZNode::KEY_REQ_DATA, reqdata);
    if(zookeeper_->createZNode(write_req_queue_, znode.serialize(), ZooKeeper::ZNODE_SEQUENCE))
    {
        LOG(INFO) << "a write request pushed to the queue : " << zookeeper_->getLastCreatedNodePath();
    }
    else
    {
        LOG(ERROR) << "write request pushed failed." <<
            "," << reqdata;
        return false;
    }
    return true;
}

bool MasterManagerBase::disableNewWrite()
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (write_prepared_)
    {
        LOG(INFO) << "disable write failed for already prepared : ";
        return false;
    }
    new_write_disabled_ = true;
    return true;
}

void MasterManagerBase::enableNewWrite()
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    new_write_disabled_ = false;
}

void MasterManagerBase::endPreparedWrite()
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    write_prepared_ = false;
}

bool MasterManagerBase::endWriteReq()
{
    if (NodeManagerBase::isAsyncEnabled())
    {
        return true;
    }

    if (stopping_)
        return true;

    if (!zookeeper_ || !zookeeper_->isZNodeExists(write_prepare_node_))
    {
        return true;
    }
    std::string sdata;
    if (zookeeper_->getZNodeData(write_prepare_node_, sdata))
    {
        ZNode znode;
        znode.loadKvString(sdata);
        std::string write_server = znode.getStrValue(ZNode::KEY_MASTER_SERVER_REAL_PATH);
        if (write_server != serverRealPath_)
        {
            LOG(WARNING) << "end request mismatch server. " << write_server << " vs " << serverRealPath_;
            return false;
        }
        zookeeper_->deleteZNode(write_prepare_node_);
        LOG(INFO) << "end write request success on server : " << serverRealPath_;
    }
    else
    {
        LOG(WARNING) << "get write request data failed while end request on server :" << serverRealPath_;
        return false;
    }
    return true;
}

bool MasterManagerBase::isAllWorkerIdle()
{
    if (!isAllWorkerInState(NodeManagerBase::NODE_STATE_STARTED))
    {
        LOG(INFO) << "one of primary worker not ready for new write request.";
        return false;
    }
    return true;
}

bool MasterManagerBase::isAllWorkerInState(int state)
{
    WorkerMapT::iterator it;
    for (it = workerMap_.begin(); it != workerMap_.end(); it++)
    {
        std::string nodepath = getNodePath(it->second->replicaId_,  it->first);
        std::string sdata;
        if (zookeeper_->getZNodeData(nodepath, sdata, ZooKeeper::WATCH))
        {
            ZNode nodedata;
            nodedata.loadKvString(sdata);
            if (nodedata.getUInt32Value(ZNode::KEY_NODE_STATE) != (uint32_t)state)
            {
                LOG(INFO) << "worker not ready for state : " << state << ", " << nodepath;
                return false;
            }
        }
    }
    return true;
}

bool MasterManagerBase::isBusy()
{
    if (!isDistributeEnable_)
        return false;
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (stopping_ || !zookeeper_ || !zookeeper_->isConnected())
        return true;
    if (zookeeper_->isZNodeExists(write_prepare_node_))
    {
        LOG(INFO) << "Master is busy because there is another write request running";
        return true;
    }
    return !isAllWorkerIdle();
}

void MasterManagerBase::showWorkers()
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    WorkerMapT::iterator it;
    for (it = workerMap_.begin(); it != workerMap_.end(); it++)
    {
        cout << it->second->toString() ;
    }
}

/// protected ////////////////////////////////////////////////////////////////////

std::string MasterManagerBase::state2string(MasterStateType e)
{
    std::stringstream ss;
    switch (e)
    {
    case MASTER_STATE_INIT:
        return "MASTER_STATE_INIT";
        break;
    case MASTER_STATE_STARTING:
        return "MASTER_STATE_STARTING";
        break;
    case MASTER_STATE_STARTING_WAIT_ZOOKEEPER:
        return "MASTER_STATE_STARTING_WAIT_ZOOKEEPER";
        break;
    case MASTER_STATE_STARTING_WAIT_WORKERS:
        return "MASTER_STATE_STARTING_WAIT_WORKERS";
        break;
    case MASTER_STATE_STARTED:
        return "MASTER_STATE_STARTED";
        break;
    //case MASTER_STATE_FAILOVERING:
    //    return "MASTER_STATE_FAILOVERING";
    //    break;
    //case MASTER_STATE_RECOVERING:
    //    return "MASTER_STATE_RECOVERING";
    //    break;
    }

    return "UNKNOWN";
}

void MasterManagerBase::watchAll()
{
    // for replica change
    std::vector<std::string> childrenList;
    zookeeper_->getZNodeChildren(topologyPath_, childrenList, ZooKeeper::WATCH);
    for (size_t i = 0; i < childrenList.size(); i++)
    {
        std::vector<std::string> chchList;
        zookeeper_->getZNodeChildren(childrenList[i], chchList, ZooKeeper::WATCH);
    }

    // for nodes change
    for(std::set<shardid_t>::const_iterator cit = sf1rTopology_.all_shard_nodes_.begin();
        cit != sf1rTopology_.all_shard_nodes_.end(); ++cit)
    //for (uint32_t nodeid = 1; nodeid <= sf1rTopology_.nodeNum_; nodeid++)
    {
        shardid_t nodeid = *cit;
        std::string nodePath = getNodePath(sf1rTopology_.curNode_.replicaId_, nodeid);
        zookeeper_->isZNodeExists(nodePath, ZooKeeper::WATCH);
    }

    if (isMinePrimary())
    {
        zookeeper_->isZNodeExists(write_prepare_node_, ZooKeeper::WATCH);
        zookeeper_->isZNodeExists(write_req_queue_parent_, ZooKeeper::WATCH);
    }
}

bool MasterManagerBase::checkZooKeeperService()
{
    if (!zookeeper_->isConnected())
    {
        zookeeper_->connect(true);

        if (!zookeeper_->isConnected())
        {
            return false;
        }
    }

    return true;
}

void MasterManagerBase::doStart()
{
    stopping_ = false;
    detectReplicaSet();

    detectWorkers();

    // Each Master serves as a Search Server, register it without waiting for all workers to be ready.
    registerServiceServer();
    LOG(INFO) << "distributed node info : ";
    LOG(INFO) << sf1rTopology_.toString();
}


int MasterManagerBase::detectWorkersInReplica(replicaid_t replicaId, size_t& detected, size_t& good)
{
    bool mine_primary = isMinePrimary();
    if (mine_primary)
        LOG(INFO) << "I am primary master ";

    for(std::set<shardid_t>::const_iterator cit = sf1rTopology_.all_shard_nodes_.begin();
        cit != sf1rTopology_.all_shard_nodes_.end(); ++cit)
    //for (uint32_t nodeid = 1; nodeid <= sf1rTopology_.nodeNum_; nodeid++)
    {
        shardid_t nodeid = *cit;
        std::string data;
        std::string nodePath = getNodePath(replicaId, nodeid);
        if (zookeeper_->getZNodeData(nodePath, data, ZooKeeper::WATCH))
        {
            ZNode znode;
            znode.loadKvString(data);

            // if this sf1r node provides worker server
            if (znode.hasKey(ZNode::KEY_WORKER_PORT))
            {
                if (mine_primary)
                {
                    if(!isPrimaryWorker(replicaId, nodeid))
                    {
                        LOG(INFO) << "primary master need detect primary worker, ignore non-primary worker";
                        LOG (INFO) << "node " << nodeid << ", replica: " << replicaId;
                        continue;
                    }
                }

                if (workerMap_.find(nodeid) != workerMap_.end())
                {
                    if (workerMap_[nodeid]->worker_.isGood_)
                        continue;
                    workerMap_[nodeid]->worker_.isGood_ = true;
                }
                else
                {
                    // insert new worker
                    boost::shared_ptr<Sf1rNode> sf1rNode(new Sf1rNode);
                    sf1rNode->worker_.isGood_ = true;
                    workerMap_[nodeid] = sf1rNode;
                }

                // update worker info
                boost::shared_ptr<Sf1rNode>& workerNode = workerMap_[nodeid];
                workerNode->nodeId_ = nodeid;
                updateWorkerNode(workerNode, znode);
                workerNode->replicaId_ = replicaId;

                detected ++;
                if (workerNode->worker_.isGood_)
                {
                    good ++;
                }
            }
        }
        else
        {
            // reset watcher
            zookeeper_->isZNodeExists(nodePath, ZooKeeper::WATCH);
        }
    }

    if (detected >= sf1rTopology_.all_shard_nodes_.size())
    {
        masterState_ = MASTER_STATE_STARTED;
        LOG (INFO) << CLASSNAME << " detected " << sf1rTopology_.all_shard_nodes_.size()
                   << " all workers (good " << good << ")" << std::endl;
    }
    else
    {
        masterState_ = MASTER_STATE_STARTING_WAIT_WORKERS;
        LOG (INFO) << CLASSNAME << " detected " << detected << " workers (good " << good
                   << "), all " << sf1rTopology_.all_shard_nodes_.size() << std::endl;
    }
    return good;
}

int MasterManagerBase::detectWorkers()
{
    size_t detected = 0;
    size_t good = 0;
    WorkerMapT old_workers = workerMap_;

    workerMap_.clear();
    // detect workers from "current" replica first
    replicaid_t replicaId = sf1rTopology_.curNode_.replicaId_;
    detectWorkersInReplica(replicaId, detected, good);

    for (size_t i = 0; i < replicaIdList_.size(); i++)
    {
        if (masterState_ != MASTER_STATE_STARTING_WAIT_WORKERS)
        {
            LOG(INFO) << "detected worker enough, stop detect other replica.";
            break;
        }
        if (replicaId == replicaIdList_[i])
            continue;
        // not enough, check other replica
        LOG(INFO) << "begin detect workers in other replica : " << replicaIdList_[i];
        detectWorkersInReplica(replicaIdList_[i], detected, good);
    }
    WorkerMapT::iterator old_it = old_workers.begin();
    WorkerMapT::iterator new_it = workerMap_.begin();
    size_t compared_size = 0;
    while(old_it != old_workers.end() && new_it != workerMap_.end())
    {
        if (old_it->first != new_it->first)
            break;
        if (old_it->second->nodeId_ != new_it->second->nodeId_)
            break;
        if (old_it->second->replicaId_ != new_it->second->replicaId_)
            break;
        if (old_it->second->host_ != new_it->second->host_)
            break;
        if (old_it->second->worker_.port_ != new_it->second->worker_.port_)
            break;
        if (old_it->second->worker_.isGood_ != new_it->second->worker_.isGood_)
            break;
        ++old_it;
        ++new_it;
        ++compared_size;
    }

    if ((compared_size != old_workers.size()) ||
        (compared_size != workerMap_.size()))
    {
        //
        // update workers' info to aggregators
        resetAggregatorConfig();
    }
    return good;
}

void MasterManagerBase::updateWorkerNode(boost::shared_ptr<Sf1rNode>& workerNode, ZNode& znode)
{
    workerNode->replicaId_ = sf1rTopology_.curNode_.replicaId_;
    workerNode->host_ = znode.getStrValue(ZNode::KEY_HOST);
    //workerNode->port_ = znode.getUInt32Value(ZNode::KEY_WORKER_PORT);

    try
    {
        workerNode->worker_.port_ =
                boost::lexical_cast<port_t>(znode.getStrValue(ZNode::KEY_WORKER_PORT));
    }
    catch (std::exception& e)
    {
        workerNode->worker_.isGood_ = false;
        LOG (ERROR) << "failed to convert workerPort \"" << znode.getStrValue(ZNode::KEY_WORKER_PORT)
                    << "\" got from worker on node " << workerNode->nodeId_
                    << " @" << workerNode->host_;
    }

    try
    {
        workerNode->dataPort_ =
                boost::lexical_cast<port_t>(znode.getStrValue(ZNode::KEY_DATA_PORT));
    }
    catch (std::exception& e)
    {
        workerNode->worker_.isGood_ = false;
        LOG (ERROR) << "failed to convert dataPort \"" << znode.getStrValue(ZNode::KEY_DATA_PORT)
                    << "\" got from worker on node " << workerNode->nodeId_
                    << " @" << workerNode->host_;
    }

    LOG (INFO) << CLASSNAME << " detected worker on (node" << workerNode->nodeId_ <<") "
               << workerNode->host_ << ":" << workerNode->worker_.port_ << std::endl;
}

void MasterManagerBase::detectReplicaSet(const std::string& zpath)
{
    // find replications
    std::vector<std::string> childrenList;
    zookeeper_->getZNodeChildren(topologyPath_, childrenList, ZooKeeper::WATCH);

    replicaIdList_.clear();
    for (size_t i = 0; i < childrenList.size(); i++)
    {
        std::string sreplicaId;
        zookeeper_->getZNodeData(childrenList[i], sreplicaId);
        try
        {
            replicaIdList_.push_back(boost::lexical_cast<replicaid_t>(sreplicaId));
            LOG (INFO) << " detected replica id \"" << sreplicaId
                << "\" for " << childrenList[i];
        }
        catch (std::exception& e) {
            LOG (ERROR) << CLASSNAME << " failed to parse replica id \"" << sreplicaId
                << "\" for " << childrenList[i];
        }

        // watch for nodes change
        std::vector<std::string> chchList;
        zookeeper_->getZNodeChildren(childrenList[i], chchList, ZooKeeper::WATCH);
        zookeeper_->isZNodeExists(childrenList[i], ZooKeeper::WATCH);
    }

    // try to detect workers again while waiting for some of the workers
    if (masterState_ == MASTER_STATE_STARTING_WAIT_WORKERS)
    {
        detectWorkers();
    }

    bool need_reset_agg = false;
    WorkerMapT::iterator it;
    for (it = workerMap_.begin(); it != workerMap_.end(); it++)
    {
        boost::shared_ptr<Sf1rNode>& sf1rNode = it->second;
        if (!sf1rNode->worker_.isGood_)
        {
            // try failover
            if (!failover(sf1rNode))
            {
                LOG(WARNING) << "one of worker failed and can not cover this failure.";
                masterState_ = MASTER_STATE_STARTING_WAIT_WORKERS;
            }
            need_reset_agg = true;
        }
    }

    if (need_reset_agg)
    {
        // notify aggregators
        resetAggregatorConfig();
    }
}

void MasterManagerBase::failover(const std::string& zpath)
{
    // check path
    WorkerMapT::iterator it;
    for (it = workerMap_.begin(); it != workerMap_.end(); it++)
    {
        boost::shared_ptr<Sf1rNode>& sf1rNode = it->second;
        std::string nodePath = getNodePath(sf1rNode->replicaId_, sf1rNode->nodeId_);
        if (zpath == nodePath)
        {
            LOG (WARNING) << "[node " << sf1rNode->nodeId_ << "]@" << sf1rNode->host_ << " was broken down, in "
                          << "[replica " << sf1rNode->replicaId_ << "]";
            if (failover(sf1rNode))
            {
                LOG (INFO) << "failover: finished.";
            }
            else
            {
                LOG (INFO) << "failover: failed to cover this failure.";
                masterState_ = MASTER_STATE_STARTING_WAIT_WORKERS;
            }
            // notify aggregators
            resetAggregatorConfig();
            return;
        }
    }
    LOG (INFO) << "failed node is not in my watching workers . " << zpath;
}

bool MasterManagerBase::failover(boost::shared_ptr<Sf1rNode>& sf1rNode)
{
    sf1rNode->worker_.isGood_ = false;
    bool mine_primary = isMinePrimary();
    if (mine_primary)
        LOG(INFO) << "I am primary master ";
    for (size_t i = 0; i < replicaIdList_.size(); i++)
    {
        if (replicaIdList_[i] != sf1rNode->replicaId_)
        {
            // try to switch to replicaIdList_[i]
            ZNode znode;
            std::string sdata;
            std::string nodePath = getNodePath(replicaIdList_[i], sf1rNode->nodeId_);
            // get node data
            if (zookeeper_->getZNodeData(nodePath, sdata, ZooKeeper::WATCH))
            {
                if (mine_primary)
                {
                    if(!isPrimaryWorker(replicaIdList_[i], sf1rNode->nodeId_))
                    {
                        LOG(INFO) << "primary master need failover to primary worker, ignore non-primary worker";
                        LOG (INFO) << "node " << sf1rNode->nodeId_ << " ,replica: " << replicaIdList_[i];
                        continue;
                    }
                }
                znode.loadKvString(sdata);
                if (znode.hasKey(ZNode::KEY_WORKER_PORT))
                {
                    LOG (INFO) << "switching node " << sf1rNode->nodeId_ << " from replica " << sf1rNode->replicaId_
                               <<" to " << replicaIdList_[i];

                    sf1rNode->replicaId_ = replicaIdList_[i]; // new replica
                    sf1rNode->host_ = znode.getStrValue(ZNode::KEY_HOST);
                    try
                    {
                        sf1rNode->worker_.port_ =
                                boost::lexical_cast<port_t>(znode.getStrValue(ZNode::KEY_WORKER_PORT));
                    }
                    catch (std::exception& e)
                    {
                        LOG (ERROR) << "failed to convert workerPort \"" << znode.getStrValue(ZNode::KEY_WORKER_PORT)
                                    << "\" got from node " << sf1rNode->nodeId_ << " at " << znode.getStrValue(ZNode::KEY_HOST)
                                    << ", in replica " << replicaIdList_[i];
                        continue;
                    }

                    // succeed to failover
                    sf1rNode->worker_.isGood_ = true;
                    break;
                }
                else
                {
                    LOG (ERROR) << "[Replica " << replicaIdList_[i] << "] [Node " << sf1rNode->nodeId_
                                << "] did not enable worker server, this happened because of the mismatch configuration.";
                    LOG (ERROR) << "In the same cluster, the sf1r node with the same nodeid must have the same configuration.";
                    //throw std::runtime_error("error configuration : mismatch with the same nodeid.");
                }
            }
        }
    }


    // Watch current replica, waiting for node recover
    zookeeper_->isZNodeExists(getNodePath(sf1rNode->replicaId_, sf1rNode->nodeId_), ZooKeeper::WATCH);

    return sf1rNode->worker_.isGood_;
}


void MasterManagerBase::recover(const std::string& zpath)
{
    WorkerMapT::iterator it;
    bool mine_primary = isMinePrimary();
    if (mine_primary)
        LOG(INFO) << "I am primary master ";

    bool need_reset_agg = false;

    for (it = workerMap_.begin(); it != workerMap_.end(); it++)
    {
        boost::shared_ptr<Sf1rNode>& sf1rNode = it->second;
        if (zpath == getNodePath(sf1rTopology_.curNode_.replicaId_, sf1rNode->nodeId_))
        {
            if (mine_primary)
            {
                if(!isPrimaryWorker(sf1rTopology_.curNode_.replicaId_, sf1rNode->nodeId_))
                {
                    LOG(INFO) << "primary master need recover to primary worker, ignore non-primary worker";
                    LOG (INFO) << "node " << sf1rNode->nodeId_ << " ,replica: " << sf1rTopology_.curNode_.replicaId_;
                    continue;
                }
            }

            LOG (INFO) << "recover: node " << sf1rNode->nodeId_
                       << " recovered in current replica " << sf1rTopology_.curNode_.replicaId_;

            ZNode znode;
            std::string sdata;
            if (zookeeper_->getZNodeData(zpath, sdata, ZooKeeper::WATCH))
            {
                // try to recover
                znode.loadKvString(sdata);
                try
                {
                    sf1rNode->worker_.port_ =
                            boost::lexical_cast<port_t>(znode.getStrValue(ZNode::KEY_WORKER_PORT));
                }
                catch (std::exception& e)
                {
                    LOG (ERROR) << "failed to convert workerPort \"" << znode.getStrValue(ZNode::KEY_WORKER_PORT)
                                << "\" got from node " << sf1rNode->nodeId_ << " at " << znode.getStrValue(ZNode::KEY_HOST)
                                << ", in replica " << sf1rTopology_.curNode_.replicaId_;
                    continue;
                }

                sf1rNode->replicaId_ = sf1rTopology_.curNode_.replicaId_; // new replica
                sf1rNode->host_ = znode.getStrValue(ZNode::KEY_HOST);
                // recovered, and notify aggregators
                sf1rNode->worker_.isGood_ = true;
                need_reset_agg = true;
                break;
            }
        }
    }

    if (need_reset_agg)
        resetAggregatorConfig();
}

void MasterManagerBase::setServicesData(ZNode& znode)
{
    // write service name to server node.
    std::string services;
    ServiceMapT::const_iterator cit = all_distributed_services_.begin();
    while(cit != all_distributed_services_.end())
    {
        if (services.empty())
            services = cit->first;
        else
            services += "," + cit->first;

        std::string collections;
        const std::vector<MasterCollection>& collectionList = sf1rTopology_.curNode_.master_.getMasterCollList(cit->first);
        for (std::vector<MasterCollection>::const_iterator it = collectionList.begin();
                it != collectionList.end(); it++)
        {
            if (collections.empty())
                collections = (*it).name_;
            else
                collections += "," + (*it).name_;
        }
 
        znode.setValue(cit->first + ZNode::KEY_COLLECTION, collections);

        ++cit;
    }
    znode.setValue(ZNode::KEY_REPLICA_ID, sf1rTopology_.curNode_.replicaId_);
    znode.setValue(ZNode::KEY_SERVICE_NAMES, services);
    znode.setValue(ZNode::KEY_SERVICE_STATE, "ReadyForRead");
    if (sf1rTopology_.curNode_.master_.hasAnyService())
    {
        znode.setValue(ZNode::KEY_MASTER_PORT, sf1rTopology_.curNode_.master_.port_);
        znode.setValue(ZNode::KEY_MASTER_NAME, sf1rTopology_.curNode_.master_.name_);
    }
}

void MasterManagerBase::initServices()
{
}

void MasterManagerBase::updateServiceReadState(const std::string& my_state, bool include_self)
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    updateServiceReadStateWithoutLock(my_state, include_self);
}

void MasterManagerBase::updateServiceReadStateWithoutLock(const std::string& my_state, bool include_self)
{
    // service is ready for read means all shard workers current master connected are ready for read.
    if (masterState_ != MASTER_STATE_STARTED && masterState_ != MASTER_STATE_STARTING_WAIT_WORKERS)
    {
        return;
    }
    if (stopping_)
        return;
    ZNode znode;
    std::string olddata;
    if(zookeeper_->getZNodeData(serverRealPath_, olddata, ZooKeeper::WATCH))
    {
        if (olddata.empty())
            return;
        znode.loadKvString(olddata);
    }
    else
    {
        LOG(INFO) << "get server service data error";
        return;
    }

    std::string new_state = my_state;
    std::string old_state = znode.getStrValue(ZNode::KEY_SERVICE_STATE);
    if (my_state == "BusyForShard" || my_state == "ReadyForRead")
    {
        WorkerMapT::const_iterator it = workerMap_.begin();
        bool all_ready = true;
        for ( ; it != workerMap_.end(); ++it)
        {
            if (it->second->nodeId_ == sf1rTopology_.curNode_.nodeId_)
            {
                if (!include_self)
                    continue;
            }
            std::string nodepath = getNodePath(it->second->replicaId_, it->second->nodeId_);
            std::string sdata;
            if (zookeeper_->getZNodeData(nodepath, sdata, ZooKeeper::WATCH))
            {
                ZNode worker_znode;
                worker_znode.loadKvString(sdata);
                std::string value = worker_znode.getStrValue(ZNode::KEY_SERVICE_STATE);
                if (value != "ReadyForRead" && value != "BusyForShard")
                {
                    LOG(INFO) << "one shard of master service is not ready for read:" << nodepath;
                    all_ready = false;
                    if (it->second->nodeId_ == sf1rTopology_.curNode_.nodeId_)
                    {
                        new_state = "BusyForSelf";
                    }
                    else
                        new_state = "BusyForShard";
                    break;
                }
            }
            else
            {
                LOG(INFO) << "get node data failed: " << nodepath;
                if (it->second->nodeId_ == sf1rTopology_.curNode_.nodeId_)
                {
                    all_ready = false;
                    new_state = "BusyForSelf";
                    break;
                }
            }
        }
        if (all_ready)
            new_state = "ReadyForRead";
    }
    if (old_state == new_state)
        return;

    znode.setValue(ZNode::KEY_HOST, sf1rTopology_.curNode_.host_);
    znode.setValue(ZNode::KEY_BA_PORT, sf1rTopology_.curNode_.baPort_);
    znode.setValue(ZNode::KEY_MASTER_PORT, SuperNodeManager::get()->getMasterPort());

    setServicesData(znode);
    LOG(INFO) << "current master service state changed : " << old_state << " to " << new_state;
    znode.setValue(ZNode::KEY_SERVICE_STATE, new_state);
    LOG(INFO) << "server service old data " << olddata;
    zookeeper_->setZNodeData(serverRealPath_, znode.serialize());
}

void MasterManagerBase::registerDistributeServiceMaster(boost::shared_ptr<IDistributeService> sp_service, bool enable_master)
{
    if (enable_master)
    {
        if (all_distributed_services_.find(sp_service->getServiceName()) != 
            all_distributed_services_.end() )
        {
            LOG(WARNING) << "duplicate service name!!!!!!!";
            throw std::runtime_error("duplicate service!");
        }
        LOG(INFO) << "registering service master: " << sp_service->getServiceName();
        all_distributed_services_[sp_service->getServiceName()] = sp_service;
    }
}

bool MasterManagerBase::findServiceMasterAddress(const std::string& service, std::string& host, uint32_t& port)
{
    if (!zookeeper_ || !zookeeper_->isConnected())
        return false;

    std::vector<std::string> children;
    zookeeper_->getZNodeChildren(serverParentPath_, children);

    for (size_t i = 0; i < children.size(); ++i)
    {
        std::string serviceMasterPath = children[i];

        std::string data;
        if (zookeeper_->getZNodeData(serviceMasterPath, data))
        {
            ZNode znode;
            znode.loadKvString(data);
            std::string service_names = znode.getStrValue(ZNode::KEY_SERVICE_NAMES);
            if (service_names.find(service) == std::string::npos)
                continue;

            LOG(INFO) << "find service master address success : " << service << ", on server :" << serviceMasterPath;
            host = znode.getStrValue(ZNode::KEY_HOST);
            port = znode.getUInt32Value(ZNode::KEY_MASTER_PORT);
            return true;
        }
    }
    return false;
}

void MasterManagerBase::registerServiceServer()
{
    // Master server provide search service
    if (!zookeeper_->isZNodeExists(serverParentPath_))
    {
        zookeeper_->createZNode(serverParentPath_);
    }

    initServices();

    ZNode znode;
    znode.setValue(ZNode::KEY_HOST, sf1rTopology_.curNode_.host_);
    znode.setValue(ZNode::KEY_BA_PORT, sf1rTopology_.curNode_.baPort_);
    znode.setValue(ZNode::KEY_MASTER_PORT, SuperNodeManager::get()->getMasterPort());

    setServicesData(znode);

    if (zookeeper_->createZNode(serverPath_, znode.serialize(), ZooKeeper::ZNODE_EPHEMERAL_SEQUENCE))
    {
        serverRealPath_ = zookeeper_->getLastCreatedNodePath();
        LOG(INFO) << "self server : " << serverRealPath_ << ", data:" << znode.serialize();
    }
    if (!zookeeper_->isZNodeExists(write_req_queue_root_parent_, ZooKeeper::WATCH))
    {
        zookeeper_->createZNode(write_req_queue_root_parent_);
    }
    if (!zookeeper_->isZNodeExists(write_req_queue_parent_, ZooKeeper::WATCH))
    {
        zookeeper_->createZNode(write_req_queue_parent_);
    }
    if (!zookeeper_->isZNodeExists(write_prepare_node_parent_, ZooKeeper::WATCH))
    {
        zookeeper_->createZNode(write_prepare_node_parent_);
    }
    std::vector<string> reqchild;
    zookeeper_->getZNodeChildren(write_req_queue_parent_, reqchild, ZooKeeper::WATCH);
}

void MasterManagerBase::resetAggregatorConfig(boost::shared_ptr<AggregatorBase>& aggregator)
{
    LOG(INFO) << "resetting aggregator...";
    // get shardids for collection of aggregator
    std::vector<shardid_t> shardidList;
    if (!sf1rTopology_.curNode_.master_.getShardidList(aggregator->service(),
            aggregator->collection(), shardidList))
    {
        LOG(INFO) << "no shard nodes for aggregator : " << aggregator->collection();
        return;
    }

    // set workers for aggregator
    AggregatorConfig aggregatorConfig;
    for (size_t i = 0; i < shardidList.size(); i++)
    {
        WorkerMapT::iterator it = workerMap_.find(shardidList[i]);
        if (it != workerMap_.end())
        {
            if(!it->second->worker_.isGood_)
            {
                LOG(INFO) << "worker_ : " << it->second->nodeId_ << " is not good, so do not added to aggregator.";
                continue;
            }
            bool isLocal = (it->second->nodeId_ == sf1rTopology_.curNode_.nodeId_);
            aggregatorConfig.addWorker(it->second->host_, it->second->worker_.port_, shardidList[i], isLocal);
        }
        else
        {
            LOG (ERROR) << "worker " << shardidList[i] << " was not found for Aggregator of "
                << aggregator->collection() << " in service " << aggregator->service();
        }
    }

    LOG(INFO) << aggregator->collection() << ":" << aggregatorConfig.toString();
    aggregator->setAggregatorConfig(aggregatorConfig, true);
}

void MasterManagerBase::resetAggregatorConfig()
{
    std::vector<boost::shared_ptr<AggregatorBase> >::iterator agg_it;
    for (agg_it = aggregatorList_.begin(); agg_it != aggregatorList_.end(); ++agg_it)
    {
        resetAggregatorConfig(*agg_it);
    }
}

bool MasterManagerBase::isPrimaryWorker(replicaid_t replicaId, nodeid_t nodeId)
{
    std::string nodepath = getNodePath(replicaId,  nodeId);
    std::string sdata;
    if (zookeeper_->getZNodeData(nodepath, sdata, ZooKeeper::WATCH))
    {
        ZNode znode;
        znode.loadKvString(sdata);
        std::string self_reg_primary = znode.getStrValue(ZNode::KEY_SELF_REG_PRIMARY_PATH);
        std::vector<std::string> node_list;
        zookeeper_->getZNodeChildren(getPrimaryNodeParentPath(nodeId), node_list);
        if (node_list.empty())
        {
            LOG(INFO) << "no any primary node for node id: " << nodeId;
            return false;
        }
        return self_reg_primary == node_list[0];
    }
    else
        return false;
}

bool MasterManagerBase::isMinePrimary()
{
    if(!isDistributeEnable_)
        return true;

    if (!zookeeper_ || !zookeeper_->isConnected())
        return false;
    return is_mine_primary_;
}

void MasterManagerBase::updateMasterReadyForNew(bool is_ready)
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    is_ready_for_new_write_ = is_ready;
    if (is_ready_for_new_write_)
    {
        if (!isMinePrimary() || stopping_)
            return;
        checkForWriteReq();
    }
}

bool MasterManagerBase::hasAnyCachedRequest()
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    return is_mine_primary_ && !cached_write_reqlist_.empty();
}

void MasterManagerBase::notifyChangedPrimary(bool is_new_primary)
{
    boost::lock_guard<boost::mutex> lock(state_mutex_);
    if (!is_new_primary)
    {
        // try to delete last prepared node.
        endWriteReq();
    }
    is_mine_primary_ = is_new_primary;
    LOG(INFO) << "mine primary master state changed: " << is_new_primary;
    if (is_new_primary)
    {
        if (masterState_ == MASTER_STATE_STARTED || masterState_ == MASTER_STATE_STARTING_WAIT_WORKERS)
        {
            if (stopping_)
                return;
            // reset current workers, need detect primary workers.
            detectWorkers();
            zookeeper_->isZNodeExists(write_prepare_node_, ZooKeeper::WATCH);
            if (cached_write_reqlist_.empty())
                cacheNewWriteFromZNode();
        }
    }
}

