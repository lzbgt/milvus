// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#include "server/delivery/request/CreatePartitionRequest.h"
#include "server/DBWrapper.h"
#include "utils/Log.h"
#include "utils/TimeRecorder.h"
#include "utils/ValidationUtil.h"

#include <fiu-local.h>
#include <memory>
#include <string>

namespace milvus {
namespace server {

CreatePartitionRequest::CreatePartitionRequest(const std::shared_ptr<milvus::server::Context>& context,
                                               const std::string& collection_name, const std::string& tag)
    : BaseRequest(context, BaseRequest::kCreatePartition), collection_name_(collection_name), tag_(tag) {
}

BaseRequestPtr
CreatePartitionRequest::Create(const std::shared_ptr<milvus::server::Context>& context,
                               const std::string& collection_name, const std::string& tag) {
    return std::shared_ptr<BaseRequest>(new CreatePartitionRequest(context, collection_name, tag));
}

Status
CreatePartitionRequest::OnExecute() {
    std::string hdr = "CreatePartitionRequest(collection=" + collection_name_ + ", partition_tag=" + tag_ + ")";
    TimeRecorderAuto rc(hdr);

    try {
        // step 1: check arguments
        auto status = ValidationUtil::ValidateCollectionName(collection_name_);
        fiu_do_on("CreatePartitionRequest.OnExecute.invalid_table_name",
                  status = Status(milvus::SERVER_UNEXPECTED_ERROR, ""));
        if (!status.ok()) {
            return status;
        }

        if (tag_ == milvus::engine::DEFAULT_PARTITON_TAG) {
            return Status(SERVER_INVALID_PARTITION_TAG, "'_default' is built-in partition tag");
        }

        status = ValidationUtil::ValidatePartitionTags({tag_});
        fiu_do_on("CreatePartitionRequest.OnExecute.invalid_partition_name",
                  status = Status(milvus::SERVER_UNEXPECTED_ERROR, ""));
        if (!status.ok()) {
            return status;
        }

        // only process root collection, ignore partition collection
        engine::meta::CollectionSchema table_schema;
        table_schema.collection_id_ = collection_name_;
        status = DBWrapper::DB()->DescribeCollection(table_schema);
        fiu_do_on("CreatePartitionRequest.OnExecute.invalid_partition_tags",
                  status = Status(milvus::SERVER_UNEXPECTED_ERROR, ""));
        if (!status.ok()) {
            if (status.code() == DB_NOT_FOUND) {
                return Status(SERVER_TABLE_NOT_EXIST, TableNotExistMsg(collection_name_));
            } else {
                return status;
            }
        } else {
            if (!table_schema.owner_collection_.empty()) {
                return Status(SERVER_INVALID_TABLE_NAME, TableNotExistMsg(collection_name_));
            }
        }

        rc.RecordSection("check validation");

        // step 2: create partition
        status = DBWrapper::DB()->CreatePartition(collection_name_, "", tag_);
        fiu_do_on("CreatePartitionRequest.OnExecute.db_already_exist", status = Status(milvus::DB_ALREADY_EXIST, ""));
        fiu_do_on("CreatePartitionRequest.OnExecute.create_partition_fail",
                  status = Status(milvus::SERVER_UNEXPECTED_ERROR, ""));
        fiu_do_on("CreatePartitionRequest.OnExecute.throw_std_exception", throw std::exception());
        if (!status.ok()) {
            // partition could exist
            if (status.code() == DB_ALREADY_EXIST) {
                return Status(SERVER_INVALID_TABLE_NAME, status.message());
            }
            return status;
        }
    } catch (std::exception& ex) {
        return Status(SERVER_UNEXPECTED_ERROR, ex.what());
    }

    return Status::OK();
}

}  // namespace server
}  // namespace milvus
