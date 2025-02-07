#include "../include/rm.h"
#include <dirent.h>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <utility>
#include <string>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <iterator>
#include <stdexcept>
#include <memory>
#include <cstdlib>
#include <algorithm>


namespace PeterDB {
    RelationManager &RelationManager::instance() {
        static RelationManager _relation_manager = RelationManager();
        return _relation_manager;
    }

    RelationManager::RelationManager() = default;

    RelationManager::~RelationManager() = default;

    RelationManager::RelationManager(const RelationManager &) = default;

    RelationManager &RelationManager::operator=(const RelationManager &) = default;

    RC RelationManager::createCatalog() {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        RID rid;

        // Create and populate "Tables"
        if (rbfm.createFile("Tables") != 0) return -1;

        FileHandle tablesFileHandle;
        if (rbfm.openFile("Tables", tablesFileHandle) != 0) return -1;

        std::vector<Attribute> tablesDescriptor = {
            {"table-id", TypeInt, 4},
            {"table-name", TypeVarChar, 50},
            {"file-name", TypeVarChar, 50}
        };

        char tablesData[PAGE_SIZE];

        std::vector<std::tuple<int, std::string, std::string>> tableEntries = {
            {1, "Tables", "Tables"},
            {2, "Columns", "Columns"}
        };

        for (const auto &entry : tableEntries) {
            int tableId;
            std::string tableName, fileName;

            std::tie(tableId, tableName, fileName) = entry;

            int nameLength = tableName.length();
            int fileNameLength = fileName.length();
            int offset = ceil((double)tablesDescriptor.size() / 8);
            memset(tablesData, 0, offset);

            memcpy(tablesData + offset, &tableId, sizeof(int));
            offset += sizeof(int);

            memcpy(tablesData + offset, &nameLength, sizeof(int));
            offset += sizeof(int);
            memcpy(tablesData + offset, tableName.c_str(), nameLength);
            offset += nameLength;

            memcpy(tablesData + offset, &fileNameLength, sizeof(int));
            offset += sizeof(int);
            memcpy(tablesData + offset, fileName.c_str(), fileNameLength);

            rbfm.insertRecord(tablesFileHandle, tablesDescriptor, tablesData, rid);
        }

        rbfm.closeFile(tablesFileHandle);

        // Create and populate "Columns"
        if (rbfm.createFile("Columns") != 0) {
            return -1;
        }

        FileHandle columnsFileHandle;
        if (rbfm.openFile("Columns", columnsFileHandle) != 0) return -1;

        std::vector<Attribute> columnsDescriptor = {
            {"table-id", TypeInt, 4},
            {"column-name", TypeVarChar, 50},
            {"column-type", TypeInt, 4},
            {"column-length", TypeInt, 4},
            {"column-position", TypeInt, 4}
        };

        char columnsData[PAGE_SIZE];

        std::vector<std::tuple<int, std::string, int, int, int>> columnEntries = {
            {1, "table-id", TypeInt, 4, 1},
            {1, "table-name", TypeVarChar, 50, 2},
            {1, "file-name", TypeVarChar, 50, 3},
            {2, "table-id", TypeInt, 4, 1},
            {2, "column-name", TypeVarChar, 50, 2},
            {2, "column-type", TypeInt, 4, 3},
            {2, "column-length", TypeInt, 4, 4},
            {2, "column-position", TypeInt, 4, 5}
        };

        for (const auto &entry : columnEntries) {
            int tableId, columnType, columnLength, columnPosition;
            std::string columnName;

            std::tie(tableId, columnName, columnType, columnLength, columnPosition) = entry;

            int columnNameLength = columnName.length();
            int offset = ceil((double)columnsDescriptor.size() / 8);
            memset(columnsData, 0, offset);

            memcpy(columnsData + offset, &tableId, sizeof(int));
            offset += sizeof(int);

            memcpy(columnsData + offset, &columnNameLength, sizeof(int));
            offset += sizeof(int);
            memcpy(columnsData + offset, columnName.c_str(), columnNameLength);
            offset += columnNameLength;

            memcpy(columnsData + offset, &columnType, sizeof(int));
            offset += sizeof(int);

            memcpy(columnsData + offset, &columnLength, sizeof(int));
            offset += sizeof(int);

            memcpy(columnsData + offset, &columnPosition, sizeof(int));
            offset += sizeof(int);

            rbfm.insertRecord(columnsFileHandle, columnsDescriptor, columnsData, rid);
        }

        rbfm.closeFile(columnsFileHandle);

        catalogExists = true;
        currentTableID = 3;
        return 0;
    }


    RC RelationManager::deleteCatalog() {
        if (!catalogExists) {
            return -1;
        }

        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tablesFileHandle;

        if (rbfm.openFile("Tables", tablesFileHandle) != 0) {
            return -1;
        }

        std::vector<Attribute> tablesDescriptor = {
            {"table-id", TypeInt, 4},
            {"table-name", TypeVarChar, 50},
            {"file-name", TypeVarChar, 50}
        };

        unsigned numPages = tablesFileHandle.getNumberOfPages();
        char recordData[PAGE_SIZE];
        RID rid;

        // Iterate through the Tables file to delete all user tables
        for (unsigned pageNum = 0; pageNum < numPages; pageNum++) {
            char page[PAGE_SIZE];
            if (tablesFileHandle.readPage(pageNum, page) != 0) {
                return -1;
            }

            unsigned numSlots;
            memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

            for (unsigned slotNum = 0; slotNum < numSlots; slotNum++) {
                // Don't delete Tables and Columns yet

                rid.pageNum = pageNum;
                rid.slotNum = slotNum;

                if (rbfm.readRecord(tablesFileHandle, tablesDescriptor, rid, recordData) == 0) {
                    // Extract table name from the record
                    unsigned nullByteOffset = ceil((double)tablesDescriptor.size() / 8);
                    int tableId;
                    memcpy(&tableId, recordData + nullByteOffset, sizeof(int));

                    unsigned tableNameLength;
                    memcpy(&tableNameLength, recordData + nullByteOffset + sizeof(int), sizeof(int));

                    std::string tableName(recordData + nullByteOffset + sizeof(int) + sizeof(int), tableNameLength);

                    // Delete the table file
                    rbfm.destroyFile(tableName);
                }
            }
        }

        rbfm.closeFile(tablesFileHandle);

        // Now delete catalog files
        if (rbfm.destroyFile("Tables") != 0) {
            return -1;
        }
        if (rbfm.destroyFile("Columns") != 0) {
            return -1;
        }
        catalogExists = false;
        return 0;
    }

    RC RelationManager::createTable(const std::string &tableName, const std::vector<Attribute> &attrs) {
        if (tableName == "Tables" || tableName == "Columns") {
            return -1;
        }

        if (!catalogExists) {
            return -1;
        }
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        RID rid;

        // Create a new table file
        if (rbfm.createFile(tableName) != 0) {
            return -1;
        }

        // Update "Tables" file with the new table entry
        FileHandle tablesFileHandle;
        if (rbfm.openFile("Tables", tablesFileHandle) != 0) {
            return -1;
        }

        std::vector<Attribute> tablesDescriptor = {
            {"table-id", TypeInt, 4},
            {"table-name", TypeVarChar, 50},
            {"file-name", TypeVarChar, 50}
        };

        // Use the currentTableID property and increment it for the new table
        int tableId = currentTableID;
        currentTableID++;  // Increment the table ID for future tables
        std::string fileName = tableName;

        char tablesData[PAGE_SIZE];
        int offset = ceil((double)tablesDescriptor.size() / 8);
        memset(tablesData, 0, offset);

        // Insert table ID
        memcpy(tablesData + offset, &tableId, sizeof(int));
        offset += sizeof(int);

        // Insert table name
        int nameLength = tableName.length();
        memcpy(tablesData + offset, &nameLength, sizeof(int));
        offset += sizeof(int);
        memcpy(tablesData + offset, tableName.c_str(), nameLength);
        offset += nameLength;

        // Insert file name
        int fileNameLength = fileName.length();
        memcpy(tablesData + offset, &fileNameLength, sizeof(int));
        offset += sizeof(int);
        memcpy(tablesData + offset, fileName.c_str(), fileNameLength);

        // Insert into "Tables" file
        rbfm.insertRecord(tablesFileHandle, tablesDescriptor, tablesData, rid);
        rbfm.closeFile(tablesFileHandle);  // Close the Tables file after inserting the table

        // Step 3: Now, work on the "Columns" file
        FileHandle columnsFileHandle;
        if (rbfm.openFile("Columns", columnsFileHandle) != 0) {
            return -1;  // Failed to open the Columns file
        }

        std::vector<Attribute> columnsDescriptor = {
            {"table-id", TypeInt, 4},
            {"column-name", TypeVarChar, 50},
            {"column-type", TypeInt, 4},
            {"column-length", TypeInt, 4},
            {"column-position", TypeInt, 4}
        };

        int columnPosition = 1;
        for (const auto &attr : attrs) {
            char columnsData[PAGE_SIZE];
            offset = ceil((double)columnsDescriptor.size() / 8);
            memset(columnsData, 0, offset);

            // Insert table ID
            memcpy(columnsData + offset, &tableId, sizeof(int));
            offset += sizeof(int);

            // Insert column name
            int columnNameLength = attr.name.length();
            memcpy(columnsData + offset, &columnNameLength, sizeof(int));
            offset += sizeof(int);
            memcpy(columnsData + offset, attr.name.c_str(), columnNameLength);
            offset += columnNameLength;

            // Insert column type
            memcpy(columnsData + offset, &attr.type, sizeof(int));
            offset += sizeof(int);

            // Insert column length
            memcpy(columnsData + offset, &attr.length, sizeof(int));
            offset += sizeof(int);

            // Insert column position
            memcpy(columnsData + offset, &columnPosition, sizeof(int));
            offset += sizeof(int);

            // Insert into "Columns" file
            rbfm.insertRecord(columnsFileHandle, columnsDescriptor, columnsData, rid);
            columnPosition++;
        }

        rbfm.closeFile(columnsFileHandle);

        return 0;
    }


    RC RelationManager::deleteTable(const std::string &tableName) {
        if (!catalogExists) {
            return -1;
        }
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tablesFileHandle, columnsFileHandle;

        std::vector<Attribute> tablesDescriptor = {
            {"table-id", TypeInt, 4},
            {"table-name", TypeVarChar, 50},
            {"file-name", TypeVarChar, 50}
        };

        std::vector<Attribute> columnsDescriptor = {
            {"table-id", TypeInt, 4},
            {"column-name", TypeVarChar, 50},
            {"column-type", TypeInt, 4},
            {"column-length", TypeInt, 4},
            {"column-position", TypeInt, 4}
        };

        // Open Tables file
        if (rbfm.openFile("Tables", tablesFileHandle) != 0) {
            return -1;
        }

        RID rid;
        char recordData[PAGE_SIZE];
        int tableIdToDelete = -1;
        bool tableFound = false;

        unsigned numPages = tablesFileHandle.getNumberOfPages();
        for (unsigned pageNum = 0; pageNum < numPages; pageNum++) {
            char page[PAGE_SIZE];
            if (tablesFileHandle.readPage(pageNum, page) != 0) {
                return -1;
            }

            // Retrieve the number of slots from the slot directory
            unsigned numSlots;
            memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

            for (unsigned slotNum = 0; slotNum < numSlots; slotNum++) {
                // Don't delete Tables and Columns
                if ((pageNum == 0 && slotNum == 0) || (pageNum == 0 && slotNum == 1)) {
                    continue;
                }
                rid.pageNum = pageNum;
                rid.slotNum = slotNum;

                if (rbfm.readRecord(tablesFileHandle, tablesDescriptor, rid, recordData) == 0) {
                    // rbfm.printRecord(tablesDescriptor, recordData, std::cout);
                    if (rbfm.checkCondition(recordData, tablesDescriptor, "table-name", EQ_OP, tableName.c_str())) {
                        unsigned nullByteOffset = ceil((double)tablesDescriptor.size() / 8);
                        memcpy(&tableIdToDelete, recordData + nullByteOffset, sizeof(int));  // Extract table-id
                        rbfm.deleteRecord(tablesFileHandle, tablesDescriptor, rid);
                        tableFound = true;
                        break;
                    }
                }
            }
            if (tableFound) break;
        }

        if (!tableFound) {
            return -1;
        }

        rbfm.closeFile(tablesFileHandle);
        rbfm.destroyFile(tableName); // I know that when I create tables the file and table name are the same, so this works

        // Open Columns file
        if (rbfm.openFile("Columns", columnsFileHandle) != 0) {
            return -1;
        }

        numPages = columnsFileHandle.getNumberOfPages();

        for (unsigned pageNum = 0; pageNum < numPages; pageNum++) {
            char page[PAGE_SIZE];
            if (columnsFileHandle.readPage(pageNum, page) != 0) {
                return -1;
            }

            unsigned numSlots;
            memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

            for (unsigned slotNum = 0; slotNum < numSlots; slotNum++) {
                rid.pageNum = pageNum;
                rid.slotNum = slotNum;

                if (rbfm.readRecord(columnsFileHandle, columnsDescriptor, rid, recordData) == 0) {
                    if (rbfm.checkCondition(recordData, columnsDescriptor, "table-id", EQ_OP, &tableIdToDelete)) {
                        rbfm.deleteRecord(columnsFileHandle, columnsDescriptor, rid);
                    }
                }
            }
        }

        rbfm.closeFile(columnsFileHandle);
        return 0;
    }

    RC RelationManager::findTableId(std::string tableName, int &tableId) {
        if (!catalogExists) {
            return -1;
        }
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tablesFileHandle;
        std::vector<Attribute> tablesDescriptor = {
            {"table-id", TypeInt, 4},
            {"table-name", TypeVarChar, 50},
            {"file-name", TypeVarChar, 50}
        };

        if (rbfm.openFile("Tables", tablesFileHandle) != 0) {
            return -1;
        }

        RID rid;
        char recordData[PAGE_SIZE];
        unsigned numPages = tablesFileHandle.getNumberOfPages();

        for (unsigned pageNum = 0; pageNum < numPages; pageNum++) {
            char page[PAGE_SIZE];
            if (tablesFileHandle.readPage(pageNum, page) != 0) {
                return -1;
            }

            unsigned numSlots;
            memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

            for (unsigned slotNum = 0; slotNum < numSlots; slotNum++) {
                rid.pageNum = pageNum;
                rid.slotNum = slotNum;

                if (rbfm.readRecord(tablesFileHandle, tablesDescriptor, rid, recordData) == 0) {
                    if (rbfm.checkCondition(recordData, tablesDescriptor, "table-name", EQ_OP, tableName.c_str())) {
                        unsigned nullByteOffset = static_cast<size_t>(ceil(static_cast<double>(tablesDescriptor.size()) / 8));
                        memcpy(&tableId, recordData + nullByteOffset, sizeof(int));
                        rbfm.closeFile(tablesFileHandle);
                        return 0;
                    }
                }
            }
        }

        rbfm.closeFile(tablesFileHandle);
        return -1;
    }

    RC RelationManager::getAttributes(const std::string &tableName, std::vector<Attribute> &attrs) {
        int tableId;
        if (findTableId(tableName, tableId) != 0) {
            return -1;
        }

        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle columnsFileHandle;

        std::vector<Attribute> columnsDescriptor = {
            {"table-id", TypeInt, 4},
            {"column-name", TypeVarChar, 50},
            {"column-type", TypeInt, 4},
            {"column-length", TypeInt, 4},
            {"column-position", TypeInt, 4}
        };

        if (rbfm.openFile("Columns", columnsFileHandle) != 0) {
            return -1;
        }

        unsigned numPages = columnsFileHandle.getNumberOfPages();

        for (unsigned pageNum = 0; pageNum < numPages; pageNum++) {
            char page[PAGE_SIZE];
            if (columnsFileHandle.readPage(pageNum, page) != 0) {
                return -1;
            }

            unsigned numSlots;
            memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

            for (unsigned slotNum = 0; slotNum < numSlots; slotNum++) {
                RID rid;
                char recordData[PAGE_SIZE];
                rid.pageNum = pageNum;
                rid.slotNum = slotNum;

                if (rbfm.readRecord(columnsFileHandle, columnsDescriptor, rid, recordData) == 0) {
                    if (rbfm.checkCondition(recordData, columnsDescriptor, "table-id", EQ_OP, &tableId)) {
                        Attribute attr;
                        unsigned offset = ceil((double)columnsDescriptor.size() / 8) + sizeof(int);

                        int varcharLength;
                        memcpy(&varcharLength, recordData + offset, sizeof(int));
                        offset += sizeof(int);
                        char columnName[varcharLength + 1];
                        memcpy(columnName, recordData + offset, varcharLength);
                        columnName[varcharLength] = '\0';
                        attr.name = std::string(columnName);
                        offset += varcharLength;

                        memcpy(&attr.type, recordData + offset, sizeof(int));
                        offset += sizeof(int);

                        memcpy(&attr.length, recordData + offset, sizeof(int));

                        attrs.push_back(attr);
                    }
                }
            }
        }

        rbfm.closeFile(columnsFileHandle);
        return 0;
    }



    RC RelationManager::insertTuple(const std::string &tableName, const void *data, RID &rid) {
        if (tableName == "Tables" || tableName == "Columns") {
            return -1;
        }
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);
        if (rbfm.insertRecord(tableHandle, recordDescriptor, data, rid) == -1) {
            return -1;
        }
        return 0;
    }

    RC RelationManager::deleteTuple(const std::string &tableName, const RID &rid) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);
        if (rbfm.deleteRecord(tableHandle, recordDescriptor, rid) == -1) {
            return -1;
        }
        return 0;
    }

    RC RelationManager::updateTuple(const std::string &tableName, const void *data, const RID &rid) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);
        if (rbfm.updateRecord(tableHandle, recordDescriptor, data, rid) == -1) {
            return -1;
        }
        return 0;
    }

    RC RelationManager::readTuple(const std::string &tableName, const RID &rid, void *data) {
        int tableID;
        if (findTableId(tableName, tableID) == -1) {
            return -1;
        }
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);
        if (rbfm.readRecord(tableHandle, recordDescriptor, rid, data) == -1) {
            return -1;
        }
        return 0;
    }

    RC RelationManager::printTuple(const std::vector<Attribute> &attrs, const void *data, std::ostream &out) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        if (rbfm.printRecord(attrs, data, out) == -1) {
            return -1;
        }
        return 0;
    }

    RC RelationManager::readAttribute(const std::string &tableName, const RID &rid, const std::string &attributeName,
                                      void *data) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);
        if (rbfm.readAttribute(tableHandle, recordDescriptor, rid, attributeName, data) == -1) {
            return -1;
        }
        return 0;
    }

    RC RelationManager::scan(const std::string &tableName,
                             const std::string &conditionAttribute,
                             const CompOp compOp,
                             const void *value,
                             const std::vector<std::string> &attributeNames,
                             RM_ScanIterator &rm_ScanIterator) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        if (getAttributes(tableName, recordDescriptor) == -1) {
            return -1;
        }
        rm_ScanIterator.rbfmIter = new RBFM_ScanIterator(tableName + "_scan");
        rm_ScanIterator.rbfmIter->initialize(tableName + "_scan");
        if (rbfm.scan(tableHandle, recordDescriptor, conditionAttribute, compOp, value, attributeNames, *rm_ScanIterator.rbfmIter) != 0) {
            return -1;
        }
        return 0;
    }

    void RM_ScanIterator::initialize(RBFM_ScanIterator *rbfmIter) {
        this->rbfmIter = rbfmIter;
    }

    RC RM_ScanIterator::getNextTuple(RID &rid, void *data) {
        if (this->rbfmIter->getNextRecord(rid, data) == -1) {
            return -1;
        }
        return 0;
    }

    RC RM_ScanIterator::close() {
        if (this->rbfmIter == NULL) {
            std::cout << "RM_ScanIterator::close() called with NULL iterator" << std::endl;
            return 0;
        }
        if (this->rbfmIter->close() == -1) {
            return -1;
        }
        std::cout << "RM_ScanIterator::close() called" << std::endl;
        return 0;
    }

    RM_ScanIterator::RM_ScanIterator() = default;

    RM_ScanIterator::~RM_ScanIterator() = default;

    // Extra credit work
    RC RelationManager::dropAttribute(const std::string &tableName, const std::string &attributeName) {
        return -1;
    }

    // Extra credit work
    RC RelationManager::addAttribute(const std::string &tableName, const Attribute &attr) {
        return -1;
    }

    // QE IX related
    RC RelationManager::createIndex(const std::string &tableName, const std::string &attributeName){
        return -1;
    }

    RC RelationManager::destroyIndex(const std::string &tableName, const std::string &attributeName){
        return -1;
    }

    // indexScan returns an iterator to allow the caller to go through qualified entries in index
    RC RelationManager::indexScan(const std::string &tableName,
                 const std::string &attributeName,
                 const void *lowKey,
                 const void *highKey,
                 bool lowKeyInclusive,
                 bool highKeyInclusive,
                 RM_IndexScanIterator &rm_IndexScanIterator){
        return -1;
    }


    RM_IndexScanIterator::RM_IndexScanIterator() = default;

    RM_IndexScanIterator::~RM_IndexScanIterator() = default;

    RC RM_IndexScanIterator::getNextEntry(RID &rid, void *key){
        return -1;
    }

    RC RM_IndexScanIterator::close(){
        return -1;
    }

} // namespace PeterDB