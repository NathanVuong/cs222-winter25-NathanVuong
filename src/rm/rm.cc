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
#include <src/include/ix.h>


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
            // std::cout << "Catalog not found" << std::endl;
            return 0;
        }

        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tablesFileHandle;

        if (rbfm.openFile("Tables", tablesFileHandle) != 0) {
            std::cout << "Tables file cannot be opened" << std::endl;
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
                std::cout << "Tables file page cannot be read: " << pageNum << std::endl;
                return -1;
            }

            unsigned numSlots;
            memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

            for (unsigned slotNum = 0; slotNum < numSlots; slotNum++) {
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

                    // Don't delete Tables and Columns yet
                    if (tableName == "Tables" || tableName == "Columns") {
                        continue;
                    }

                    // Delete the table file
                    rbfm.destroyFile(tableName);
                }
            }
        }

        rbfm.closeFile(tablesFileHandle);

        // Now delete catalog files
        if (rbfm.destroyFile("Tables") != 0) {
            std::cout << "Tables file cannot be destroyed" << std::endl;
            return -1;
        }
        if (rbfm.destroyFile("Columns") != 0) {
            std::cout << "Columns file cannot be destroyed" << std::endl;
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

    // Search directory for index files that are associated with a record file (ends in .idx)
    void RelationManager::deleteTableIndexes(const std::string &tableName) {
        DIR *dir;
        struct dirent *entry;
        if ((dir = opendir(".")) != NULL) {
            while ((entry = readdir(dir)) != NULL) {
                std::string fileName = entry->d_name;
                if (fileName.find(tableName + "_") == 0 && fileName.find(".idx") == fileName.length() - 4) {
                    std::string attributeName = fileName.substr(tableName.length() + 1, fileName.length() - tableName.length() - 5);
                    destroyIndex(tableName, attributeName);
                }
            }
            closedir(dir);
        }
    }

    RC RelationManager::deleteTable(const std::string &tableName) {
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
            std::cout << "Failed to open tables file" << std::endl;
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
                std::cout << "Failed to read tables page " << pageNum << std::endl;
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
                    unsigned tableNameLength = tableName.length();
                    char value[sizeof(unsigned) + tableNameLength];
                    memcpy(value, &tableNameLength, sizeof(unsigned));
                    memcpy(value + sizeof(unsigned), tableName.c_str(), tableNameLength);
                    if (rbfm.checkCondition(recordData, tablesDescriptor, "table-name", EQ_OP, value)) {
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
            std::cout << "Table " << tableName << " not found" << std::endl;
            return -1;
        }

        rbfm.closeFile(tablesFileHandle);
        rbfm.destroyFile(tableName); // I know that when I create tables the file and table name are the same, so this works

        // Open Columns file
        if (rbfm.openFile("Columns", columnsFileHandle) != 0) {
            std::cout << "Failed to open columns file" << std::endl;
            return -1;
        }

        numPages = columnsFileHandle.getNumberOfPages();

        for (unsigned pageNum = 0; pageNum < numPages; pageNum++) {
            char page[PAGE_SIZE];
            if (columnsFileHandle.readPage(pageNum, page) != 0) {
                std::cout << "Failed to read columns page " << pageNum << std::endl;
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

        deleteTableIndexes(tableName);
        return 0;
}



    RC RelationManager::findTableId(std::string tableName, int &tableId) {
        /*
        if (!catalogExists) {
            return -1;
        }
        */
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
                std::cout << "Error in reading page " << pageNum << std::endl;
                return -1;
            }

            unsigned numSlots;
            memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

            for (unsigned slotNum = 0; slotNum < numSlots; slotNum++) {
                rid.pageNum = pageNum;
                rid.slotNum = slotNum;

                if (rbfm.readRecord(tablesFileHandle, tablesDescriptor, rid, recordData) == 0) {
                    unsigned tableNameLength = tableName.length();
                    char value[sizeof(unsigned) + tableNameLength];
                    memcpy(value, &tableNameLength, sizeof(unsigned));
                    memcpy(value + sizeof(unsigned), tableName.c_str(), tableNameLength);
                    if (rbfm.checkCondition(recordData, tablesDescriptor, "table-name", EQ_OP, value)) {
                        unsigned nullByteOffset = static_cast<size_t>(ceil(static_cast<double>(tablesDescriptor.size()) / 8));
                        memcpy(&tableId, recordData + nullByteOffset, sizeof(int));
                        rbfm.closeFile(tablesFileHandle);
                        return 0;
                    }
                }
            }
        }

        // rbfm.closeFile(tablesFileHandle);
        std::cout << "Table " << tableName << " not found" << std::endl;
        return -1;
    }

    RC RelationManager::getAttributes(const std::string &tableName, std::vector<Attribute> &attrs) {
        int tableId;
        if (findTableId(tableName, tableId) != 0) {
            std::cout << "Table " << tableName << " not found" << std::endl;
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
            std::cout << "Opening columns failed" << std::endl;
            return -1;
        }

        unsigned numPages = columnsFileHandle.getNumberOfPages();

        for (unsigned pageNum = 0; pageNum < numPages; pageNum++) {
            char page[PAGE_SIZE];
            if (columnsFileHandle.readPage(pageNum, page) != 0) {
                std::cout << "Reading page failed: " << pageNum << std::endl;
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

        insertTupleIndexes(tableName, recordDescriptor, data, rid);

        return 0;
    }

    void RelationManager::insertTupleIndexes(const std::string &tableName, std::vector<Attribute> recordDescriptor, const void *data, const RID &rid) {
        IndexManager &im = IndexManager::instance();
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();

        size_t nullIndicatorSize = static_cast<size_t>(ceil(static_cast<double>(recordDescriptor.size()) / 8));
        const char *dataPtr = static_cast<const char *>(data);
        const char *nullIndicator = dataPtr;
        dataPtr += nullIndicatorSize;

        DIR *dir;
        struct dirent *entry;
        if ((dir = opendir(".")) != NULL) {
            while ((entry = readdir(dir)) != NULL) {
                std::string fileName = entry->d_name;
                if (fileName.find(tableName + "_") == 0 && fileName.find(".idx") == fileName.length() - 4) {
                    std::string attributeName = fileName.substr(tableName.length() + 1, fileName.length() - tableName.length() - 5);
                    // std::cout << "Found index for attribute: " << attributeName << std::endl;

                    // Find corresponding attribute
                    Attribute indexAttribute;
                    size_t offset = 0;
                    bool found = false;
                    for (size_t i = 0; i < recordDescriptor.size(); ++i) {
                        if (recordDescriptor[i].name == attributeName) {
                            indexAttribute = recordDescriptor[i];
                            found = true;
                            break;
                        }

                        // Adjust offset based on attribute type
                        size_t bytePos = i / 8;
                        size_t bitPos = 7 - (i % 8);
                        bool isNull = nullIndicator[bytePos] & (1 << bitPos);
                        if (isNull) continue;

                        if (recordDescriptor[i].type == TypeInt || recordDescriptor[i].type == TypeReal) {
                            offset += 4;
                        } else if (recordDescriptor[i].type == TypeVarChar) {
                            uint32_t varcharLength;
                            memcpy(&varcharLength, dataPtr + offset, sizeof(uint32_t));
                            offset += 4 + varcharLength;
                        }
                    }

                    if (!found) continue;

                    // Extract and print the data for the attribute
                    if (indexAttribute.type == TypeInt) {
                        int value;
                        memcpy(&value, dataPtr + offset, sizeof(int));
                        // std::cout << "Inserting value for " << attributeName << ": " << value << std::endl;
                    } else if (indexAttribute.type == TypeReal) {
                        float value;
                        memcpy(&value, dataPtr + offset, sizeof(float));
                        // std::cout << "Inserting value for " << attributeName << ": " << value << std::endl;
                    } else if (indexAttribute.type == TypeVarChar) {
                        uint32_t varcharLength;
                        memcpy(&varcharLength, dataPtr + offset, sizeof(uint32_t));
                        char *varcharValue = new char[varcharLength + 1];
                        memcpy(varcharValue, dataPtr + offset + 4, varcharLength);
                        varcharValue[varcharLength] = '\0';
                        // std::cout << "Inserting value for " << attributeName << ": " << varcharValue << std::endl;
                        delete[] varcharValue;
                    }

                    IXFileHandle indexFileHandle;
                    if (im.openFile(fileName, indexFileHandle) == 0) {
                        im.insertEntry(indexFileHandle, indexAttribute, dataPtr + offset, rid);
                        im.closeFile(indexFileHandle);
                    }
                }
            }
            closedir(dir);
        }
    }


    RC RelationManager::deleteTuple(const std::string &tableName, const RID &rid) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);

        deleteTupleIndexes(tableName, recordDescriptor, rid);

        if (rbfm.deleteRecord(tableHandle, recordDescriptor, rid) == -1) {
            return -1;
        }
        return 0;
    }

    void RelationManager::deleteTupleIndexes(const std::string &tableName, std::vector<Attribute> recordDescriptor, const RID &rid) {
        IndexManager &im = IndexManager::instance();
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();

        // Open the table file to read the data before deletion
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) != 0) {
            std::cerr << "Failed to open table file for reading before deletion." << std::endl;
            return;
        }

        // Read the record
        void *recordData = malloc(PAGE_SIZE);
        if (rbfm.readRecord(tableHandle, recordDescriptor, rid, recordData) != 0) {
            std::cerr << "Failed to read record before deletion." << std::endl;
            free(recordData);
            return;
        }

        size_t nullIndicatorSize = static_cast<size_t>(ceil(static_cast<double>(recordDescriptor.size()) / 8));
        const char *dataPtr = static_cast<const char *>(recordData);
        const char *nullIndicator = dataPtr;
        dataPtr += nullIndicatorSize;

        DIR *dir;
        struct dirent *entry;
        if ((dir = opendir(".")) != NULL) {
            while ((entry = readdir(dir)) != NULL) {
                std::string fileName = entry->d_name;
                if (fileName.find(tableName + "_") == 0 && fileName.find(".idx") == fileName.length() - 4) {
                    std::string attributeName = fileName.substr(tableName.length() + 1, fileName.length() - tableName.length() - 5);
                    // Print attribute name being processed
                    // std::cout << "Processing index for attribute: " << attributeName << std::endl;

                    // Find corresponding attribute
                    Attribute indexAttribute;
                    size_t offset = 0;
                    bool found = false;
                    for (size_t i = 0; i < recordDescriptor.size(); ++i) {
                        if (recordDescriptor[i].name == attributeName) {
                            indexAttribute = recordDescriptor[i];
                            found = true;
                            break;
                        }

                        // Adjust offset based on attribute type
                        size_t bytePos = i / 8;
                        size_t bitPos = 7 - (i % 8);
                        bool isNull = nullIndicator[bytePos] & (1 << bitPos);
                        if (isNull) continue;

                        if (recordDescriptor[i].type == TypeInt || recordDescriptor[i].type == TypeReal) {
                            offset += 4;
                        } else if (recordDescriptor[i].type == TypeVarChar) {
                            uint32_t varcharLength;
                            memcpy(&varcharLength, dataPtr + offset, sizeof(uint32_t));
                            offset += 4 + varcharLength;
                        }
                    }

                    if (!found) continue;

                    // Extract and print the data for the attribute before deletion
                    if (indexAttribute.type == TypeInt) {
                        int value;
                        memcpy(&value, dataPtr + offset, sizeof(int));
                    } else if (indexAttribute.type == TypeReal) {
                        float value;
                        memcpy(&value, dataPtr + offset, sizeof(float));
                    } else if (indexAttribute.type == TypeVarChar) {
                        uint32_t varcharLength;
                        memcpy(&varcharLength, dataPtr + offset, sizeof(uint32_t));
                        char *varcharValue = new char[varcharLength + 1];
                        memcpy(varcharValue, dataPtr + offset + 4, varcharLength);
                        varcharValue[varcharLength] = '\0';
                        delete[] varcharValue;
                    }

                    // Open the index file and delete the entry
                    IXFileHandle indexFileHandle;
                    if (im.openFile(fileName, indexFileHandle) == 0) {
                        im.deleteEntry(indexFileHandle, indexAttribute, dataPtr + offset, rid);
                        im.closeFile(indexFileHandle);
                    }
                }
            }
            closedir(dir);
        }

        free(recordData);
    }


    RC RelationManager::updateTuple(const std::string &tableName, const void *data, const RID &rid) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);

        deleteTupleIndexes(tableName, recordDescriptor, rid);
        insertTupleIndexes(tableName, recordDescriptor, data, rid);

        if (rbfm.updateRecord(tableHandle, recordDescriptor, data, rid) == -1) {
            return -1;
        }
        return 0;
    }

    RC RelationManager::readTuple(const std::string &tableName, const RID &rid, void *data) {
        int tableID;
        if (findTableId(tableName, tableID) == -1) {
            std::cout << "TableID for " << tableName << " not found" << std::endl;
            return -1;
        }
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tableHandle;
        if (rbfm.openFile(tableName, tableHandle) == -1) {
            std::cout << "Opening " << tableName << " failed" << std::endl;
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);
        if (rbfm.readRecord(tableHandle, recordDescriptor, rid, data) == -1) {
            std::cout << "Reading record for " << tableName << " failed" << std::endl;
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
            std::cout << "Opening " << tableName << " failed" << std::endl;
            return -1;
        }
        std::vector<Attribute> recordDescriptor;
        if (getAttributes(tableName, recordDescriptor) == -1) {
            std::cout << "Getting attributes failed" << std::endl;
            return -1;
        }
        rm_ScanIterator.rbfmIter = new RBFM_ScanIterator(tableName + "_scan");
        rm_ScanIterator.rbfmIter->initialize(tableName + "_scan");
        if (rbfm.scan(tableHandle, recordDescriptor, conditionAttribute, compOp, value, attributeNames, *rm_ScanIterator.rbfmIter) != 0) {
            std::cout << "Scanning failed" << std::endl;
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
    RC RelationManager::createIndex(const std::string &tableName, const std::string &attributeName) {
        IndexManager &im = IndexManager::instance();
        std::string indexFileName = tableName + "_" + attributeName + ".idx";

        // Create the index file
        if (im.createFile(indexFileName) != 0) {
            std::cerr << "Error: Failed to create index file " << indexFileName << std::endl;
            return -1;
        }

        // Insert metadata into "Tables" system table
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        FileHandle tablesFileHandle;

        if (rbfm.openFile("Tables", tablesFileHandle) != 0) {
            std::cerr << "Error: Failed to open Tables system table" << std::endl;
            return -1;
        }

        std::vector<Attribute> tablesDescriptor = {
            {"table-id", TypeInt, 4},
            {"table-name", TypeVarChar, 50},
            {"file-name", TypeVarChar, 50}
        };

        RID rid;
        int tableId = currentTableID++;

        char tablesData[PAGE_SIZE];
        int offset = ceil((double)tablesDescriptor.size() / 8);
        memset(tablesData, 0, offset);

        // Insert table ID
        memcpy(tablesData + offset, &tableId, sizeof(int));
        offset += sizeof(int);

        // Insert index name
        int nameLength = indexFileName.length();
        memcpy(tablesData + offset, &nameLength, sizeof(int));
        offset += sizeof(int);
        memcpy(tablesData + offset, indexFileName.c_str(), nameLength);
        offset += nameLength;

        // Insert file name (same as index name in this case)
        memcpy(tablesData + offset, &nameLength, sizeof(int));
        offset += sizeof(int);
        memcpy(tablesData + offset, indexFileName.c_str(), nameLength);

        // Insert the index metadata into "Tables"
        if (rbfm.insertRecord(tablesFileHandle, tablesDescriptor, tablesData, rid) != 0) {
            std::cerr << "Error: Failed to insert index metadata into Tables" << std::endl;
            return -1;
        }

        rbfm.closeFile(tablesFileHandle);

        // Open the table file
        FileHandle tableFileHandle;
        if (rbfm.openFile(tableName, tableFileHandle) != 0) {
            std::cerr << "Error: Failed to open table file " << tableName << std::endl;
            return -1;
        }

        // Retrieve table attributes
        std::vector<Attribute> recordDescriptor;
        if (getAttributes(tableName, recordDescriptor) != 0) {
            std::cerr << "Error: Failed to get attributes for table " << tableName << std::endl;
            return -1;
        }

        // Find the indexed attribute
        Attribute indexAttribute;
        bool found = false;
        for (const auto &attr : recordDescriptor) {
            if (attr.name == attributeName) {
                indexAttribute = attr;
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "Error: Attribute " << attributeName << " not found in table " << tableName << std::endl;
            return -1;
        }

        // Open the index file
        IXFileHandle indexFileHandle;
        if (im.openFile(indexFileName, indexFileHandle) != 0) {
            std::cerr << "Error: Failed to open index file " << indexFileName << std::endl;
            return -1;
        }

        // Scan the table and populate the index
        RM_ScanIterator rm_ScanIterator;
        std::vector<std::string> attributeNames = {};

        for (const auto &attr : recordDescriptor) {
            attributeNames.push_back(attr.name);
        }

        if (scan(tableName, "", NO_OP, nullptr, attributeNames, rm_ScanIterator) != 0) {
            std::cerr << "Error: Failed to scan table " << tableName << std::endl;
            return -1;
        }

        RID recordRID;
        char data[PAGE_SIZE];

        while (rm_ScanIterator.getNextTuple(recordRID, data) != -1) {
            // Extract the indexed attribute
            const char *dataPtr = static_cast<const char *>(data);

            // Compute the null indicator size and move past it
            size_t nullIndicatorSize = ceil(recordDescriptor.size() / 8.0);
            dataPtr += nullIndicatorSize;

            // Now process the attributes and extract the indexed attribute
            size_t offset = 0;
            void *valuePtr = nullptr;
            uint32_t varcharLength = 0;

            for (const auto &attr : recordDescriptor) {
                // Check if the attribute is NULL
                size_t bytePos = &attr - &recordDescriptor[0];
                size_t bitPos = 7 - (bytePos % 8);
                bool isNull = data[bytePos / 8] & (1 << bitPos);

                if (!isNull) {
                    if (attr.name == attributeName) {
                        valuePtr = (void *)(dataPtr + offset);
                        break;
                    }

                    // Move offset based on attribute type
                    if (attr.type == TypeInt || attr.type == TypeReal) {
                        offset += sizeof(int);
                    } else if (attr.type == TypeVarChar) {
                        memcpy(&varcharLength, dataPtr + offset, sizeof(uint32_t));
                        offset += sizeof(uint32_t) + varcharLength;
                    }
                }
            }

            if (valuePtr == nullptr) {
                std::cerr << "Warning: NULL value found for attribute " << attributeName << ", skipping..." << std::endl;
                continue;
            }

            // Insert into the index
            im.insertEntry(indexFileHandle, indexAttribute, valuePtr, recordRID);
        }

        // Cleanup
        rm_ScanIterator.close();
        im.closeFile(indexFileHandle);
        rbfm.closeFile(tableFileHandle);

        return 0;
    }



    RC RelationManager::destroyIndex(const std::string &tableName, const std::string &attributeName){
        IndexManager &im = IndexManager::instance();
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        std::string indexFileName = tableName + "_" + attributeName + ".idx";

        // Destroy the index file
        if (im.destroyFile(indexFileName) != 0) {
            return -1;
        }

        FileHandle tablesFileHandle;
        if (rbfm.openFile("Tables", tablesFileHandle) != 0) {
            return -1;
        }

        std::vector<Attribute> tablesDescriptor = {
            {"table-id", TypeInt, 4},
            {"table-name", TypeVarChar, 50},
            {"file-name", TypeVarChar, 50}
        };

        RID rid;
        char recordData[PAGE_SIZE];
        bool indexFound = false;

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
                    unsigned nameLength;
                    memcpy(&nameLength, recordData + sizeof(int), sizeof(unsigned));
                    char storedName[nameLength + 1];
                    memcpy(storedName, recordData + sizeof(int) + sizeof(unsigned), nameLength);
                    storedName[nameLength] = '\0';

                    if (indexFileName == std::string(storedName)) {
                        rbfm.deleteRecord(tablesFileHandle, tablesDescriptor, rid);
                        indexFound = true;
                        break;
                    }
                }
            }
            if (indexFound) break;
        }

        rbfm.closeFile(tablesFileHandle);
        return indexFound ? 0 : -1;
    }

    // indexScan returns an iterator to allow the caller to go through qualified entries in index
    RC RelationManager::indexScan(const std::string &tableName,
                 const std::string &attributeName,
                 const void *lowKey,
                 const void *highKey,
                 bool lowKeyInclusive,
                 bool highKeyInclusive,
                 RM_IndexScanIterator &rm_IndexScanIterator){

        IndexManager &im = IndexManager::instance();
        IXFileHandle indexFileHandle;

        // Construct the index filename
        std::string indexFileName = tableName + "_" + attributeName + ".idx";
        // Open the index file
        if (im.openFile(indexFileName, indexFileHandle) != 0) {
            std::cerr << "Failed to open index file: " << indexFileName << std::endl;
            return -1;
        }

        // Retrieve table attributes
        std::vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);

        // Find the matching attribute
        Attribute targetAttribute;
        bool found = false;
        for (const auto &attr : recordDescriptor) {
            if (attr.name == attributeName) {
                targetAttribute = attr;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "Attribute not found in table descriptor: " << attributeName << std::endl;
            return -1;
        }

        if (im.scan(indexFileHandle, targetAttribute, lowKey, highKey, lowKeyInclusive, highKeyInclusive, rm_IndexScanIterator.ix_ScanIterator) == -1) {
            std::cerr << "Failed to scan index file: " << indexFileName << std::endl;
            return -1;
        }


        return 0;
    }


    RM_IndexScanIterator::RM_IndexScanIterator() = default;

    RM_IndexScanIterator::~RM_IndexScanIterator() = default;

    RC RM_IndexScanIterator::getNextEntry(RID &rid, void *key){
        if (this->ix_ScanIterator.getNextEntry(rid, key) == -1) {
            return -1;
        }
        return 0;
    }

    RC RM_IndexScanIterator::close(){
        if (this->ix_ScanIterator.close() == -1) {
            std::cerr << "Failed to close index scan iterator" << std::endl;
            return -1;
        }
        return 0;
    }
} // namespace PeterDB