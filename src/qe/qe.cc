#include "src/include/qe.h"

#include <complex>
#include <unordered_map>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <utility>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <cassert>
#include <iterator>
#include <stdexcept>
#include <memory>

namespace PeterDB {
    Filter::Filter(Iterator *input, const Condition &condition) {
        // Generate unique name
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        std::string filterFileName = "filter_" + std::to_string(rbfm.getFilterFileNum()) + "_scan";
        rbfm.incrementFilterFileNum();
        // std::cout << rbfm.getFilterFileNum() << std::endl;
        // Initialize the RBFM Scan Iterator
        input->getAttributes(this->recordDescriptor);
        this->rbfmScanIterator = new RBFM_ScanIterator(filterFileName);
        this->rbfmScanIterator->initialize(filterFileName);
        this->rbfmScanIterator->setRecordDescriptor(this->recordDescriptor);

        // Find LHS attribute index and type
        int lhsAttrIndex = -1;
        AttrType lhsAttrType;
        for (size_t i = 0; i < recordDescriptor.size(); i++) {
            if (recordDescriptor[i].name == condition.lhsAttr) {
                lhsAttrIndex = i;
                lhsAttrType = recordDescriptor[i].type;
                break;
            }
        }

        if (lhsAttrIndex == -1) {
            std::cerr << "Error: Left-hand side attribute not found." << std::endl;
            return;
        }

        // Extract RHS value from condition
        void *rhsValue = condition.rhsValue.data;

        // Allocate space for tuple processing
        void *tupleData = malloc(PAGE_SIZE);
        RID rid;

        // Iterate through input tuples
        while (input->getNextTuple(tupleData) == 0) {
            const char *dataPtr = static_cast<const char *>(tupleData);
            size_t nullIndicatorSize = ceil(recordDescriptor.size() / 8.0);
            dataPtr += nullIndicatorSize;
            size_t offset = 0;

            // Extract LHS value
            void *lhsValue = nullptr;
            for (int i = 0; i <= lhsAttrIndex; i++) {
                if (recordDescriptor[i].type == TypeInt || recordDescriptor[i].type == TypeReal) {
                    if (i == lhsAttrIndex) lhsValue = (void *)(dataPtr + offset);
                    offset += 4;
                } else if (recordDescriptor[i].type == TypeVarChar) {
                    int varLen;
                    memcpy(&varLen, dataPtr + offset, sizeof(int));
                    if (i == lhsAttrIndex) lhsValue = (void *)(dataPtr + offset);
                    offset += 4 + varLen;
                }
            }

            // Compare values
            bool insertTuple = false;
            if (lhsAttrType == TypeInt) {
                int lhs = *(int *)lhsValue;
                int rhs = *(int *)rhsValue;
                switch (condition.op) {
                    case EQ_OP: insertTuple = (lhs == rhs); break;
                    case LT_OP: insertTuple = (lhs < rhs); break;
                    case LE_OP: insertTuple = (lhs <= rhs); break;
                    case GT_OP: insertTuple = (lhs > rhs); break;
                    case GE_OP: insertTuple = (lhs >= rhs); break;
                    case NE_OP: insertTuple = (lhs != rhs); break;
                    case NO_OP: insertTuple = true; break;
                }
            } else if (lhsAttrType == TypeReal) {
                float lhs = *(float *)lhsValue;
                float rhs = *(float *)rhsValue;
                switch (condition.op) {
                    case EQ_OP: insertTuple = (lhs == rhs); break;
                    case LT_OP: insertTuple = (lhs < rhs); break;
                    case LE_OP: insertTuple = (lhs <= rhs); break;
                    case GT_OP: insertTuple = (lhs > rhs); break;
                    case GE_OP: insertTuple = (lhs >= rhs); break;
                    case NE_OP: insertTuple = (lhs != rhs); break;
                    case NO_OP: insertTuple = true; break;
                }
            } else if (lhsAttrType == TypeVarChar) {
                int lhsLen;
                memcpy(&lhsLen, lhsValue, sizeof(int));
                std::string lhsStr((char *)lhsValue + 4, lhsLen);

                int rhsLen;
                memcpy(&rhsLen, rhsValue, sizeof(int));
                std::string rhsStr((char *)rhsValue + 4, rhsLen);

                switch (condition.op) {
                    case EQ_OP: insertTuple = (lhsStr == rhsStr); break;
                    case LT_OP: insertTuple = (lhsStr < rhsStr); break;
                    case LE_OP: insertTuple = (lhsStr <= rhsStr); break;
                    case GT_OP: insertTuple = (lhsStr > rhsStr); break;
                    case GE_OP: insertTuple = (lhsStr >= rhsStr); break;
                    case NE_OP: insertTuple = (lhsStr != rhsStr); break;
                    case NO_OP: insertTuple = true; break;
                }
            }
            // Insert tuple if it matches filter condition
            if (insertTuple) {
                this->rbfmScanIterator->insertData(tupleData, rid, this->recordDescriptor);
            }
        }
        free(tupleData);
    }

    Filter::~Filter() {
        this->rbfmScanIterator->close();
    }

    RC Filter::getNextTuple(void *data) {
        RID rid;
        if (this->rbfmScanIterator->getNextRecord(rid, data) == -1) {
            return -1;
        }
        return 0;
    }

    RC Filter::getAttributes(std::vector<Attribute> &attrs) const {
        attrs = this->recordDescriptor;
        return 0;
    }

    Project::Project(Iterator *input, const std::vector<std::string> &attrNames) {
        // Generate unique name
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        std::string projectFileName = "project_" + std::to_string(rbfm.getProjectFileNum()) + "_scan";
        rbfm.incrementProjectFileNum();

        // Initialize the RBFM Scan Iterator
        std::vector<Attribute> inputDescriptor;
        input->getAttributes(inputDescriptor);
        this->rbfmScanIterator = new RBFM_ScanIterator(projectFileName);
        this->rbfmScanIterator->initialize(projectFileName);

        // Find the indices of the attributes we want to project
        std::vector<int> attrIndices;
        this->recordDescriptor.clear();

        for (const std::string &attrName : attrNames) {
            for (size_t i = 0; i < inputDescriptor.size(); i++) {
                if (inputDescriptor[i].name == attrName) {
                    attrIndices.push_back(i);
                    this->recordDescriptor.push_back(inputDescriptor[i]);  // Store only projected attributes
                    break;
                }
            }
        }

        // Set the record descriptor for projected attributes
        this->rbfmScanIterator->setRecordDescriptor(this->recordDescriptor);

        // Compute null indicator sizes
        size_t inputNullIndicatorSize = ceil(inputDescriptor.size() / 8.0);
        size_t projectedNullIndicatorSize = ceil(recordDescriptor.size() / 8.0);

        // Allocate memory for processing tuples
        void *tupleData = malloc(PAGE_SIZE);
        RID rid;

        // Iterate through input tuples
        while (input->getNextTuple(tupleData) == 0) {  // 0 means success
            const char *dataPtr = static_cast<const char *>(tupleData);

            // Read the null indicator from the original tuple
            const char *inputNullIndicator = dataPtr;
            dataPtr += inputNullIndicatorSize;

            // Prepare space for projected record
            char projectedData[PAGE_SIZE];
            memset(projectedData, 0, PAGE_SIZE);
            char *projectedNullIndicator = projectedData;
            size_t projectedOffset = projectedNullIndicatorSize;

            size_t projectedAttrIndex = 0;

            // Iterate through the attributes in attrNames to extract values in order
            for (const std::string &attrName : attrNames) {
                size_t offset = 0;
                bool found = false;
                const Attribute *attr = nullptr;

                // Find the corresponding attribute in the original record descriptor
                for (size_t i = 0; i < inputDescriptor.size(); ++i) {
                    if (inputDescriptor[i].name == attrName) {
                        attr = &inputDescriptor[i];
                        found = true;
                        break;
                    }
                    if (inputDescriptor[i].type == TypeReal || inputDescriptor[i].type == TypeInt) {
                        offset += sizeof(float);
                    } else if (inputDescriptor[i].type == TypeVarChar) {
                        int strLen;
                        memcpy(&strLen, dataPtr + offset, sizeof(int));
                        offset += sizeof(int) + strLen;
                    }
                }
                bool isNull = false;
                if (found) {
                    // Check if the attribute is NULL in the original record
                    size_t bytePos = projectedAttrIndex / 8;
                    size_t bitPos = 7 - (projectedAttrIndex % 8);
                    isNull = inputNullIndicator[bytePos] & (1 << bitPos);

                    if (isNull) {
                        // Mark this attribute as NULL in the projected null indicator
                        projectedNullIndicator[projectedAttrIndex / 8] |= (1 << (7 - (projectedAttrIndex % 8)));
                    } else {
                        // Process the attribute based on its type
                        if (attr->type == TypeInt) {
                            int value;
                            memcpy(&value, dataPtr + offset, sizeof(int));
                            memcpy(projectedData + projectedOffset, &value, sizeof(int));
                            projectedOffset += sizeof(int);
                        } else if (attr->type == TypeReal) {
                            float value;
                            memcpy(&value, dataPtr + offset, sizeof(float));
                            memcpy(projectedData + projectedOffset, &value, sizeof(float));
                            projectedOffset += sizeof(float);
                        } else if (attr->type == TypeVarChar) {
                            uint32_t strLen;
                            memcpy(&strLen, dataPtr + offset, sizeof(uint32_t));
                            offset += sizeof(uint32_t);

                            std::string strValue(dataPtr + offset, strLen);

                            memcpy(projectedData + projectedOffset, &strLen, sizeof(uint32_t));
                            projectedOffset += sizeof(uint32_t);
                            memcpy(projectedData + projectedOffset, dataPtr + offset, strLen);
                            projectedOffset += strLen;
                        }
                    }
                    projectedAttrIndex++;
                } else {
                    std::cerr << "Attribute " << attrName << " not found in the original record descriptor!" << std::endl;
                }
            }
            // Insert the projected tuple into the scan iterator
            this->rbfmScanIterator->insertData(projectedData, rid, this->recordDescriptor);
        }
        free(tupleData);
    }


    Project::~Project() {
        this->rbfmScanIterator->close();
    }

    RC Project::getNextTuple(void *data) {
        RID rid;
        if (this->rbfmScanIterator->getNextRecord(rid, data) == -1) {
            return -1;
        }
        return 0;
    }

    RC Project::getAttributes(std::vector<Attribute> &attrs) const {
        attrs = this->recordDescriptor;
        return 0;
    }

    std::string extractKey(void *tupleData, const Attribute &joinAttr, const std::vector<Attribute> &recordDescriptor) {
        std::string key;
        int offset = 0;

        offset += ceil(recordDescriptor.size() / 8.0);
        // Find the offset of the join attribute in the tuple
        for (size_t i = 0; i < recordDescriptor.size(); ++i) {
            const Attribute &attr = recordDescriptor[i];

            // Check if this attribute is null using the null indicator byte
            int nullByteIndex = i / 8;
            int bitIndex = i % 8;
            char nullByte;
            memcpy(&nullByte, (char*)tupleData + nullByteIndex, sizeof(char));
            bool isNull = (nullByte >> (7 - bitIndex)) & 1;

            if (isNull) {
                continue;
            }

            // Move the offset based on the type size of the previous attribute
            if (attr.name == joinAttr.name) {
                // We found the join attribute, extract the key
                if (attr.type == TypeInt) {
                    int keyValue;
                    memcpy(&keyValue, (char*)tupleData + offset, sizeof(int));
                    key = std::to_string(keyValue);
                    std::cout << keyValue << std::endl;
                    std::cout << key << std::endl;
                } else if (attr.type == TypeVarChar) {
                    int strLength;
                    memcpy(&strLength, (char*)tupleData + offset, sizeof(int));
                    offset += sizeof(int);
                    char* strData = (char*)tupleData + offset;
                    key = std::string(strData, strLength);
                    offset += strLength;
                }
                break;
            }

            // Otherwise, move the offset based on the attribute's type
            if (attr.type == TypeInt) {
                offset += sizeof(int);
            } else if (attr.type == TypeReal) {
                offset += sizeof(float);
            } else if (attr.type == TypeVarChar) {
                int strLength;
                memcpy(&strLength, (char*)tupleData + offset, sizeof(int));
                offset += sizeof(int) + strLength;
            }
        }

        return key;
    }

    float extractFloatKey(void *tupleData, const Attribute &joinAttr, const std::vector<Attribute> &recordDescriptor) {
        int offset = ceil(recordDescriptor.size() / 8.0);

        for (size_t i = 0; i < recordDescriptor.size(); ++i) {
            const Attribute &attr = recordDescriptor[i];

            // Check if this attribute is null
            int nullByteIndex = i / 8;
            int bitIndex = i % 8;
            char nullByte;
            memcpy(&nullByte, (char*)tupleData + nullByteIndex, sizeof(char));
            bool isNull = (nullByte >> (7 - bitIndex)) & 1;

            if (isNull) {
                continue;
            }

            // If this is the join attribute and it's a float, extract it
            if (attr.name == joinAttr.name && attr.type == TypeReal) {
                float keyValue;
                memcpy(&keyValue, (char*)tupleData + offset, sizeof(float));
                return keyValue;
            }

            // Move offset based on type
            if (attr.type == TypeInt) {
                offset += sizeof(int);
            } else if (attr.type == TypeReal) {
                offset += sizeof(float);
            } else if (attr.type == TypeVarChar) {
                int strLength;
                memcpy(&strLength, (char*)tupleData + offset, sizeof(int));
                offset += sizeof(int) + strLength;
            }
        }

        return NULL;
    }



    void mergeRecords(const void *leftTuple, const void *rightTuple,
                  const std::vector<Attribute> &leftAttrs, const std::vector<Attribute> &rightAttrs,
                  void *joinedTuple) {
        // Compute null indicator sizes
        size_t leftNullIndicatorSize = ceil(leftAttrs.size() / 8.0);
        size_t rightNullIndicatorSize = ceil(rightAttrs.size() / 8.0);
        size_t joinedNullIndicatorSize = ceil((leftAttrs.size() + rightAttrs.size()) / 8.0);

        // Initialize joined tuple memory
        memset(joinedTuple, 0, PAGE_SIZE);
        char *joinedData = static_cast<char *>(joinedTuple);
        char *joinedNullIndicator = joinedData;
        size_t joinedOffset = joinedNullIndicatorSize;

        // Pointers to left and right null indicators
        const char *leftNullIndicator = static_cast<const char *>(leftTuple);
        const char *rightNullIndicator = static_cast<const char *>(rightTuple);

        // Data pointers
        const char *leftData = leftNullIndicator + leftNullIndicatorSize;
        const char *rightData = rightNullIndicator + rightNullIndicatorSize;

        size_t leftOffset = 0;
        size_t rightOffset = 0;
        size_t joinedAttrIndex = 0;

        // Process left tuple attributes
        for (size_t i = 0; i < leftAttrs.size(); i++, joinedAttrIndex++) {
            size_t bytePos = i / 8;
            size_t bitPos = 7 - (i % 8);
            bool isNull = leftNullIndicator[bytePos] & (1 << bitPos);

            if (isNull) {
                joinedNullIndicator[joinedAttrIndex / 8] |= (1 << (7 - (joinedAttrIndex % 8)));  // Mark NULL
            } else {
                if (leftAttrs[i].type == TypeInt) {
                    memcpy(joinedData + joinedOffset, leftData + leftOffset, sizeof(int));
                    leftOffset += sizeof(int);
                    joinedOffset += sizeof(int);
                } else if (leftAttrs[i].type == TypeReal) {
                    memcpy(joinedData + joinedOffset, leftData + leftOffset, sizeof(float));
                    leftOffset += sizeof(float);
                    joinedOffset += sizeof(float);
                } else if (leftAttrs[i].type == TypeVarChar) {
                    uint32_t strLen;
                    memcpy(&strLen, leftData + leftOffset, sizeof(uint32_t));
                    memcpy(joinedData + joinedOffset, &strLen, sizeof(uint32_t));
                    leftOffset += sizeof(uint32_t);
                    joinedOffset += sizeof(uint32_t);
                    memcpy(joinedData + joinedOffset, leftData + leftOffset, strLen);
                    leftOffset += strLen;
                    joinedOffset += strLen;
                }
            }
        }

        // Process right tuple attributes
        for (size_t i = 0; i < rightAttrs.size(); i++, joinedAttrIndex++) {
            size_t bytePos = i / 8;
            size_t bitPos = 7 - (i % 8);
            bool isNull = rightNullIndicator[bytePos] & (1 << bitPos);

            if (isNull) {
                joinedNullIndicator[joinedAttrIndex / 8] |= (1 << (7 - (joinedAttrIndex % 8)));
            } else {
                if (rightAttrs[i].type == TypeInt) {
                    memcpy(joinedData + joinedOffset, rightData + rightOffset, sizeof(int));
                    rightOffset += sizeof(int);
                    joinedOffset += sizeof(int);
                } else if (rightAttrs[i].type == TypeReal) {
                    memcpy(joinedData + joinedOffset, rightData + rightOffset, sizeof(float));
                    rightOffset += sizeof(float);
                    joinedOffset += sizeof(float);
                } else if (rightAttrs[i].type == TypeVarChar) {
                    uint32_t strLen;
                    memcpy(&strLen, rightData + rightOffset, sizeof(uint32_t));
                    memcpy(joinedData + joinedOffset, &strLen, sizeof(uint32_t));
                    rightOffset += sizeof(uint32_t);
                    joinedOffset += sizeof(uint32_t);
                    memcpy(joinedData + joinedOffset, rightData + rightOffset, strLen);
                    rightOffset += strLen;
                    joinedOffset += strLen;
                }
            }
        }
    }

    BNLJoin::BNLJoin(Iterator *leftIn, TableScan *rightIn, const Condition &condition, const unsigned int numPages) {
        // Generate unique name
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        std::string bnlJoinFileName = "bnljoin_" + std::to_string(rbfm.getBNLJoinFIleNum()) + "_scan";
        std::string leftFileName = "bnljoin_left_" + std::to_string(rbfm.getBNLJoinFIleNum());
        rbfm.incrementBNLJoinFIleNum();
        this->rbfmScanIterator = new RBFM_ScanIterator(bnlJoinFileName);
        this->rbfmScanIterator->initialize(bnlJoinFileName);
        rbfm.createFile(leftFileName);
        FileHandle leftFileHandle;
        rbfm.openFile(leftFileName, leftFileHandle);

        // Get the schema for the left and right relations
        std::vector<Attribute> leftAttrs;
        std::vector<Attribute> rightAttrs;
        leftIn->getAttributes(leftAttrs);
        rightIn->getAttributes(rightAttrs);

        // Concatenate the left and right attributes to form the record descriptor for the join result
        std::vector<Attribute> resultAttrs = leftAttrs;
        resultAttrs.insert(resultAttrs.end(), rightAttrs.begin(), rightAttrs.end());
        this->recordDescriptor = resultAttrs;

        // Identify the type of the join attribute
        Attribute lJoinAttr;
        for (const Attribute &attr : leftAttrs) {
            if (attr.name == condition.lhsAttr) {
                lJoinAttr = attr;
                break;
            }
        }
        Attribute rJoinAttr;
        for (const Attribute &attr : rightAttrs) {
            if (attr.name == condition.rhsAttr) {
                rJoinAttr = attr;
                break;
            }
        }

        // Define a hash tables
        std::unordered_map<std::string, std::vector<RID>> leftHashTable;
        std::unordered_map<float, std::vector<RID>> leftFloatHashTable;
        void *tupleData = malloc(PAGE_SIZE);
        RID rid;
        while (leftIn->getNextTuple(tupleData) == 0) {
            rbfm.insertRecord(leftFileHandle, leftAttrs, tupleData, rid);

            // Extract the join key and insert into hash table
            if (lJoinAttr.type == TypeReal) {
                float key = extractFloatKey(tupleData, lJoinAttr, leftAttrs);
                leftFloatHashTable[key].push_back(rid);
            } else {
                std::string key = extractKey(tupleData, lJoinAttr, leftAttrs);
                leftHashTable[key].push_back(rid);
            }
        }
        free(tupleData);

        void *rightTuple = malloc(PAGE_SIZE);
        void *leftTuple = malloc(PAGE_SIZE);
        void *joinedTuple = malloc(PAGE_SIZE);

        while (rightIn->getNextTuple(rightTuple) == 0) {
            if (rJoinAttr.type == TypeReal) {
                float rightKey = extractFloatKey(rightTuple, rJoinAttr, rightAttrs);
                auto match = leftFloatHashTable.find(rightKey);
                if (match != leftFloatHashTable.end()) {
                    for (const RID &leftRid : match->second) {
                        rbfm.readRecord(leftFileHandle, leftAttrs, rid, leftTuple);
                        mergeRecords(leftTuple, rightTuple, leftAttrs, rightAttrs, joinedTuple);
                        rbfmScanIterator->insertData(joinedTuple, rid, resultAttrs);
                    }
                }
            } else {
                std::string rightKey = extractKey(rightTuple, rJoinAttr, rightAttrs);
                auto match = leftHashTable.find(rightKey);
                if (match != leftHashTable.end()) {
                    for (const RID &leftRid : match->second) {
                        rbfm.readRecord(leftFileHandle, leftAttrs, leftRid, leftTuple);
                        mergeRecords(leftTuple, rightTuple, leftAttrs, rightAttrs, joinedTuple);
                        rbfmScanIterator->insertData(joinedTuple, rid, resultAttrs);
                    }
                }
            }
        }

        free(rightTuple);
        free(leftTuple);
        free(joinedTuple);
        rbfm.destroyFile(leftFileName);
    }

    BNLJoin::~BNLJoin() {
        this->rbfmScanIterator->close();
    }

    RC BNLJoin::getNextTuple(void *data) {
        RID rid;
        if (this->rbfmScanIterator->getNextRecord(rid, data) == -1) {
            return -1;
        }
        return 0;
    }

    RC BNLJoin::getAttributes(std::vector<Attribute> &attrs) const {
        attrs = this->recordDescriptor;
        return 0;
    }

    INLJoin::INLJoin(Iterator *leftIn, IndexScan *rightIn, const Condition &condition) {
        // Generate unique name
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        std::string inlJoinFileName = "inljoin_" + std::to_string(rbfm.getINLJoinFIleNum()) + "_scan";
        std::string leftFileName = "inljoin_left_" + std::to_string(rbfm.getINLJoinFIleNum());
        rbfm.incrementINLJoinFIleNum();
        this->rbfmScanIterator = new RBFM_ScanIterator(inlJoinFileName);
        this->rbfmScanIterator->initialize(inlJoinFileName);
        rbfm.createFile(leftFileName);
        FileHandle leftFileHandle;
        rbfm.openFile(leftFileName, leftFileHandle);

        // Get the schema for the left and right relations
        std::vector<Attribute> leftAttrs;
        std::vector<Attribute> rightAttrs;
        leftIn->getAttributes(leftAttrs);
        rightIn->getAttributes(rightAttrs);

        // Concatenate the left and right attributes to form the record descriptor for the join result
        std::vector<Attribute> resultAttrs = leftAttrs;
        resultAttrs.insert(resultAttrs.end(), rightAttrs.begin(), rightAttrs.end());
        this->recordDescriptor = resultAttrs;

        // Identify the type of the join attribute
        Attribute lJoinAttr;
        for (const Attribute &attr : leftAttrs) {
            if (attr.name == condition.lhsAttr) {
                lJoinAttr = attr;
                break;
            }
        }
        Attribute rJoinAttr;
        for (const Attribute &attr : rightAttrs) {
            if (attr.name == condition.rhsAttr) {
                rJoinAttr = attr;
                break;
            }
        }

        // Define a hash tables
        std::unordered_map<std::string, std::vector<RID>> leftHashTable;
        std::unordered_map<float, std::vector<RID>> leftFloatHashTable;
        void *tupleData = malloc(PAGE_SIZE);
        RID rid;
        while (leftIn->getNextTuple(tupleData) == 0) {
            rbfm.insertRecord(leftFileHandle, leftAttrs, tupleData, rid);

            // Extract the join key and insert into hash table
            if (lJoinAttr.type == TypeReal) {
                float key = extractFloatKey(tupleData, lJoinAttr, leftAttrs);
                leftFloatHashTable[key].push_back(rid);
            } else {
                std::string key = extractKey(tupleData, lJoinAttr, leftAttrs);
                leftHashTable[key].push_back(rid);
            }
        }
        free(tupleData);

        void *rightTuple = malloc(PAGE_SIZE);
        void *leftTuple = malloc(PAGE_SIZE);
        void *joinedTuple = malloc(PAGE_SIZE);

        while (rightIn->getNextTuple(rightTuple) == 0) {
            if (rJoinAttr.type == TypeReal) {
                float rightKey = extractFloatKey(rightTuple, rJoinAttr, rightAttrs);
                auto match = leftFloatHashTable.find(rightKey);
                if (match != leftFloatHashTable.end()) {
                    for (const RID &leftRid : match->second) {
                        rbfm.readRecord(leftFileHandle, leftAttrs, leftRid, leftTuple);
                        mergeRecords(leftTuple, rightTuple, leftAttrs, rightAttrs, joinedTuple);
                        rbfmScanIterator->insertData(joinedTuple, rid, resultAttrs);
                    }
                }
            } else {
                std::string rightKey = extractKey(rightTuple, rJoinAttr, rightAttrs);
                auto match = leftHashTable.find(rightKey);
                if (match != leftHashTable.end()) {
                    for (const RID &leftRid : match->second) {
                        rbfm.readRecord(leftFileHandle, leftAttrs, leftRid, leftTuple);
                        mergeRecords(leftTuple, rightTuple, leftAttrs, rightAttrs, joinedTuple);
                        rbfmScanIterator->insertData(joinedTuple, rid, resultAttrs);
                    }
                }
            }
        }

        free(rightTuple);
        free(leftTuple);
        free(joinedTuple);
        rbfm.destroyFile(leftFileName);
    }

    INLJoin::~INLJoin() {
        this->rbfmScanIterator->close();
    }

    RC INLJoin::getNextTuple(void *data) {
        RID rid;
        if (this->rbfmScanIterator->getNextRecord(rid, data) == -1) {
            return -1;
        }
        return 0;
    }

    RC INLJoin::getAttributes(std::vector<Attribute> &attrs) const {
        attrs = this->recordDescriptor;
        return 0;
    }

    GHJoin::GHJoin(Iterator *leftIn, Iterator *rightIn, const Condition &condition, const unsigned int numPartitions) {

    }

    GHJoin::~GHJoin() {

    }

    RC GHJoin::getNextTuple(void *data) {
        return -1;
    }

    RC GHJoin::getAttributes(std::vector<Attribute> &attrs) const {
        return -1;
    }

    float extractNumericKey(void *tupleData, const Attribute &aggAttr, const std::vector<Attribute> &recordDescriptor) {
        int offset = ceil(recordDescriptor.size() / 8.0);  // Skip null indicator bytes
        for (size_t i = 0; i < recordDescriptor.size(); ++i) {
            const Attribute &attr = recordDescriptor[i];

            // Check if this attribute is null
            int nullByteIndex = i / 8;
            int bitIndex = i % 8;
            char nullByte;
            memcpy(&nullByte, (char*)tupleData + nullByteIndex, sizeof(char));
            bool isNull = (nullByte >> (7 - bitIndex)) & 1;

            if (isNull) {
                continue;
            }

            // If this is the attribute we need, extract it
            if (attr.name == aggAttr.name) {
                if (attr.type == TypeInt) {
                    int intValue;
                    memcpy(&intValue, (char*)tupleData + offset, sizeof(int));
                    return static_cast<float>(intValue);
                } else if (attr.type == TypeReal) {
                    float floatValue;
                    memcpy(&floatValue, (char*)tupleData + offset, sizeof(float));
                    return floatValue;
                }
            }

            // Move offset based on type
            if (attr.type == TypeInt || attr.type == TypeReal) {
                offset += sizeof(int);  // Both int and float are 4 bytes
            } else if (attr.type == TypeVarChar) {
                int strLength;
                memcpy(&strLength, (char*)tupleData + offset, sizeof(int));
                offset += sizeof(int) + strLength;
            }
        }

        return 0.0;
    }

    Aggregate::Aggregate(Iterator *input, const Attribute &aggAttr, AggregateOp op) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        int count = 0;

        // Retrieve the record descriptor
        std::vector<Attribute> inputDescriptor;
        input->getAttributes(inputDescriptor);

        // Define the output attribute name based on the aggregation operation
        std::string aggOpStr;
        switch (op) {
            case MIN: aggOpStr = "MIN"; break;
            case MAX: aggOpStr = "MAX"; break;
            case SUM: aggOpStr = "SUM"; break;
            case AVG: aggOpStr = "AVG"; break;
            case COUNT: aggOpStr = "COUNT"; break;
        }

        Attribute outputAttr;
        outputAttr.name = aggOpStr + "(" + aggAttr.name + ")";
        outputAttr.type = TypeReal;
        outputAttr.length = 4;

        this->recordDescriptor.clear();
        this->recordDescriptor.push_back(outputAttr);

        // Allocate memory for tuple
        void *tupleData = malloc(PAGE_SIZE);

        while (input->getNextTuple(tupleData) == 0) {
            float value = extractNumericKey(tupleData, aggAttr, inputDescriptor);
            switch (op) {
                case MIN:
                    if (count == 0 || value < floatAgg) floatAgg = value;
                break;
                case MAX:
                    if (count == 0 || value > floatAgg) floatAgg = value;
                break;
                case SUM:
                case AVG:
                    floatAgg += value;
                break;
                case COUNT:
                    floatAgg += 1.0;
                break;
            }
            count++;
        }

        if (op == AVG && count > 0) {
            floatAgg /= static_cast<float>(count);
        }

        free(tupleData);
    }

    Aggregate::Aggregate(Iterator *input, const Attribute &aggAttr, const Attribute &groupAttr, AggregateOp op) {

    }

    Aggregate::~Aggregate() {
        this->floatAgg = 0.0;
    }

    RC Aggregate::getNextTuple(void *data) {
        if (this->tupleParsed) {
            return -1;
        }
        size_t numFields = recordDescriptor.size();
        size_t nullIndicatorSize = static_cast<size_t>(ceil(static_cast<double>(numFields) / 8.0));
        memset(data, 0, nullIndicatorSize);
        memcpy(static_cast<char*>(data) + nullIndicatorSize, &this->floatAgg, sizeof(float));
        this->tupleParsed = true;
        return 0;
    }

    RC Aggregate::getAttributes(std::vector<Attribute> &attrs) const {
        attrs = this->recordDescriptor;
        return 0;
    }
} // namespace PeterDB
